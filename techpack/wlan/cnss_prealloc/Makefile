# SPDX-License-Identifier: GPL-2.0-only

WLAN_PLATFORM_ROOT := $(srctree)/techpack/wlan

ifeq ($(CONFIG_ARCH_KALAMA), y)
	include $(WLAN_PLATFORM_ROOT)/config/kalama.conf
endif

ifeq ($(CONFIG_CNSS_OUT_OF_TREE),y)
ccflags-y += -I$(WLAN_PLATFORM_ROOT)/inc
endif

obj-$(CONFIG_WCNSS_MEM_PRE_ALLOC) += cnss_prealloc.o
