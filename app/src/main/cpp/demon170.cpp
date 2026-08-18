// ============================================================
//  Dodge Demon 170 Racing Simulator v8.0 - ANDROID
//  C++20, OpenGL ES 2.0, Single File
//  Compatible: Android 4.4.4 KitKat (API 19+)
//  Features: Virtual buttons, Cruise control, No clutch,
//            Auto transmission, Boost gauge, Timers
// ============================================================
//  Build: NDK CMake (see CMakeLists.txt)
//  Controls (virtual buttons on screen):
//    [GAS]    Throttle (hold)
//    [BRK]    Brake
//    [UP]     Shift up (1-8)
//    [DN]     Shift down
//    [A/M]    Toggle AUTO/MANUAL
//    [CRZ]    Cruise ON/OFF
//    [+]      Cruise +2 km/h
//    [-]      Cruise -2 km/h
//    [SET]    Cruise: cycle preset
//    [RST]    Reset all
// ============================================================

// ========== HEADER SELECTION ==========
#ifdef __TEST__
  // Desktop testing - use stub headers
  #include "test_stub.h"
#else
  // Android NDK - use real headers
  #include <jni.h>
  #include <android/log.h>
  #include <EGL/egl.h>
  #include <GLES2/gl2.h>
#endif

#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>

#if defined(__ANDROID__) || defined(ANDROID)
#define LOG_TAG "Demon170"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define JNIEXPORT __attribute__((visibility("default")))
#define JNICALL
#else
#define LOGI(...) fprintf(stdout, __VA_ARGS__)
#define LOGE(...) fprintf(stderr, __VA_ARGS__)
#define JNIEXPORT
#define JNICALL
#endif

// ============================================================
//  Dodge Demon 170 reference data
// ============================================================
namespace Car {

    // ========== TIMER (inside namespace) ==========
    static std::chrono::high_resolution_clock::time_point g_time_start;

    void timer_init() {
        g_time_start = std::chrono::high_resolution_clock::now();
    }

    double timer_now() {
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double>(now - g_time_start).count();
    }

    // ========== PHYSICS CONSTANTS ==========
    constexpr double G = 9.80665;
    constexpr double AIR_DENS = 1.225;
    constexpr double TIRE_R = 0.337;
    constexpr double MASS = 1941.0;
    constexpr double CD = 0.366;
    constexpr double FRONT_A = 2.2;
    constexpr double ROLL_RES = 0.015;
    constexpr double MECH_EFF = 0.92;

    constexpr int    IDLE_RPM = 800;
    constexpr int    REDLINE = 7000;
    constexpr int    FUEL_CUT = 7300;
    constexpr double MAX_TORQUE = 1281.0;   // Nm @ 4200 rpm
    constexpr double MAX_POWER = 764000.0;   // W @ 6500 rpm (1025 HP)

    constexpr double FINAL_DRIVE = 3.09;
    // ZF 8HP90 8-speed automatic (real Demon 170 gear ratios)
    constexpr double GEAR_RATIO[9] = { 0, 4.714, 3.143, 2.106, 1.667, 1.285, 1.000, 0.839, 0.667 };
    constexpr double GEAR_EFF[9]   = { 0, 0.95, 0.95, 0.96, 0.96, 0.97, 0.97, 0.97, 0.97 };

    // ========== SIM STATE (all atomic) ==========
    std::atomic<double> g_throttle(0.0);
    std::atomic<double> g_brake(0.0);
    std::atomic<int>    g_gear(1);
    std::atomic<double> g_rpm(800.0);
    std::atomic<double> g_speed(0.0);
    std::atomic<double> g_distance(0.0);
    std::atomic<bool>   g_running(true);

    // Cruise
    std::atomic<bool>   g_cruise_on(false);
    std::atomic<double> g_cruise_target(0.0);
    std::atomic<double> g_cruise_throttle(0.0);

    // Timers
    std::atomic<double> g_timer_start(0.0);
    std::atomic<double> g_0to100_time(-1.0);
    std::atomic<bool>   g_0to100_active(false);
    std::atomic<double> g_0to60mph_time(-1.0);
    std::atomic<bool>   g_0to60mph_active(false);
    std::atomic<double> g_quarter_mile_time(-1.0);
    std::atomic<double> g_quarter_mile_speed(-1.0);
    std::atomic<bool>   g_quarter_mile_active(false);

    // Auto-shift
    std::atomic<bool>   g_auto_shift(true);

    // ========== SCREEN LAYOUT ==========
    constexpr int COLS = 90;
    constexpr int ROWS = 36;
    constexpr int ROAD_HALF = 16;
    constexpr int CAR_ROW = 22;

    // Virtual button layout (10 buttons)
    constexpr int BTN_ROW = 30;
    constexpr int BTN_COUNT = 10;
    struct VBtn { int x, w; const char* label; };
    static const VBtn BTNS[BTN_COUNT] = {
        { 1, 8,  "GAS"   },   // 0
        { 10, 7, "BRK"   },   // 1
        { 18, 5, "UP"    },   // 2
        { 24, 5, "DN"    },   // 3
        { 30, 6, "A/M"   },   // 4
        { 37, 6, "CRZ"   },   // 5
        { 44, 4, "+"     },   // 6
        { 49, 4, "-"     },   // 7
        { 54, 6, "SET"   },   // 8
        { 61, 6, "RST"   },   // 9
    };

    // Cell buffer
    struct Cell {
        char ch;
        uint8_t fg;
        uint8_t bg;
    };

    static Cell g_cells[ROWS][COLS];
    static std::mutex g_render_mutex;

    // ============================================================
    //  Helpers
    // ============================================================
    inline double clamp(double v, double lo, double hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    }
    inline int iround(double v) { return (int)(v + 0.5); }

    double engine_torque(double rpm_val) {
        if (rpm_val < 700) return 100.0;
        if (rpm_val > (double)FUEL_CUT) return 0.0;
        // Flat-top torque curve peaking at 4200rpm
        double peak = 4200.0;
        double sigma = 1500.0;
        double tq = MAX_TORQUE * std::exp(-0.5 * std::pow((rpm_val - peak) / sigma, 2));
        // Power ceiling
        double max_tq = MAX_POWER / (2 * 3.14159265358979 * rpm_val / 60.0);
        if (max_tq < tq) tq = max_tq;
        if (rpm_val < (double)IDLE_RPM) tq = 80.0;
        return tq;
    }

    double speed_to_rpm(double spd, int gear) {
        if (gear < 1 || gear > 8) return (double)IDLE_RPM;
        double w = spd / TIRE_R * GEAR_RATIO[gear] * FINAL_DRIVE * 60.0 / (2 * 3.14159265358979);
        return clamp(w, 0.0, (double)(FUEL_CUT + 500));
    }

    // ============================================================
    //  Auto transmission (ZF 8HP90 logic)
    // ============================================================
    void auto_shift(double rpm, double thr) {
        int gr = g_gear.load();
        // Upshift: high RPM + significant throttle
        if (gr < 8 && rpm > 6500.0 && thr > 0.3) {
            ++gr;
            g_gear.store(gr);
            double spd = g_speed.load();
            g_rpm.store(clamp(speed_to_rpm(spd, gr), (double)IDLE_RPM, (double)FUEL_CUT));
        }
        // Downshift: low RPM + throttle demand
        else if (gr > 1 && rpm < 2500.0 && thr > 0.2) {
            --gr;
            g_gear.store(gr);
            double spd = g_speed.load();
            g_rpm.store(clamp(speed_to_rpm(spd, gr), (double)IDLE_RPM, (double)FUEL_CUT));
        }
        // Kickdown: full throttle at high gear
        else if (thr > 0.9 && gr > 3 && rpm < 4000.0) {
            gr = (gr > 4) ? gr - 2 : gr - 1;
            if (gr < 1) gr = 1;
            g_gear.store(gr);
            double spd = g_speed.load();
            g_rpm.store(clamp(speed_to_rpm(spd, gr), (double)IDLE_RPM, (double)FUEL_CUT));
        }
    }

    // ============================================================
    //  Cruise Control PI
    // ============================================================
    constexpr double CRUISE_KP = 0.12;
    constexpr double CRUISE_KI = 0.035;
    constexpr double CRUISE_MAX_I = 0.80;
    constexpr double CRUISE_MIN_SPD = 3.0;    // ~11 km/h
    constexpr double CRUISE_MAX_SPD = 90.0;   // ~324 km/h

    static double cruise_integral = 0.0;

    void cruise_control(double dt, double spd, double& out_throttle) {
        if (!g_cruise_on.load()) {
            cruise_integral = 0.0;
            return;
        }
        double target = g_cruise_target.load();
        double error = target - spd;
        double tentative = CRUISE_KP * error + CRUISE_KI * cruise_integral;
        bool saturating = (tentative > 1.0 && error > 0) || (tentative < 0.0 && error < 0);
        if (!saturating) {
            cruise_integral += error * dt;
            if (cruise_integral > CRUISE_MAX_I) cruise_integral = CRUISE_MAX_I;
            if (cruise_integral < -CRUISE_MAX_I) cruise_integral = -CRUISE_MAX_I;
        }
        double out = CRUISE_KP * error + CRUISE_KI * cruise_integral;
        // Deadband
        if (std::fabs(error) < 0.2) out *= 0.3;
        out = clamp(out, 0.0, 1.0);
        if (error < -2.0) out = 0.0;
        out_throttle = out;
        g_cruise_throttle.store(out);
    }

    // ============================================================
    //  Physics
    // ============================================================
    void physics(double dt) {
        double thr = g_throttle.load();
        double brk = g_brake.load();
        int    gr = g_gear.load();
        double rpm = g_rpm.load();
        double spd = g_speed.load();

        // Cruise overrides throttle
        double effective_throttle = thr;
        if (g_cruise_on.load()) {
            double cruise_out = 0.0;
            cruise_control(dt, spd, cruise_out);
            effective_throttle = cruise_out;
        } else {
            double dummy = 0;
            cruise_control(dt, spd, dummy); // resets integral
        }

        // Auto-shift
        if (g_auto_shift.load()) {
            auto_shift(rpm, effective_throttle);
            gr = g_gear.load();
        }

        // RPM update
        if (gr >= 1 && gr <= 8 && spd > 0.1) {
            double tgt = speed_to_rpm(spd, gr);
            rpm += (tgt - rpm) * clamp(dt * 8.0, 0.0, 1.0);
        } else {
            double tgt = (double)IDLE_RPM + effective_throttle * ((double)FUEL_CUT - (double)IDLE_RPM);
            rpm += (tgt - rpm) * clamp(dt * 5.0, 0.0, 1.0);
        }
        rpm = clamp(rpm, 0.0, (double)(FUEL_CUT + 200));

        // Drive force from engine
        double drive = 0.0;
        if (gr >= 1 && gr <= 8) {
            double tq = engine_torque(rpm) * effective_throttle;
            double wtq = tq * GEAR_RATIO[gr] * FINAL_DRIVE * GEAR_EFF[gr];
            drive = wtq / TIRE_R;
        }

        // Aerodynamic drag
        double drag = 0.5 * AIR_DENS * CD * FRONT_A * spd * spd;
        // Rolling resistance
        double roll = ROLL_RES * MASS * G;
        double net = drive - drag - roll;

        // Engine braking (off-throttle)
        if (effective_throttle < 0.02 && spd > 0.5 && gr >= 1) {
            double eb = engine_torque(rpm) * 0.15 * GEAR_RATIO[gr] * FINAL_DRIVE * GEAR_EFF[gr] / TIRE_R;
            net -= eb;
        }

        // Brake force
        if (brk > 0) {
            double brake_force = brk * MASS * 8.0;
            net -= brake_force;
        }

        spd += (net / MASS) * dt;
        if (spd < 0) spd = 0;

        double dist = g_distance.load() + spd * dt;

        // ===== 0-100 km/h timer =====
        if (g_0to100_active.load()) {
            if (spd * 3.6 >= 100.0) {
                g_0to100_time.store(timer_now() - g_timer_start.load());
                g_0to100_active.store(false);
            }
        } else if (thr > 0.5 && spd < 1.0 && gr == 1 && !g_cruise_on.load()) {
            g_timer_start.store(timer_now());
            g_0to100_active.store(true);
        }

        // ===== 0-60 mph timer =====
        if (g_0to60mph_active.load()) {
            if (spd * 2.237 >= 60.0) {
                g_0to60mph_time.store(timer_now() - g_timer_start.load());
                g_0to60mph_active.store(false);
            }
        } else if (thr > 0.5 && spd < 1.0 && gr == 1 && !g_cruise_on.load()) {
            g_timer_start.store(timer_now());
            g_0to60mph_active.store(true);
        }

        // ===== 1/4 mile timer =====
        constexpr double QUARTER_MILE = 402.336;
        if (g_quarter_mile_active.load()) {
            if (dist >= QUARTER_MILE) {
                g_quarter_mile_time.store(timer_now() - g_timer_start.load());
                g_quarter_mile_speed.store(spd * 3.6);
                g_quarter_mile_active.store(false);
            }
        } else if (thr > 0.5 && spd < 1.0 && gr == 1 && !g_cruise_on.load()) {
            g_timer_start.store(timer_now());
            g_quarter_mile_active.store(true);
        }

        // Store back
        g_rpm.store(rpm);
        g_speed.store(spd);
        g_distance.store(dist);
    }

    // ============================================================
    //  Rendering helpers
    // ============================================================
    static void putc(int r, int c, char ch, uint8_t fg = 7, uint8_t bg = 0) {
        if (r < 0 || r >= ROWS || c < 0 || c >= COLS) return;
        g_cells[r][c].ch = ch;
        g_cells[r][c].fg = fg;
        g_cells[r][c].bg = bg;
    }

    static void puts(int r, int c, const char* s, uint8_t fg = 7, uint8_t bg = 0) {
        for (int i = 0; s[i]; ++i) putc(r, c + i, s[i], fg, bg);
    }

    static void fillbar(int r, int c, int len, char ch, uint8_t fg = 7, uint8_t bg = 0) {
        for (int i = 0; i < len; ++i) putc(r, c + i, ch, fg, bg);
    }

    static void put_int(int r, int c, int v, int width, uint8_t fg = 7) {
        char tmp[16];
        int sign = (v < 0);
        int av = sign ? -v : v;
        int ti = 0;
        if (av == 0) tmp[ti++] = '0';
        while (av > 0) { tmp[ti++] = '0' + av % 10; av /= 10; }
        int total = ti + (sign ? 1 : 0);
        int pad = width - total;
        int col = c;
        for (int p = 0; p < pad; ++p) putc(r, col++, ' ', fg);
        if (sign) putc(r, col++, '-', fg);
        for (int i = ti - 1; i >= 0; --i) putc(r, col++, tmp[i], fg);
    }

    static void put_fixed1(int r, int c, double v, int width, uint8_t fg = 7) {
        bool sign = (v < 0);
        double av = sign ? -v : v;
        int whole = (int)av;
        int frac = (int)((av - whole) * 10 + 0.5);
        if (frac >= 10) { frac -= 10; whole++; }
        char t1[16]; int l1 = 0;
        if (whole == 0) t1[l1++] = '0';
        while (whole > 0) { t1[l1++] = '0' + whole % 10; whole = whole / 10; }
        int total_len = l1 + 1 + 1 + (sign ? 1 : 0);
        int pad = width - total_len;
        int col = c;
        for (int p = 0; p < pad; ++p) putc(r, col++, ' ', fg);
        if (sign) putc(r, col++, '-', fg);
        for (int i = l1 - 1; i >= 0; --i) putc(r, col++, t1[i], fg);
        putc(r, col++, '.', fg);
        putc(r, col++, '0' + frac, fg);
    }

    static void put_fixed2(int r, int c, double v, int width, uint8_t fg = 7) {
        bool sign = (v < 0);
        double av = sign ? -v : v;
        int whole = (int)av;
        int frac = (int)((av - whole) * 100 + 0.5);
        if (frac >= 100) { frac -= 100; whole++; }
        char t1[16]; int l1 = 0;
        if (whole == 0) t1[l1++] = '0';
        while (whole > 0) { t1[l1++] = '0' + whole % 10; whole = whole / 10; }
        int total_len = l1 + 1 + 2 + (sign ? 1 : 0);
        int pad = width - total_len;
        int col = c;
        for (int p = 0; p < pad; ++p) putc(r, col++, ' ', fg);
        if (sign) putc(r, col++, '-', fg);
        for (int i = l1 - 1; i >= 0; --i) putc(r, col++, t1[i], fg);
        putc(r, col++, '.', fg);
        putc(r, col++, '0' + frac / 10, fg);
        putc(r, col++, '0' + frac % 10, fg);
    }

    static void put_fixed0(int r, int c, double v, int width, uint8_t fg = 7) {
        bool sign = (v < 0);
        double av = sign ? -v : v;
        int whole = (int)(av + 0.5);
        char t1[16]; int l1 = 0;
        if (whole == 0) t1[l1++] = '0';
        while (whole > 0) { t1[l1++] = '0' + whole % 10; whole = whole / 10; }
        int total_len = l1 + (sign ? 1 : 0);
        int pad = width - total_len;
        int col = c;
        for (int p = 0; p < pad; ++p) putc(r, col++, ' ', fg);
        if (sign) putc(r, col++, '-', fg);
        for (int i = l1 - 1; i >= 0; --i) putc(r, col++, t1[i], fg);
    }

    static void put_fixed3(int r, int c, double v, int width, uint8_t fg = 7) {
        bool sign = (v < 0);
        double av = sign ? -v : v;
        int whole = (int)av;
        int frac = (int)((av - whole) * 1000 + 0.5);
        if (frac >= 1000) { frac -= 1000; whole++; }
        char t1[16]; int l1 = 0;
        if (whole == 0) t1[l1++] = '0';
        while (whole > 0) { t1[l1++] = '0' + whole % 10; whole = whole / 10; }
        int total_len = l1 + 1 + 3 + (sign ? 1 : 0);
        int pad = width - total_len;
        int col = c;
        for (int p = 0; p < pad; ++p) putc(r, col++, ' ', fg);
        if (sign) putc(r, col++, '-', fg);
        for (int i = l1 - 1; i >= 0; --i) putc(r, col++, t1[i], fg);
        putc(r, col++, '.', fg);
        putc(r, col++, '0' + frac / 100, fg);
        putc(r, col++, '0' + (frac / 10) % 10, fg);
        putc(r, col++, '0' + frac % 10, fg);
    }

    // ============================================================
    //  Main render
    // ============================================================
    void render() {
        std::lock_guard<std::mutex> lock(g_render_mutex);

        double spd = g_speed.load();
        double rpm = g_rpm.load();
        int    gr = g_gear.load();
        double thr = g_throttle.load();
        double brk = g_brake.load();
        double dist = g_distance.load();
        double kmh = spd * 3.6;
        bool   cruise_on = g_cruise_on.load();
        double  cruise_tgt = g_cruise_target.load();

        // Clear
        for (int r = 0; r < ROWS; ++r)
            for (int c = 0; c < COLS; ++c)
                g_cells[r][c] = { ' ', 7, 0 };

        int rc = COLS / 2;
        int rl = rc - ROAD_HALF;
        int rr = rc + ROAD_HALF;

        // ===== ROAD SURFACE =====
        int road_top = 5;
        int road_bot = CAR_ROW + 3;
        for (int r = road_top; r < road_bot; ++r) {
            for (int c = rl; c <= rr; ++c) {
                int seed = (r * 7 + c * 13) ^ ((int)dist);
                char tex = (seed & 1) ? '.' : ' ';
                putc(r, c, tex, 8);
            }
        }

        // Center dashed line
        for (int r = road_top + 1; r < road_bot; ++r) {
            int phase = (r + (int)(dist * 0.5)) % 4;
            if (phase < 2) putc(r, rc, '|', 14);
        }

        // Solid edge lines
        for (int r = road_top; r < road_bot; ++r) {
            putc(r, rl, '|', 15);
            putc(r, rr, '|', 15);
        }

        // Curb stones (red/white alternating)
        for (int r = road_top; r < road_bot; ++r) {
            int cp = (r + (int)(dist * 0.3)) % 4;
            uint8_t ca = (cp < 2) ? 12 : 15;
            putc(r, rl - 1, '#', ca);
            putc(r, rr + 1, '#', ca);
        }

        // Milestone markers
        int mk_km = (int)(dist / 1000);
        char mk_buf[16]; int mti = 0;
        mk_buf[mti++] = '[';
        if (mk_km == 0) mk_buf[mti++] = '0';
        else {
            char tmp[8]; int tli = 0;
            int mv = mk_km;
            while (mv > 0) { tmp[tli++] = '0' + mv % 10; mv /= 10; }
            for (int i = tli - 1; i >= 0; --i) mk_buf[mti++] = tmp[i];
        }
        mk_buf[mti++] = 'k'; mk_buf[mti++] = 'm'; mk_buf[mti++] = ']'; mk_buf[mti] = 0;
        for (int r = road_top + 2; r < road_bot; r += 5) {
            int lx = rl - (int)strlen(mk_buf) - 2;
            int rx = rr + 3;
            if (lx > 0) puts(r, lx, mk_buf, 10);
            puts(r, rx, mk_buf, 10);
        }

        // ===== CAR (Demon 170 widebody) =====
        const char* car_art[7] = {
            "  ________  ",
            " /        \\ ",
            "| DEMON 170|",
            "|----------|",
            "|________|_|",
            "  ||    || ",
            " (  )(    )"
        };
        int car_col = rc - 6;
        for (int i = 0; i < 7; ++i) {
            int row = CAR_ROW - 6 + i;
            if (row < 0 || row >= ROWS) continue;
            for (int j = 0; car_art[i][j]; ++j) {
                char ch = car_art[i][j];
                if (ch != ' ') putc(row, car_col + j, ch, 12);
            }
        }

        // Wheels (rotate with distance)
        const char wc[] = { 'O', 'o', 'O', 'o' };
        int wf = ((int)(dist * 3)) % 4;
        putc(CAR_ROW, car_col + 2, wc[wf], 14);
        putc(CAR_ROW, car_col + 8, wc[wf], 14);

        // Exhaust flame at full throttle
        double display_thr = cruise_on ? g_cruise_throttle.load() : thr;
        if (display_thr > 0.85 && spd > 5) {
            putc(CAR_ROW - 2, car_col + 11, '~', 12);
            putc(CAR_ROW - 1, car_col + 12, '~', 10);
            putc(CAR_ROW, car_col + 12, '~', 14);
        }

        // ============================================================
        //  DASHBOARD
        // ============================================================
        const char* title = "<<< DODGE DEMON 170 - 6.2L S/C HEMI V8 >>>";
        puts(0, rc - (int)strlen(title)/2, title, 12, 0);

        // ===== RPM bar (50 chars, 7000 redline) =====
        puts(1, 2, "RPM", 14);
        fillbar(1, 6, 50, '-', 8);
        int rpm_fill = iround(clamp(rpm / 7500.0, 0.0, 1.0) * 50);
        for (int i = 0; i < rpm_fill; ++i) {
            uint8_t at = (i > 40) ? 12 : (i > 28 ? 14 : 10);
            char ch = (i > 40) ? '=' : '#';
            putc(1, 6 + i, ch, at);
        }
        put_fixed0(1, 58, rpm, 7, 14);
        puts(1, 66, "RPM", 7);
        if (rpm > (double)REDLINE) puts(1, 72, "!!! REV LIMITER !!!", 12, 0);
        else if (rpm > (double)REDLINE * 0.9) puts(1, 72, "!! REDLINE !!", 14, 0);

        // ===== Gear display =====
        puts(2, rc - 10, "GEAR:", 15);
        char gbuf[8];
        gbuf[0] = '['; gbuf[1] = '0' + gr; gbuf[2] = ']'; gbuf[3] = 0;
        if (g_auto_shift.load()) puts(2, rc - 4, gbuf, 10, 0);
        else puts(2, rc - 4, gbuf, 14, 0);

        // Gear indicator bar (8 gears)
        for (int g = 1; g <= 8; ++g) {
            int gc = rc + 1 + (g - 1) * 3;
            if (g <= gr) putc(3, gc, '#', 10);
            else putc(3, gc, '.', 8);
        }
        puts(3, rc - 6, "GEARS:", 7);
        puts(3, rc + 26, g_auto_shift.load() ? "AUTO" : "MANUAL", g_auto_shift.load() ? 10 : 14);

        // ===== Speedometer =====
        puts(4, 2, "SPEED:", 11);
        put_fixed1(4, 9, kmh, 7, 15);
        puts(4, 17, "km/h", 7);
        put_fixed2(4, 22, spd, 6, 7);
        puts(4, 29, "m/s", 7);

        fillbar(4, 36, 20, '-', 8);
        int spd_fill = iround(clamp(kmh / 340.0, 0.0, 1.0) * 20);
        for (int i = 0; i < spd_fill; ++i) {
            uint8_t at = (i > 16) ? 12 : (i > 12 ? 14 : 9);
            putc(4, 36 + i, '#', at);
        }

        // ===== Boost gauge =====
        puts(4, 58, "BOOST:", 13);
        fillbar(4, 65, 12, '-', 8);
        double boost_psi = display_thr * 21.3;
        int boost_fill = iround(clamp(boost_psi / 25.0, 0.0, 1.0) * 12);
        for (int i = 0; i < boost_fill; ++i) putc(4, 65 + i, '^', i > 9 ? 12 : 11);
        put_fixed1(4, 78, boost_psi, 4, 13);
        puts(4, 83, "psi", 7);

        // ===== Throttle bar =====
        puts(5, 2, "THR:", 14);
        fillbar(5, 7, 20, '-', 8);
        int thr_fill = iround(display_thr * 20);
        if (thr_fill > 20) thr_fill = 20;
        for (int i = 0; i < thr_fill; ++i) putc(5, 7 + i, '#', i > 16 ? 12 : 10);
        put_fixed0(5, 29, display_thr * 100.0, 5, 14);
        puts(5, 35, "%", 14);

        // ===== Brake bar =====
        puts(5, 38, "BRK:", 9);
        fillbar(5, 43, 10, '-', 8);
        int brk_fill = iround(brk * 10);
        for (int i = 0; i < brk_fill; ++i) putc(5, 43 + i, '#', 12);

        // ===== Shift mode =====
        puts(5, 55, g_auto_shift.load() ? "AUTO (A/M=man)" : "MANUAL (A/M=auto)", g_auto_shift.load() ? 10 : 14);

        // ===== Cruise status =====
        puts(5, 76, "CRZ:", 13);
        if (cruise_on) {
            puts(5, 80, "ON ", 10);
            puts(6, 76, "TGT:", 10);
            put_fixed1(6, 81, cruise_tgt * 3.6, 6, 10);
            puts(6, 88, "km/h", 10);
        } else {
            puts(5, 80, "OFF", 8);
        }

        // ===== Distance =====
        put_fixed1(7, 2, dist, 9, 7);
        puts(7, 12, "m (", 7);
        put_fixed3(7, 15, dist / 1000.0, 7, 7);
        puts(7, 23, "km)", 7);

        // ===== Timers =====
        double t100 = g_0to100_time.load();
        if (t100 > 0) { puts(7, 38, "0-100:", 11); put_fixed2(7, 45, t100, 6, 11); puts(7, 52, "s", 11); }
        else if (g_0to100_active.load()) puts(7, 38, "0-100: RUNNING...", 14);

        double t60 = g_0to60mph_time.load();
        if (t60 > 0) { puts(7, 60, "0-60:", 11); put_fixed2(7, 65, t60, 5, 11); puts(7, 71, "s", 11); }
        else if (g_0to60mph_active.load()) puts(7, 60, "0-60:...", 14);

        double qm = g_quarter_mile_time.load();
        if (qm > 0) {
            puts(8, 38, "1/4mi:", 12); put_fixed2(8, 45, qm, 6, 12); puts(8, 52, "s @", 12);
            put_fixed1(8, 56, g_quarter_mile_speed.load(), 6, 12); puts(8, 63, "km/h", 12);
        } else if (g_quarter_mile_active.load()) puts(8, 38, "1/4mi: RUNNING...", 14);

        // ===== Load meter =====
        double torque_avail = engine_torque(rpm) / MAX_TORQUE;
        double load = clamp(display_thr * torque_avail, 0.0, 1.0);
        puts(8, 2, "LOAD:", 13);
        fillbar(8, 8, 30, '-', 8);
        int lf = iround(load * 30);
        for (int i = 0; i < lf; ++i) putc(8, 8 + i, '#', 13);

        // ===== Cruise output bar =====
        if (cruise_on) {
            puts(8, 42, "CR-OUT:", 9);
            fillbar(8, 50, 15, '-', 8);
            int crf = iround(g_cruise_throttle.load() * 15);
            if (crf > 15) crf = 15;
            for (int i = 0; i < crf; ++i) putc(8, 50 + i, '#', i > 11 ? 12 : 9);
        }

        // ============================================================
        //  VIRTUAL BUTTONS (10 buttons, 3 rows)
        // ============================================================
        for (int b = 0; b < BTN_COUNT; ++b) {
            int bx = BTNS[b].x;
            int bw = BTNS[b].w;
            int br = BTN_ROW;
            const char* lbl = BTNS[b].label;

            // Button frame (rectangle)
            for (int i = bx; i < bx + bw && i < COLS; ++i) {
                putc(br - 1, i, '-', 15, 1);
                putc(br + 1, i, '-', 15, 1);
            }
            putc(br - 1, bx, '+', 15, 1);
            putc(br - 1, bx + bw - 1, '+', 15, 1);
            putc(br + 1, bx, '+', 15, 1);
            putc(br + 1, bx + bw - 1, '+', 15, 1);

            // Label with state-based coloring
            int ll = (int)strlen(lbl);
            int lx = bx + (bw - ll) / 2;
            uint8_t lfg = 7, lbg = 1;
            switch (b) {
                case 0: if (thr > 0.5) { lfg = 0; lbg = 10; } break;  // GAS green
                case 1: if (brk > 0.1) { lfg = 0; lbg = 12; } break;  // BRK red
                case 5: if (cruise_on) { lfg = 0; lbg = 10; } break;  // CRZ green
                case 4: lfg = g_auto_shift.load() ? 10 : 14; break;    // A/M
            }
            puts(br, lx, lbl, lfg, lbg);
        }

        // ============================================================
        //  HELP BAR
        // ============================================================
        const char* help = "GAS=hold  UP/DN=shift  A/M=auto-man  CRZ=cruise  +=fast  -=slow  SET=preset  RST=reset";
        puts(ROWS - 2, rc - (int)strlen(help)/2, help, 7, 0);

        // ============================================================
        //  STATUS LINE
        // ============================================================
        putc(ROWS - 1, 0, ' ', 8);
        puts(ROWS - 1, 1, "Thr:", 8);
        put_fixed0(ROWS - 1, 6, display_thr * 100, 4, 8);
        puts(ROWS - 1, 11, "% Brk:", 8);
        put_fixed0(ROWS - 1, 16, brk * 100, 4, 8);
        puts(ROWS - 1, 21, "% G:", 8);
        put_int(ROWS - 1, 24, gr, 1, 8);
        puts(ROWS - 1, 26, " RPM:", 8);
        put_fixed0(ROWS - 1, 31, rpm, 6, 8);
        puts(ROWS - 1, 38, " SPD:", 8);
        put_fixed1(ROWS - 1, 43, kmh, 7, 8);
        puts(ROWS - 1, 51, "km/h D:", 8);
        put_fixed0(ROWS - 1, 58, dist, 8, 8);
        puts(ROWS - 1, 67, "m", 8);
        if (cruise_on) puts(ROWS - 1, 70, " CRZ", 10);
        puts(ROWS - 1, 76, g_auto_shift.load() ? " AUTO" : " MAN", g_auto_shift.load() ? 10 : 14);

        // ============================================================
        //  LAUNCH HINT (blinking)
        // ============================================================
        if (spd < 1.0 && !cruise_on) {
            const char* launch = "*** HOLD [GAS] TO LAUNCH ***";
            static double blink_t = 0;
            blink_t += 0.016;
            if (((int)(blink_t * 2)) % 2 == 0)
                puts(CAR_ROW + 5, rc - (int)strlen(launch)/2, launch, 14, 0);
        }

        // ============================================================
        //  SHIFT SUGGESTION
        // ============================================================
        if (g_auto_shift.load() && spd > 1.0) {
            if (rpm > 6300 && gr < 8) {
                const char* sug = ">>> UPSHIFT >>>";
                puts(CAR_ROW + 6, rc - (int)strlen(sug)/2, sug, 10, 0);
            } else if (rpm < 2800 && gr > 1 && display_thr > 0.3) {
                const char* sug = ">>> DOWNSHIFT >>>";
                puts(CAR_ROW + 6, rc - (int)strlen(sug)/2, sug, 14, 0);
            }
        }
    }

    // ============================================================
    //  Input handling (virtual button events)
    // ============================================================
    void button_press(int btn_id) {
        switch (btn_id) {
            case 0: // GAS
                if (g_cruise_on.load()) { g_cruise_on.store(false); cruise_integral = 0.0; }
                g_throttle.store(1.0);
                break;
            case 1: // BRAKE
                g_brake.store(1.0);
                if (g_cruise_on.load()) { g_cruise_on.store(false); cruise_integral = 0.0; }
                break;
            case 2: { // UP shift
                int g = g_gear.load();
                if (g < 8) { ++g; g_gear.store(g);
                    if (g_cruise_on.load()) { g_cruise_on.store(false); cruise_integral = 0.0; }
                    g_rpm.store(clamp(speed_to_rpm(g_speed.load(), g), (double)IDLE_RPM, (double)FUEL_CUT));
                }
                break;
            }
            case 3: { // DN shift
                int g = g_gear.load();
                if (g > 0) { --g; g_gear.store(g);
                    if (g_cruise_on.load()) { g_cruise_on.store(false); cruise_integral = 0.0; }
                    g_rpm.store(clamp(speed_to_rpm(g_speed.load(), g), (double)IDLE_RPM, (double)FUEL_CUT));
                }
                break;
            }
            case 4: // A/M toggle
                g_auto_shift.store(!g_auto_shift.load());
                break;
            case 5: { // CRZ toggle
                if (g_cruise_on.load()) {
                    g_cruise_on.store(false); cruise_integral = 0.0; g_throttle.store(0.0);
                } else {
                    double spd = g_speed.load();
                    if (spd >= CRUISE_MIN_SPD && spd <= CRUISE_MAX_SPD) {
                        g_cruise_on.store(true); g_cruise_target.store(spd); cruise_integral = 0.0;
                    }
                }
                break;
            }
            case 6: { // Cruise +
                if (g_cruise_on.load()) {
                    double t = g_cruise_target.load() + 2.0 / 3.6;
                    if (t > CRUISE_MAX_SPD) t = CRUISE_MAX_SPD;
                    g_cruise_target.store(t);
                }
                break;
            }
            case 7: { // Cruise -
                if (g_cruise_on.load()) {
                    double t = g_cruise_target.load() - 2.0 / 3.6;
                    if (t < CRUISE_MIN_SPD) t = CRUISE_MIN_SPD;
                    g_cruise_target.store(t);
                }
                break;
            }
            case 8: { // SET preset
                if (g_cruise_on.load()) {
                    double cur = g_cruise_target.load() * 3.6;
                    double presets[] = { 60, 80, 100, 120, 130, 150, 180, 200, 220, 250, 280, 300 };
                    int n = (int)(sizeof(presets) / sizeof(presets[0]));
                    for (int i = 0; i < n; ++i) {
                        if (cur < presets[i] - 1) { g_cruise_target.store(presets[i] / 3.6); cruise_integral = 0.0; break; }
                    }
                } else {
                    double spd = g_speed.load();
                    if (spd >= CRUISE_MIN_SPD) { g_cruise_on.store(true); g_cruise_target.store(spd); cruise_integral = 0.0; }
                }
                break;
            }
            case 9: // RST
                g_throttle.store(0.0); g_brake.store(0.0); g_gear.store(1);
                g_rpm.store((double)IDLE_RPM); g_speed.store(0.0); g_distance.store(0.0);
                g_0to100_time.store(-1.0); g_0to100_active.store(false);
                g_0to60mph_time.store(-1.0); g_0to60mph_active.store(false);
                g_quarter_mile_time.store(-1.0); g_quarter_mile_speed.store(-1.0); g_quarter_mile_active.store(false);
                g_cruise_on.store(false); g_cruise_target.store(0.0); cruise_integral = 0.0;
                g_auto_shift.store(true); g_timer_start.store(timer_now());
                break;
        }
    }

    void button_release(int btn_id) {
        if (btn_id == 0) {
            // GAS released
            if (!g_cruise_on.load()) g_throttle.store(0.0);
        } else if (btn_id == 1) {
            // BRAKE released
            g_brake.store(0.0);
        }
    }

    int hit_button(double nx, double ny) {
        double row_h = 1.0 / ROWS;
        int row = (int)(ny / row_h);
        if (row != BTN_ROW && row != BTN_ROW - 1 && row != BTN_ROW + 1) return -1;
        double col_w = 1.0 / COLS;
        int col = (int)(nx / col_w);
        for (int b = 0; b < BTN_COUNT; ++b) {
            if (col >= BTNS[b].x && col < BTNS[b].x + BTNS[b].w) return b;
        }
        return -1;
    }

    void quit() { g_running.store(false); }

} // namespace Car

// ============================================================
//  JNI exports (only compiled for Android)
// ============================================================
#if defined(__ANDROID__) || defined(ANDROID)

extern "C" {

JNIEXPORT void JNICALL
Java_com_demon170_DemonActivity_nativeInit(JNIEnv* env, jclass clazz) {
    Car::timer_init();
    Car::g_timer_start.store(Car::timer_now());
    LOGI("Demon170 nativeInit done");
}

JNIEXPORT void JNICALL
Java_com_demon170_DemonActivity_nativeQuit(JNIEnv* env, jclass clazz) {
    Car::quit();
}

JNIEXPORT void JNICALL
Java_com_demon170_DemonActivity_nativeButtonDown(JNIEnv* env, jclass clazz, jint btn) {
    Car::button_press((int)btn);
}

JNIEXPORT void JNICALL
Java_com_demon170_DemonActivity_nativeButtonUp(JNIEnv* env, jclass clazz, jint btn) {
    Car::button_release((int)btn);
}

JNIEXPORT jint JNICALL
Java_com_demon170_DemonActivity_nativeHitTest(JNIEnv* env, jclass clazz, jfloat nx, jfloat ny) {
    return (jint)Car::hit_button((double)nx, (double)ny);
}

JNIEXPORT void JNICALL
Java_com_demon170_DemonActivity_nativeRender(JNIEnv* env, jclass clazz) {
    Car::render();
}

JNIEXPORT jboolean JNICALL
Java_com_demon170_DemonActivity_nativeIsRunning(JNIEnv* env, jclass clazz) {
    return (jboolean)Car::g_running.load();
}

JNIEXPORT jbyteArray JNICALL
Java_com_demon170_DemonActivity_nativeGetCells(JNIEnv* env, jclass clazz) {
    jbyteArray arr = env->NewByteArray(Car::ROWS * Car::COLS * 3);
    if (!arr) return nullptr;

    std::lock_guard<std::mutex> lock(Car::g_render_mutex);

    jbyte* buf = env->GetByteArrayElements(arr, nullptr);
    for (int r = 0; r < Car::ROWS; ++r) {
        for (int c = 0; c < Car::COLS; ++c) {
            int idx = (r * Car::COLS + c) * 3;
            buf[idx + 0] = (jbyte)Car::g_cells[r][c].ch;
            buf[idx + 1] = (jbyte)Car::g_cells[r][c].fg;
            buf[idx + 2] = (jbyte)Car::g_cells[r][c].bg;
        }
    }
    env->ReleaseByteArrayElements(arr, buf, 0);
    return arr;
}

JNIEXPORT jdoubleArray JNICALL
Java_com_demon170_DemonActivity_nativeGetSimData(JNIEnv* env, jclass clazz) {
    jdoubleArray arr = env->NewDoubleArray(10);
    if (!arr) return nullptr;
    jdouble buf[10];
    buf[0] = Car::g_speed.load() * 3.6;
    buf[1] = Car::g_rpm.load();
    buf[2] = (double)Car::g_gear.load();
    buf[3] = Car::g_throttle.load();
    buf[4] = Car::g_brake.load();
    buf[5] = Car::g_cruise_on.load() ? 1.0 : 0.0;
    buf[6] = Car::g_cruise_target.load() * 3.6;
    buf[7] = Car::g_distance.load();
    buf[8] = Car::g_auto_shift.load() ? 1.0 : 0.0;
    buf[9] = Car::g_cruise_throttle.load();
    env->SetDoubleArrayRegion(arr, 0, 10, buf);
    return arr;
}

JNIEXPORT void JNICALL
Java_com_demon170_DemonActivity_nativePhysicsStep(JNIEnv* env, jclass clazz, jdouble dt) {
    Car::physics((double)dt);
}

} // extern "C"

#endif // defined(__ANDROID__) || defined(ANDROID)

// ============================================================
//  Main entry (standalone test mode for desktop)
// ============================================================
#if defined(__TEST__) && !defined(NO_MAIN)
int main() {
    using namespace Car;
    timer_init();
    g_timer_start.store(timer_now());

    constexpr double PHYS_DT = 1.0 / 60.0;
    constexpr double RENDER_DT = 1.0 / 30.0;
    double phys_acc = 0.0;
    double next_render = timer_now() + RENDER_DT;
    double last = timer_now();

    LOGI("Demon170 standalone test mode");

    printf("Demon 170 Sim v8.0 - Test Mode\n");
    printf("Auto-running physics...\n");

    while (g_running.load()) {
        double now = timer_now();
        double frame_dt = now - last;
        last = now;
        if (frame_dt > 0.25) frame_dt = 0.25;

        // Auto throttle for testing
        if (g_speed.load() < 100.0) {
            g_throttle.store(1.0);
        } else {
            g_throttle.store(0.0);
        }

        phys_acc += frame_dt;
        while (phys_acc >= PHYS_DT) { physics(PHYS_DT); phys_acc -= PHYS_DT; }

        if (now >= next_render) {
            render();
            printf("\rRPM=%7.0f  SPD=%6.1fkm/h  GEAR=%d  THR=%4.1f%%  DIST=%8.0fm  CRZ=%s  AUTO=%s    ",
                g_rpm.load(), g_speed.load()*3.6, g_gear.load(),
                g_throttle.load()*100, g_distance.load(),
                g_cruise_on.load() ? "ON " : "OFF",
                g_auto_shift.load() ? "YES" : "NO");
            fflush(stdout);
            next_render = now + RENDER_DT;
        }

        if (g_speed.load() > 120.0) {
            // Stop after reaching 120km/h
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    printf("\nFinal: RPM=%.0f  SPD=%.1fkm/h  DIST=%.0fm\n",
        g_rpm.load(), g_speed.load()*3.6, g_distance.load());
    return 0;
}
#endif
