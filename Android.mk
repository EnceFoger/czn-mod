LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
LOCAL_MODULE := czn_speed
LOCAL_SRC_FILES := speed.cpp
LOCAL_LDLIBS := -llog
include $(BUILD_SHARED_LIBRARY)
