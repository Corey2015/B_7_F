LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE_TAGS := optional
#LOCAL_MODULE_TAGS := eng 
LOCAL_MODULE := SensorTest
ifndef ($(TARGET_BUILD_VARIANT),eng)
	LOCAL_SRC_FILES := sensorteset-user-release.apk
else
	LOCAL_SRC_FILES := sensorteset-eng-release.apk
endif
LOCAL_MODULE_CLASS := APPS
LOCAL_MODULE_SUFFIX := $(COMMON_ANDROID_PACKAGE_SUFFIX)
LOCAL_CERTIFICATE := PRESIGNED

include $(BUILD_PREBUILT)
