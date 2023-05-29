# SPDX-License-Identifier: GPL-2.0-only

SSG_MODULE_ROOT := $(srctree)/techpack/securemsm
WLAN_PLATFORM_ROOT := $(srctree)/techpack/wlan

ifeq ($(CONFIG_ARCH_KALAMA), y)
	include $(WLAN_PLATFORM_ROOT)/config/kalama.conf
endif

ifeq ($(CONFIG_CNSS_OUT_OF_TREE),y)
ccflags-y += -I$(WLAN_PLATFORM_ROOT)/cnss_utils
ccflags-y += -I$(WLAN_PLATFORM_ROOT)/inc
else
ccflags-y += -I$(srctree)/drivers/net/wireless/cnss_utils
endif

ifeq ($(CONFIG_CNSS_HW_SECURE_DISABLE), y)
ccflags-y += -I$(SSG_MODULE_ROOT)/smcinvoke/
ccflags-y += -I$(SSG_MODULE_ROOT)/linux/
endif

obj-$(CONFIG_CNSS2) += cnss2.o

cnss2-y := main.o
cnss2-y += bus.o
cnss2-y += debug.o
cnss2-y += pci.o
cnss2-y += power.o
cnss2-y += genl.o
cnss2-$(CONFIG_PCI_MSM) += pci_qcom.o
cnss2-$(CONFIG_CNSS2_QMI) += qmi.o coexistence_service_v01.o ip_multimedia_subsystem_private_service_v01.o
