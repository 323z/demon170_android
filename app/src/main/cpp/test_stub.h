// test_stub.h - Minimal stubs for desktop testing
// When __TEST__ is defined, Android/JNI headers are not available.
// This file provides just enough types to compile the C++ core standalone.

#ifndef DEMON170_TEST_STUB_H
#define DEMON170_TEST_STUB_H

#include <stdint.h>

// JNI type stubs
typedef int32_t  jint;
typedef int64_t  jlong;
typedef double   jdouble;
typedef float    jfloat;
typedef int16_t  jshort;
typedef int8_t   jbyte;
typedef int32_t  jboolean;
typedef void*    jobject;
typedef void*    jclass;
typedef void*    JNIEnv;

// JNI calling convention macro - no-op on desktop
#ifndef JNICALL
#define JNICALL
#endif

// Android log stubs
#define ANDROID_LOG_INFO    0
#define ANDROID_LOG_ERROR   1

// EGL/GLES type stubs (not used in test mode)
typedef void* EGLDisplay;
typedef void* EGLSurface;
typedef void* EGLContext;
typedef void* EGLConfig;

#define EGL10_EGL_NO_DISPLAY  ((EGLDisplay)0)
#define EGL10_EGL_NO_CONTEXT  ((EGLContext)0)
#define EGL10_EGL_NO_SURFACE ((EGLSurface)0)
#define EGL10_EGL_NONE        0x3038
#define EGL10_EGL_RED_SIZE    0x3024
#define EGL10_EGL_GREEN_SIZE  0x3023
#define EGL10_EGL_BLUE_SIZE   0x3022
#define EGL10_EGL_ALPHA_SIZE  0x3021
#define EGL10_EGL_DEPTH_SIZE  0x3025
#define EGL10_EGL_STENCIL_SIZE 0x3026
#define EGL10_EGL_RENDER_TYPE 0x3040
#define EGL10_EGL_OPENGL_ES2_BIT 0x0004
#define EGL10_EGL_CONTEXT_CLIENT_VERSION 0x3098

#endif // DEMON170_TEST_STUB_H
