#
# Copyright (C) 2021-2023 The LineageOS Project
#
# SPDX-License-Identifier: Apache-2.0
#

# Include the common OEM chipset BoardConfig.
include device/oneplus/sm8550-common/BoardConfigCommon.mk

DEVICE_PATH := device/oneplus/aston

# Assert
TARGET_OTA_ASSERT_DEVICE := OP5D35L1,OP5CF9L1

# Display
TARGET_SCREEN_DENSITY := 420

# Kernel
TARGET_KERNEL_CONFIG += vendor/oplus/aston.config

ifeq ($(TARGET_BUILD_PERMISSIVE),true)
  BOARD_BOOTCONFIG += androidboot.selinux=permissive
endif

ifeq ($(TARGET_USES_PREBUILT_DTB), true)
  BOARD_INCLUDE_DTB_IN_BOOTIMG :=
  BOARD_USES_QCOM_MERGE_DTBS_SCRIPT :=
  TARGET_NEEDS_DTBOIMAGE :=
  TARGET_PREBUILT_DTB := $(DEVICE_PATH)-kernel/dtb.img
  BOARD_MKBOOTIMG_ARGS += --dtb $(TARGET_PREBUILT_DTB)
  BOARD_PREBUILT_DTBOIMAGE := $(DEVICE_PATH)-kernel/dtbo.img
endif

# Kernel modules
BOARD_SYSTEM_KERNEL_MODULES_LOAD := $(strip $(shell cat $(DEVICE_PATH)/modules.load.system_dlkm))
BOARD_VENDOR_KERNEL_MODULES_BLOCKLIST_FILE := $(DEVICE_PATH)/modules.blocklist
BOARD_VENDOR_KERNEL_MODULES_LOAD := $(strip $(shell cat $(DEVICE_PATH)/modules.load))
BOARD_VENDOR_RAMDISK_KERNEL_MODULES_BLOCKLIST_FILE := $(BOARD_VENDOR_KERNEL_MODULES_BLOCKLIST_FILE)
BOARD_VENDOR_RAMDISK_KERNEL_MODULES_LOAD := $(strip $(shell cat $(DEVICE_PATH)/modules.load.vendor_boot))
BOARD_VENDOR_RAMDISK_RECOVERY_KERNEL_MODULES_LOAD := $(strip $(shell cat $(DEVICE_PATH)/modules.load.recovery))
BOOT_KERNEL_MODULES := $(strip $(shell cat $(DEVICE_PATH)/modules.load.recovery $(DEVICE_PATH)/modules.include.vendor_ramdisk))
SYSTEM_KERNEL_MODULES := $(strip $(shell cat $(DEVICE_PATH)/modules.include.system_dlkm))

# Partitions
BOARD_ONEPLUS_DYNAMIC_PARTITIONS_SIZE := 16638803968
BOARD_SUPER_PARTITION_SIZE := 16642998272

# Power
TARGET_POWERHAL_MODE_EXT := $(DEVICE_PATH)/power/power-mode.cpp
BOARD_POWER_CUSTOM_BOARD_LIB += \
    vendor.oplus.hardware.touch-V1-ndk

# Properties
TARGET_VENDOR_PROP += $(DEVICE_PATH)/vendor.prop

# Recovery
TARGET_RECOVERY_UI_MARGIN_HEIGHT := 103

# SEPolicy
BOARD_VENDOR_SEPOLICY_DIRS += device/oneplus/aston/sepolicy/vendor
BOARD_SYSTEM_EXT_SEPOLICY_DIRS += device/oneplus/aston/sepolicy/system_ext

# Include the proprietary files BoardConfig.
include vendor/oneplus/aston/BoardConfigVendor.mk
