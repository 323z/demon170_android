# Dodge Demon 170 Racing Simulator v8.0

**Single-file C++20 + Java Android app** — a console-style racing simulator for Android 4.4.4+ (KitKat).

## Features

- **Dodge Demon 170** real specs: 6.2L S/C HEMI V8, 1025 HP, 1281 Nm
- **8-speed ZF 8HP90** automatic with auto upshift/downshift/kickdown
- **RPM gauge** (50-char bar, redline 7000, fuel cut 7300)
- **Speedometer** + vertical bar (0-340 km/h)
- **Boost gauge** (0-25 psi, throttle-based)
- **Throttle / Brake bars** with state coloring
- **10 virtual on-screen buttons**: GAS, BRK, UP, DN, A/M, CRZ, +, -, SET, RST
- **Cruise control** (PI controller): toggle, +/- 2 km/h, preset cycle
- **0-100 km/h, 0-60 mph, 1/4 mile** timers (auto-start/stop)
- **Engine braking** when off-throttle
- **Launch hint** (blinking when stopped)
- **Shift suggestions** (upshift/downshift hints)
- **No clutch** — automatic transmission only
- **Double-buffered rendering** (60 Hz physics, 30 Hz render)
- **Zero flicker** — cell buffer rendered atomically

## Build (GitHub Actions — automatic)

1. Fork / push this repo to GitHub
2. GitHub Actions auto-builds on push to `main`/`master`
3. Download APK from **Actions → Artifacts** (`demon170-debug-apk`)
4. Install on Android 4.4.4+ device

**Why ubuntu-latest?** Fastest runner, best caching, avoids Windows排队.

## Build (Local — Windows)

### Prerequisites
- Android Studio with NDK r21+
- JDK 11
- Gradle 6.7.1 (wrapper auto-downloads from Tencent mirror)

### Steps
```cmd
git clone <your-repo-url> demon170
cd demon170
gradlew.bat :app:assembleDebug
```
APK output: `app\build\outputs\apk\debug\app-debug.apk`

## Build (Local — Linux/macOS)

```bash
chmod +x gradlew
./gradlew :app:assembleDebug
```

## Install on Device

```bash
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

## Controls (Virtual Buttons)

| Button | Action |
|--------|--------|
| **GAS** | Hold for throttle (0→100%) |
| **BRK** | Hold for brake |
| **UP** | Shift up (1→8) |
| **DN** | Shift down (8→1) |
| **A/M** | Toggle AUTO ↔ MANUAL mode |
| **CRZ** | Cruise ON/OFF (sets target = current speed) |
| **+** | Cruise target +2 km/h |
| **-** | Cruise target -2 km/h |
| **SET** | Cycle cruise preset (60→80→100→120→130→150→180→200→220→250→280→300) |
| **RST** | Reset all (speed, distance, timers, gear=1) |

## Architecture

```
demon170_android/
├── .github/workflows/build.yml   ← GitHub Actions CI
├── app/
│   ├── build.gradle              ← App-level Gradle config
│   ├── proguard-rules.pro
│   └── src/main/
│       ├── AndroidManifest.xml   ← minSdk 19 (KitKat 4.4.4)
│       ├── java/com/demon170/
│       │   ├── DemonActivity.java  ← Main activity + render/physics threads
│       │   └── DemonApp.java      ← Application class
│       ├── cpp/
│       │   ├── demon170.cpp        ← ★ Single-file C++ core (1038 lines)
│       │   ├── CMakeLists.txt      ← NDK CMake build
│       │   └── test_stub.h        ← Desktop test stubs
│       └── res/layout/
│           └── activity_demon.xml  ← (legacy layout, not used at runtime)
├── gradle/wrapper/
│   ├── gradle-wrapper.properties ← Tencent mirror
│   └── gradle-wrapper.jar
├── gradlew / gradlew.bat         ← Gradle wrapper scripts
├── build.gradle                  ← Root Gradle config
├── settings.gradle
└── gradle.properties
```

## How It Works

### C++ Core (`demon170.cpp`)
- **Single file**, no external dependencies
- All sim state in `std::atomic` (thread-safe)
- Physics: engine torque → gear ratio → wheel force → drag/roll → acceleration
- Torque curve: Gaussian peak at 4200 rpm, power ceiling at 6500 rpm
- Auto-transmission logic in `auto_shift()`
- Cruise control: PI controller with anti-windup
- Render: writes to `Cell[36][90]` buffer with fg/bg colors

### Java Layer (`DemonActivity.java`)
- **SurfaceView** full-screen (no XML layout at runtime)
- **Physics thread**: 60 Hz fixed timestep, calls `nativePhysicsStep(dt)`
- **Render thread**: 30 Hz, calls `nativeRender()` + `nativeGetCells()` → draws to Canvas
- **Touch handling**: hit-test → virtual button → JNI call
- **VGA color palette** (16 colors) mapped to Android Color

### JNI Bridge
10 native methods, all `static` (called from Java classes, not instances):
- `nativeInit()` / `nativeQuit()`
- `nativeButtonDown(int)` / `nativeButtonUp(int)`
- `nativeHitTest(float x, float y)` → returns button ID
- `nativeRender()` / `nativeGetCells()` → cell buffer
- `nativeGetSimData()` → double[10] sim state
- `nativePhysicsStep(double dt)`

## Compatibility

| Requirement | Value |
|-------------|-------|
| Min Android | 4.4.4 KitKat (API 19) |
| Target SDK | 28 (Android 9) |
| OpenGL ES | 2.0 |
| Architecture | armeabi-v7a, x86 |
| NDK | r21.4.7075529 |
| C++ Standard | C++20 |
| JDK | 11 |

## Performance

- Physics: 60 Hz fixed step (1/60s)
- Render: 30 Hz (33ms interval)
- Cell buffer: 90×36 = 3240 chars per frame
- Typical frame time: <5ms on any modern device

## License

MIT — do whatever you want.
