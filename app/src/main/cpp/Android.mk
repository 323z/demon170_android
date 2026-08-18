LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := demon170
LOCAL_SRC_FILES := main.cpp
LOCAL_CFLAGS := -std=c++11 -O2 -fexceptions -frtti -DANDROID -D__ANDROID__
LOCAL_LDLIBS := -lGLESv2 -llog
include $(BUILD_SHARED_LIBRARY)
