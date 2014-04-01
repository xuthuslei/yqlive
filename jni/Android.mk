LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := ffmpeg_armv7      # name it whatever
LOCAL_SRC_FILES :=armv7/libffmpeg.a   # or $(so_path)/libthird1.so
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/include
include $(PREBUILT_STATIC_LIBRARY)    #or PREBUILT_SHARED_LIBRARY


include $(CLEAR_VARS)
LOCAL_MODULE := x264_armv7      # name it whatever
LOCAL_SRC_FILES :=armv7/libx264.a    # or $(so_path)/libthird1.so
include $(PREBUILT_STATIC_LIBRARY)    #or PREBUILT_SHARED_LIBRARY

include $(CLEAR_VARS)
LOCAL_MODULE := aac_armv7      # name it whatever
LOCAL_SRC_FILES :=armv7/libfaac.a   # or $(so_path)/libthird1.so
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/include
include $(PREBUILT_STATIC_LIBRARY)    #or PREBUILT_SHARED_LIBRARY

include $(CLEAR_VARS)

LOCAL_MODULE    := recorder
LOCAL_SRC_FILES := recorder.cpp VideoRecorder.cpp
LOCAL_LDLIBS := -llog -pthread -lz
LOCAL_STATIC_LIBRARIES := ffmpeg_armv7 x264_armv7 aac_armv7
LOCAL_CFLAGS += -D__STDC_CONSTANT_MACROS

include $(BUILD_SHARED_LIBRARY)
