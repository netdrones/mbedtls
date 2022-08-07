LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	$(wildcard library/*.c)

LOCAL_EXPORT_C_INCLUDES := \
	$(LOCAL_PATH)/include

LOCAL_MODULE := polarssl_static

include $(BUILD_STATIC_LIBRARY)

# -------------------------------------------
# libpolarssl.so
# -------------------------------------------

include $(CLEAR_VARS)

LOCAL_STATIC_LIBRARIES := polarssl_static

LOCAL_MODULE := polarssl

LOCAL_ARM_MODE := arm

include $(BUILD_SHARED_LIBRARY)