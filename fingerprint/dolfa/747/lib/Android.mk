LOCAL_PATH := $(call my-dir)

#ifeq (yes,$(strip $(HCT_SUPPORT_FINGERPRINT_ET310)))

include $(CLEAR_VARS)
LOCAL_MODULE := fingerprint.default
ifeq ($(strip $(TARGET_ARCH)), arm64)
LOCAL_SRC_FILES := lib64/fingerprint.default.so
else
LOCAL_SRC_FILES := lib32/fingerprint.default.so
endif
LOCAL_MODULE_CLASS := SHARED_LIBRARIES
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw
LOCAL_MODULE_SUFFIX := .so
include $(BUILD_PREBUILT)



include $(CLEAR_VARS)
LOCAL_MODULE := libcom_fingerprints_service
ifeq ($(strip $(TARGET_ARCH)), arm64)
LOCAL_SRC_FILES := lib64/libcom_fingerprints_service.so
else
LOCAL_SRC_FILES := lib32/libcom_fingerprints_service.so
endif
LOCAL_MODULE_CLASS := SHARED_LIBRARIES
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)
LOCAL_MODULE_SUFFIX := .so
include $(BUILD_PREBUILT)

include $(call all-makefiles-under,$(LOCAL_PATH)) 
#endif
