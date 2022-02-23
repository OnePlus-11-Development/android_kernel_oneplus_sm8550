
include $(MMRM_ROOT)/config/waipiommrm.conf
LINUXINCLUDE += -include $(MMRM_ROOT)/config/waipiommrmconf.h

ifneq ($(CONFIG_ARCH_QTI_VM), y)

obj-m += driver/
obj-m += test/

ifeq ($(CONFIG_MSM_MMRM_VM),y)
LINUXINCLUDE += -I$(MMRM_ROOT)/vm/common/inc/
obj-m += vm/be/
endif

endif
