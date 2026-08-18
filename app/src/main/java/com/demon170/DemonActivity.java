package com.demon170;

import android.app.Activity;
import android.os.Bundle;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.MotionEvent;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.util.Log;
import android.view.View;

public class DemonActivity extends Activity implements SurfaceHolder.Callback {

    private static final String TAG = "Demon170";

    // Native methods
    public static native void nativeInit();
    public static native void nativeQuit();
    public static native void nativeButtonDown(int btn);
    public static native void nativeButtonUp(int btn);
    public static native int  nativeHitTest(float nx, float ny);
    public static native void nativeRender();
    public static native boolean nativeIsRunning();
    public static native byte[] nativeGetCells();
    public static native double[] nativeGetSimData();
    public static native void nativePhysicsStep(double dt);

    static {
        System.loadLibrary("demon170");
    }

    private SurfaceView mSurfaceView;
    private SurfaceHolder mHolder;
    private RenderThread mRenderThread;
    private PhysicsThread mPhysicsThread;

    private int mScreenW = 0;
    private int mScreenH = 0;

    // Must match C++ COLS/ROWS
    private static final int COLS = 90;
    private static final int ROWS = 36;

    // VGA color palette
    private static final int[] VGA_COLORS = {
        0xFF000000, // 0 black
        0xFF0000AA, // 1 blue
        0xFF00AA00, // 2 green
        0xFF00AAAA, // 3 cyan
        0xFFAA0000, // 4 red
        0xFFAA00AA, // 5 magenta
        0xFFAA5500, // 6 brown
        0xFFAAAAAA, // 7 light gray
        0xFF555555, // 8 dark gray
        0xFF5555FF, // 9 bright blue
        0xFF55FF55, // 10 bright green
        0xFF55FFFF, // 11 bright cyan
        0xFFFF5555, // 12 bright red
        0xFFFF55FF, // 13 bright magenta
        0xFFFFFF55, // 14 yellow
        0xFFFFFFFF, // 15 white
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        mSurfaceView = new SurfaceView(this);
        mSurfaceView.getHolder().addCallback(this);

        // Touch listener for virtual buttons
        mSurfaceView.setOnTouchListener(new View.OnTouchListener() {
            @Override
            public boolean onTouch(View v, MotionEvent event) {
                int action = event.getActionMasked();
                float x = event.getX();
                float y = event.getY();

                if (mScreenW == 0 || mScreenH == 0) return true;

                float nx = x / mScreenW;
                float ny = y / mScreenH;

                int btn = nativeHitTest(nx, ny);

                if (action == MotionEvent.ACTION_DOWN ||
                    action == MotionEvent.ACTION_POINTER_DOWN) {
                    if (btn >= 0) {
                        nativeButtonDown(btn);
                        Log.d(TAG, "BTN DOWN: " + btn);
                    }
                } else if (action == MotionEvent.ACTION_UP ||
                           action == MotionEvent.ACTION_POINTER_UP ||
                           action == MotionEvent.ACTION_CANCEL) {
                    if (btn >= 0) {
                        nativeButtonUp(btn);
                    }
                    // Also send up for any button on global up
                    if (action == MotionEvent.ACTION_UP) {
                        // Release all buttons on finger up
                        for (int i = 0; i < 10; i++) {
                            nativeButtonUp(i);
                        }
                    }
                }
                return true;
            }
        });

        setContentView(mSurfaceView);
        nativeInit();
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        mHolder = holder;
        mScreenW = mSurfaceView.getWidth();
        mScreenH = mSurfaceView.getHeight();
        Log.d(TAG, "Surface created: " + mScreenW + "x" + mScreenH);

        if (mRenderThread == null || !mRenderThread.isAlive()) {
            mRenderThread = new RenderThread();
            mRenderThread.start();
        }
        if (mPhysicsThread == null || !mPhysicsThread.isAlive()) {
            mPhysicsThread = new PhysicsThread();
            mPhysicsThread.start();
        }
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        mScreenW = width;
        mScreenH = height;
        Log.d(TAG, "Surface changed: " + width + "x" + height);
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        if (mRenderThread != null) {
            mRenderThread.stopThread();
            try { mRenderThread.join(1000); } catch (InterruptedException e) {}
        }
        if (mPhysicsThread != null) {
            mPhysicsThread.stopThread();
            try { mPhysicsThread.join(1000); } catch (InterruptedException e) {}
        }
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        nativeQuit();
    }

    // ============================================================
    //  Physics thread: 60 Hz fixed timestep
    // ============================================================
    class PhysicsThread extends Thread {
        private volatile boolean mRunning = true;
        private long mLastTime = System.nanoTime();

        void stopThread() { mRunning = false; }

        @Override
        public void run() {
            while (mRunning && nativeIsRunning()) {
                long now = System.nanoTime();
                double dt = (now - mLastTime) / 1_000_000_000.0;
                mLastTime = now;
                if (dt > 0.25) dt = 0.25;

                // Fixed step at 60Hz
                double acc = dt;
                double step = 1.0 / 60.0;
                int maxSteps = 5; // prevent spiral of death
                while (acc >= step && maxSteps-- > 0) {
                    nativePhysicsStep(step);
                    acc -= step;
                }

                try { Thread.sleep(5); } catch (InterruptedException e) {}
            }
        }
    }

    // ============================================================
    //  Render thread: 30 Hz, draws cell buffer to SurfaceView
    // ============================================================
    class RenderThread extends Thread {
        private volatile boolean mRunning = true;
        private Paint mPaint;
        private Typeface mTypeface;

        RenderThread() {
            mPaint = new Paint();
            mPaint.setAntiAlias(false);
            mTypeface = Typeface.MONOSPACE;
            mPaint.setTypeface(mTypeface);
        }

        void stopThread() { mRunning = false; }

        @Override
        public void run() {
            long nextRender = System.currentTimeMillis() + 33;

            while (mRunning && nativeIsRunning()) {
                long now = System.currentTimeMillis();

                if (now >= nextRender) {
                    renderFrame();
                    nextRender = now + 33; // ~30fps
                } else {
                    long sleep = nextRender - now;
                    if (sleep > 2) {
                        try { Thread.sleep(sleep * 4 / 5); } catch (InterruptedException e) {}
                    }
                }
            }
            renderFrame(); // final frame
        }

        private void renderFrame() {
            if (mHolder == null) return;

            Canvas canvas = null;
            try {
                canvas = mHolder.lockCanvas();
                if (canvas == null) return;

                int w = canvas.getWidth();
                int h = canvas.getHeight();
                mScreenW = w;
                mScreenH = h;

                // Tell native to render into its buffer
                nativeRender();

                // Get cell data
                byte[] cells = nativeGetCells();
                if (cells == null || cells.length < ROWS * COLS * 3) {
                    canvas.drawColor(Color.BLACK);
                    return;
                }

                // Calculate cell size
                float cellW = (float) w / COLS;
                float cellH = (float) h / ROWS;
                float textSize = Math.min(cellW, cellH) * 0.85f;

                // Clear
                canvas.drawColor(Color.BLACK);

                mPaint.setTextSize(textSize);
                mPaint.setTypeface(mTypeface);

                // Draw each cell
                for (int r = 0; r < ROWS; ++r) {
                    float yPos = r * cellH + cellH * 0.85f;

                    for (int c = 0; c < COLS; ++c) {
                        int idx = (r * COLS + c) * 3;
                        byte ch = cells[idx];
                        byte fg = cells[idx + 1];
                        byte bg = cells[idx + 2];

                        float xPos = c * cellW;

                        // Background
                        if (bg != 0) {
                            mPaint.setColor(VGA_COLORS[bg & 0x0F]);
                            canvas.drawRect(xPos, r * cellH, xPos + cellW, (r + 1) * cellH, mPaint);
                        }

                        // Foreground character
                        if (ch != 0 && ch != ' ') {
                            mPaint.setColor(VGA_COLORS[fg & 0x0F]);
                            char[] charArr = new char[] { (char)(ch & 0xFF) };
                            canvas.drawText(charArr, 0, 1, xPos, yPos, mPaint);
                        }
                    }
                }

            } finally {
                if (canvas != null && mHolder != null) {
                    try {
                        mHolder.unlockCanvasAndPost(canvas);
                    } catch (Exception e) {
                        Log.e(TAG, "unlockCanvasAndPost failed: " + e.getMessage());
                    }
                }
            }
        }
    }
}
