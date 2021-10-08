# SPDX-License-Identifier: GPL-2.0-only
ifeq ($(TARGET_USES_QMAA),false)
ifeq ($(TARGET_USES_QMAA_OVERRIDE_VIDEO),true)
PRODUCT_PACKAGES += msm_video.ko
endif
endif
