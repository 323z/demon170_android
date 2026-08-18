package com.demon170;

import android.view.Surface;
import javax.microedition.khronos.egl.EGL10;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.egl.EGLContext;
import javax.microedition.khronos.egl.EGLDisplay;
import javax.microedition.khronos.egl.EGLSurface;

public class EGLSetup {
    private EGL10 mEGL;
    private EGLDisplay mDisplay;
    private EGLSurface mSurface;
    private EGLContext mContext;

    public boolean init(Surface surface) {
        mEGL = (EGL10) EGLContext.getEGL();

        mDisplay = mEGL.eglGetDisplay(EGL10.EGL_DEFAULT_DISPLAY);
        if (mDisplay == EGL10.EGL_NO_DISPLAY) return false;

        int[] version = new int[2];
        if (!mEGL.eglInitialize(mDisplay, version)) return false;

        int[] configAttribs = {
            EGL10.EGL_RENDER_TYPE, EGL10.EGL_OPENGL_ES2_BIT,
            EGL10.EGL_RED_SIZE, 8,
            EGL10.EGL_GREEN_SIZE, 8,
            EGL10.EGL_BLUE_SIZE, 8,
            EGL10.EGL_ALPHA_SIZE, 8,
            EGL10.EGL_DEPTH_SIZE, 0,
            EGL10.EGL_STENCIL_SIZE, 0,
            EGL10.EGL_NONE
        };

        EGLConfig[] configs = new EGLConfig[1];
        int[] numConfigs = new int[1];
        mEGL.eglChooseConfig(mDisplay, configAttribs, configs, 1, numConfigs);
        if (numConfigs[0] == 0) return false;
        EGLConfig config = configs[0];

        int[] contextAttribs = { EGL10.EGL_CONTEXT_CLIENT_VERSION, 2, EGL10.EGL_NONE };
        mContext = mEGL.eglCreateContext(mDisplay, config, EGL10.EGL_NO_CONTEXT, contextAttribs);
        if (mContext == EGL10.EGL_NO_CONTEXT) return false;

        mSurface = mEGL.eglCreateWindowSurface(mDisplay, config, surface, null);
        if (mSurface == EGL10.EGL_NO_SURFACE) return false;

        if (!mEGL.eglMakeCurrent(mDisplay, mSurface, mSurface, mContext)) return false;

        return true;
    }

    public void swap() {
        if (mEGL != null && mDisplay != null && mSurface != null) {
            mEGL.eglSwapBuffers(mDisplay, mSurface);
        }
    }

    public void destroy() {
        if (mEGL == null) return;
        mEGL.eglMakeCurrent(mDisplay, EGL10.EGL_NO_SURFACE, EGL10.EGL_NO_SURFACE, EGL10.EGL_NO_CONTEXT);
        if (mSurface != null) mEGL.eglDestroySurface(mDisplay, mSurface);
        if (mContext != null) mEGL.eglDestroyContext(mDisplay, mContext);
        mEGL.eglTerminate(mDisplay);
    }

    // Getters (opaque - actual use is in native via JNI)
    public long getDisplay() { return 0; }
    public long getSurface() { return 0; }
    public long getContext() { return 0; }
}
