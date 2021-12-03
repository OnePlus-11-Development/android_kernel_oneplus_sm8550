# SPDX-License-Identifier: GPL-2.0-only
TARGET_VIDC_ENABLE := false
ifeq ($(TARGET_USES_QMAA),false)
TARGET_VIDC_ENABLE := true
endif
ifeq ($(TARGET_USES_QMAA_OVERRIDE_VIDEO),true)
TARGET_VIDC_ENABLE := true
endif

ifeq ($(TARGET_VIDC_ENABLE),true)
PRODUCT_PACKAGES += msm_video.ko
endif
