#Android makefile to build kernel as a part of Android Build
PERL		= perl

TARGET_KERNEL_ARCH := $(strip $(TARGET_KERNEL_ARCH))
ifeq ($(TARGET_KERNEL_ARCH),)
KERNEL_ARCH := arm
else
KERNEL_ARCH := $(TARGET_KERNEL_ARCH)
endif

TARGET_KERNEL_HEADER_ARCH := $(strip $(TARGET_KERNEL_HEADER_ARCH))
ifeq ($(TARGET_KERNEL_HEADER_ARCH),)
KERNEL_HEADER_ARCH := $(KERNEL_ARCH)
else
$(warning Forcing kernel header generation only for '$(TARGET_KERNEL_HEADER_ARCH)')
KERNEL_HEADER_ARCH := $(TARGET_KERNEL_HEADER_ARCH)
endif

KERNEL_HEADER_DEFCONFIG := $(strip $(KERNEL_HEADER_DEFCONFIG))
ifeq ($(KERNEL_HEADER_DEFCONFIG),)
KERNEL_HEADER_DEFCONFIG := $(KERNEL_DEFCONFIG)
endif

TARGET_KERNEL_CROSS_COMPILE_PREFIX := $(strip $(TARGET_KERNEL_CROSS_COMPILE_PREFIX))
ifeq ($(TARGET_KERNEL_CROSS_COMPILE_PREFIX),)
KERNEL_CROSS_COMPILE := arm-eabi-
else
KERNEL_CROSS_COMPILE := $(TARGET_KERNEL_CROSS_COMPILE_PREFIX)
endif

ifeq ($(TARGET_PREBUILT_KERNEL),)

KERNEL_OUT := $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ
KERNEL_OUT_ABS := $(abspath $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ)
KERNEL_CONFIG := $(KERNEL_OUT)/.config

ifeq ($(TARGET_USES_UNCOMPRESSED_KERNEL),true)
$(info Using uncompressed kernel)
TARGET_PREBUILT_INT_KERNEL := $(KERNEL_OUT)/arch/$(KERNEL_ARCH)/boot/Image
else
TARGET_PREBUILT_INT_KERNEL := $(KERNEL_OUT)/arch/$(KERNEL_ARCH)/boot/zImage
endif

ifeq ($(TARGET_KERNEL_APPEND_DTB), true)
$(info Using appended DTB)
TARGET_PREBUILT_INT_KERNEL := $(TARGET_PREBUILT_INT_KERNEL)-dtb
endif

KERNEL_HEADERS_INSTALL := $(KERNEL_OUT)/usr
KERNEL_MODULES_INSTALL := system
KERNEL_MODULES_OUT := $(TARGET_OUT)/lib/modules
KERNEL_IMG=$(KERNEL_OUT)/arch/arm/boot/Image

DTS_NAMES ?= $(shell $(PERL) -e 'while (<>) {$$a = $$1 if /CONFIG_ARCH_((?:MSM|QSD|MPQ)[a-zA-Z0-9]+)=y/; $$r = $$1 if /CONFIG_MSM_SOC_REV_(?!NONE)(\w+)=y/; $$arch = $$arch.lc("$$a$$r ") if /CONFIG_ARCH_((?:MSM|QSD|MPQ)[a-zA-Z0-9]+)=y/} print $$arch;' $(KERNEL_CONFIG))
KERNEL_USE_OF ?= $(shell $(PERL) -e '$$of = "n"; while (<>) { if (/CONFIG_USE_OF=y/) { $$of = "y"; break; } } print $$of;' $(KERNEL_PATH)/arch/arm/configs/$(KERNEL_DEFCONFIG))

ifeq ($(KERNEL_DEFCONFIG),apollo_defconfig)
DTS_NAMES := apollo
endif

ifeq ($(KERNEL_DEFCONFIG),apollo-perf_defconfig)
DTS_NAMES := apollo
endif

ifeq ($(KERNEL_DEFCONFIG),thor_defconfig)
DTS_NAMES := thor
endif

ifeq ($(KERNEL_DEFCONFIG),thor-perf_defconfig)
DTS_NAMES := thor
endif

# Compile all ursa* device-trees
ifeq ($(KERNEL_DEFCONFIG),ursa_defconfig)
DTS_NAMES := ursa
endif

ifeq ($(KERNEL_DEFCONFIG),ursa-perf_defconfig)
DTS_NAMES := ursa
endif

ifeq "$(KERNEL_USE_OF)" "y"
DTS_FILES = $(wildcard $(TOP)/$(KERNEL_PATH)/arch/arm/boot/dts/$(DTS_NAME)*.dts)
DTS_FILE = $(lastword $(subst /, ,$(1)))
DTB_FILE = $(addprefix $(KERNEL_OUT)/arch/arm/boot/,$(patsubst %.dts,%.dtb,$(call DTS_FILE,$(1))))
ZIMG_FILE = $(addprefix $(KERNEL_OUT)/arch/arm/boot/,$(patsubst %.dts,%-zImage,$(call DTS_FILE,$(1))))
KERNEL_ZIMG = $(KERNEL_OUT)/arch/arm/boot/zImage
DTC = $(KERNEL_OUT)/scripts/dtc/dtc

# Run the following section only for Apollo, Thor, Kodiak(Ursa) targets
ifneq (,$(filter apollo thor %kodiak,$(TARGET_PRODUCT)))

define append-dtb
set -e;\
mkdir -p $(KERNEL_OUT)/arch/arm/boot;\
rm -f $(KERNEL_OUT)/arch/arm/boot/*.dtb;\
rm -f $(KERNEL_OUT)/arch/arm/boot/*.dtb_dts;\
$(foreach DTS_NAME, $(DTS_NAMES), \
   $(foreach d, $(DTS_FILES), \
      $(DTC) -p 2048 -O dtb -o $(call DTB_FILE,$(d)) $(d); \
      $(DTC) -p 2048 -O dts -o $(call DTB_FILE,$(d))_dts $(d); \
      cat $(KERNEL_ZIMG) $(call DTB_FILE,$(d)) > $(call ZIMG_FILE,$(d));)) \
set +e
endef

else
# else not Apollo or Thor or Kodiak device
define append-dtb
set -e;\
mkdir -p $(KERNEL_OUT)/arch/arm/boot;\
$(foreach DTS_NAME, $(DTS_NAMES), \
   $(foreach d, $(DTS_FILES), \
      $(DTC) -p 1024 -O dtb -o $(call DTB_FILE,$(d)) $(d); \
      cat $(KERNEL_ZIMG) $(call DTB_FILE,$(d)) > $(call ZIMG_FILE,$(d));)) \
set +e
endef

endif # If Apollo, thor 

else 
define append-dtb
endef

endif # KERNEL_USE_OF

TARGET_PREBUILT_KERNEL := $(TARGET_PREBUILT_INT_KERNEL)

define mv-modules
mdpath=`find $(KERNEL_MODULES_OUT) -type f -name modules.dep`;\
if [ "$$mdpath" != "" ];then\
mpath=`dirname $$mdpath`;\
ko=`find $$mpath/$$(KERNEL_PATH) -type f -name *.ko`;\
for i in $$ko; do mv $$i $(KERNEL_MODULES_OUT)/; done;\
fi
endef

define clean-module-folder
mdpath=`find $(KERNEL_MODULES_OUT) -type f -name modules.dep`;\
if [ "$$mdpath" != "" ];then\
mpath=`dirname $$mdpath`; rm -rf $$mpath;\
fi
endef

$(KERNEL_OUT):
	mkdir -p $(KERNEL_OUT)

$(KERNEL_CONFIG): $(KERNEL_OUT) | $(KERNEL_OUT)/include/generated/trapz_generated_kernel.h
	mkdir -p $(KERNEL_OUT)
	$(MAKE) -C $(KERNEL_PATH) O=$(KERNEL_OUT_ABS) ARCH=$(KERNEL_ARCH) CROSS_COMPILE=$(KERNEL_CROSS_COMPILE) $(KERNEL_DEFCONFIG)
	if [ -e $(KERNEL_PATH)/arch/arm/configs/trapz.config ]; then cat $(KERNEL_PATH)/arch/arm/configs/trapz.config >> $@ ;\
	$(MAKE) -C $(KERNEL_PATH) O=$(KERNEL_OUT_ABS) ARCH=$(KERNEL_ARCH) CROSS_COMPILE=$(KERNEL_CROSS_COMPILE) oldconfig; fi
	if [ $(USE_HAVOK) = true ]; then $(KERNEL_PATH)/scripts/config --file $@ --enable CONFIG_HAVOK; fi

$(TARGET_PREBUILT_INT_KERNEL): $(KERNEL_OUT) $(KERNEL_HEADERS_INSTALL)
	$(hide) rm -rf $(KERNEL_OUT)/arch/$(KERNEL_ARCH)/boot/dts
	$(MAKE) -C $(KERNEL_PATH) O=$(KERNEL_OUT_ABS) ARCH=$(KERNEL_ARCH) CROSS_COMPILE=$(KERNEL_CROSS_COMPILE)
	$(MAKE) -C $(KERNEL_PATH) O=$(KERNEL_OUT_ABS) ARCH=$(KERNEL_ARCH) CROSS_COMPILE=$(KERNEL_CROSS_COMPILE) modules
	$(MAKE) -C $(KERNEL_PATH) O=$(KERNEL_OUT_ABS) INSTALL_MOD_PATH=../../$(KERNEL_MODULES_INSTALL) INSTALL_MOD_STRIP=1 ARCH=$(KERNEL_ARCH) CROSS_COMPILE=$(KERNEL_CROSS_COMPILE) modules_install
	$(mv-modules)
	$(clean-module-folder)
	$(append-dtb)

$(KERNEL_HEADERS_INSTALL): $(KERNEL_OUT)
	$(hide) rm -f $(TOP)/$(KERNEL_CONFIG)
	$(MAKE) -C $(KERNEL_PATH) O=$(KERNEL_OUT_ABS) ARCH=$(KERNEL_HEADER_ARCH) CROSS_COMPILE=$(KERNEL_CROSS_COMPILE) $(KERNEL_HEADER_DEFCONFIG)
	$(MAKE) -C $(KERNEL_PATH) O=$(KERNEL_OUT_ABS) ARCH=$(KERNEL_HEADER_ARCH) CROSS_COMPILE=$(KERNEL_CROSS_COMPILE) headers_install
	$(hide) rm -f $(TOP)/$(KERNEL_CONFIG)
	$(MAKE) -C $(KERNEL_PATH) O=$(KERNEL_OUT_ABS) ARCH=$(KERNEL_ARCH) CROSS_COMPILE=$(KERNEL_CROSS_COMPILE) $(KERNEL_DEFCONFIG)
	if [ -e $(KERNEL_PATH)/arch/arm/configs/trapz.config ]; then cat $(KERNEL_PATH)/arch/arm/configs/trapz.config >> $(KERNEL_CONFIG) ;\
	$(MAKE) -C $(KERNEL_PATH) O=$(KERNEL_OUT_ABS) ARCH=$(KERNEL_ARCH) CROSS_COMPILE=$(KERNEL_CROSS_COMPILE) oldconfig; fi
	if [ $(USE_HAVOK) = true ]; then $(KERNEL_PATH)/scripts/config --file $(KERNEL_CONFIG) --enable CONFIG_HAVOK; fi

kerneltags: $(KERNEL_OUT) $(KERNEL_CONFIG)
	$(MAKE) -C $(KERNEL_PATH) O=$(KERNEL_OUT_ABS) ARCH=$(KERNEL_ARCH) CROSS_COMPILE=$(KERNEL_CROSS_COMPILE) tags

kernelconfig: $(KERNEL_OUT) $(KERNEL_CONFIG)
	env KCONFIG_NOTIMESTAMP=true \
	     $(MAKE) -C $(KERNEL_PATH) O=$(KERNEL_OUT_ABS) ARCH=$(KERNEL_ARCH) CROSS_COMPILE=$(KERNEL_CROSS_COMPILE) menuconfig
	env KCONFIG_NOTIMESTAMP=true \
	     $(MAKE) -C $(KERNEL_PATH) O=$(KERNEL_OUT_ABS) ARCH=$(KERNEL_ARCH) CROSS_COMPILE=$(KERNEL_CROSS_COMPILE) savedefconfig
	cp $(KERNEL_OUT)/defconfig $(KERNEL_PATH)/arch/$(KERNEL_ARCH)/configs/$(KERNEL_DEFCONFIG)

.PHONY: kernel-menuconfig kernel-defconfig kernel-savedefconfig

kernel-menuconfig: kernelconfig

kernel-defconfig: $(KERNEL_CONFIG)

kernel-savedefconfig: | $(KERNEL_OUT) $(ACP)
	$(hide) env KCONFIG_NOTIMESTAMP=true \
	     $(MAKE) -C $(KERNEL_PATH) O=$(KERNEL_OUT_ABS) ARCH=$(KERNEL_ARCH) CROSS_COMPILE=$(KERNEL_CROSS_COMPILE) savedefconfig
	$(hide) $(ACP) $(KERNEL_OUT)/defconfig $(KERNEL_PATH)/arch/$(KERNEL_ARCH)/configs/$(KERNEL_DEFCONFIG)

endif
