# Test dlkm
DLKM_DIR   := device/qcom/common/dlkm
KGSL_SELECT := CONFIG_QCOM_KGSL=m
KERN_SRC := $(ANDROID_TOP)/kernel_platform/msm-kernel

LOCAL_PATH := $(call my-dir)

KBUILD_OPTIONS += BOARD_PLATFORM=$(TARGET_BOARD_PLATFORM)
KBUILD_OPTIONS += $(KGSL_SELECT)
KBUILD_OPTIONS += MODNAME=msm_kgsl
KBUILD_OPTIONS += KERN_SRC=$(KERN_SRC)

KBUILD_OPTIONS += KBUILD_EXTRA_SYMBOLS=$(PWD)/$(call intermediates-dir-for,DLKM,mmrm-module-symvers)/Module.symvers

include $(CLEAR_VARS)
# For incremental compilation
LOCAL_SRC_FILES   := $(wildcard $(LOCAL_PATH)/**/*) $(wildcard $(LOCAL_PATH)/*)
LOCAL_MODULE      := msm_kgsl.ko
LOCAL_MODULE_KBUILD_NAME  := msm_kgsl.ko
LOCAL_MODULE_TAGS         := optional
LOCAL_MODULE_DEBUG_ENABLE := true
LOCAL_MODULE_PATH := $(KERNEL_MODULES_OUT)
#LOCAL_REQUIRED_MODULES    := mmrm-module-symvers
#LOCAL_ADDITIONAL_DEPENDENCIES := $(call intermediates-dir-for,DLKM,mmrm-module-symvers)/Module.symvers

# Include msm_kgsl.ko in the /vendor/lib/modules (vendor.img)
BOARD_VENDOR_KERNEL_MODULES += $(LOCAL_MODULE_PATH)/$(LOCAL_MODULE)
include $(DLKM_DIR)/Build_external_kernelmodule.mk

