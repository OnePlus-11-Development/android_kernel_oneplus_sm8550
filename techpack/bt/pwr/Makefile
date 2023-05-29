BT_ROOT := $(srctree)/techpack/bt
SSG_MODULE_ROOT := $(srctree)/techpack/securemsm
WLAN_PLATFORM_ROOT := $(srctree)/techpack/wlan

ifeq ($(CONFIG_ARCH_KALAMA), y)
	include $(BT_ROOT)/config/kalama.conf
endif

ccflags-y += -I$(BT_ROOT)/include
ccflags-y += -I$(WLAN_PLATFORM_ROOT)/inc
ccflags-y += -I$(WLAN_PLATFORM_ROOT)/cnss_utils

ifeq ($(CONFIG_BT_HW_SECURE_DISABLE), y)
ccflags-y += -I$(SSG_MODULE_ROOT)/smcinvoke/
ccflags-y += -I$(SSG_MODULE_ROOT)/linux/
endif

obj-$(CONFIG_MSM_BT_POWER)  += btpower.o
