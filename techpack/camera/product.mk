CAMERA_DLKM_ENABLED := true
ifeq ($(TARGET_KERNEL_DLKM_DISABLE), true)
	ifeq ($(TARGET_KERNEL_DLKM_CAMERA_OVERRIDE), false)
		CAMERA_DLKM_ENABLED := false;
	endif
endif

ifeq ($(CAMERA_DLKM_ENABLED),true)
PRODUCT_PACKAGES += camera.ko

-include $(TOP)/kernel/oneplus/sm8550/techpack/camera/oplus/explorer/product.mk
endif
