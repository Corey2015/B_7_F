# LOCAL_PATH := $(call my-dir)
# #预置aliuyi.apk
# 
# include $(CLEAR_VARS)
# LOCAL_MODULE := FingerprintServiceExtension.apk 
# LOCAL_SRC_FILES := service-release-unsigned.apk
# LOCAL_MODULE_TAGS := optional
# LOCAL_MODULE_CLASS := APPS
# #LOCAL_CERTIFICATE := PRESIGNED
# LOCAL_CERTIFICATE := platform
# #LOCAL_MODULE_PATH := $(TARGET_OUT)/app
# include $(BUILD_PREBUILT)


LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE := FingerprintServiceExtension
LOCAL_SRC_FILES := service-release-unsigned.apk
LOCAL_MODULE_CLASS := APPS
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_SUFFIX := $(COMMON_ANDROID_PACKAGE_SUFFIX)
LOCAL_CERTIFICATE := platform

include $(BUILD_PREBUILT)