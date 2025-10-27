#
# Copyright (C) 2021-2025 The LineageOS Project
#
# SPDX-License-Identifier: Apache-2.0
#

# Partitions
BOARD_SUPER_PARTITION_SIZE := 16642998272

# Include the common OEM chipset BoardConfig.
include device/oneplus/sm8550-common/BoardConfigCommon.mk

DEVICE_PATH := device/oneplus/aston

# Assert
TARGET_OTA_ASSERT_DEVICE := OP5CF9L1,OP5D35L1

# Display
TARGET_SCREEN_DENSITY := 450

ifeq ($(TARGET_USES_PREBUILT_DTB), true)
  BOARD_INCLUDE_DTB_IN_BOOTIMG :=
  BOARD_USES_QCOM_MERGE_DTBS_SCRIPT :=
  TARGET_NEEDS_DTBOIMAGE :=
  TARGET_PREBUILT_DTB := $(DEVICE_PATH)-kernel/dtb.img
  BOARD_MKBOOTIMG_ARGS += --dtb $(TARGET_PREBUILT_DTB)
  BOARD_PREBUILT_DTBOIMAGE := $(DEVICE_PATH)-kernel/dtbo.img
endif

# Kernel
TARGET_KERNEL_ADDITIONAL_FLAGS += CONFIG_ASTON_DTB=y
ifeq ($(TARGET_BUILD_PERMISSIVE),true)
  BOARD_BOOTCONFIG += androidboot.selinux=permissive
endif

# Properties
TARGET_ODM_PROP += $(DEVICE_PATH)/odm.prop
TARGET_SYSTEM_EXT_PROP += $(DEVICE_PATH)/system_ext.prop
TARGET_VENDOR_PROP += $(DEVICE_PATH)/vendor.prop

# Recovery
TARGET_RECOVERY_UI_MARGIN_HEIGHT := 103

# SEPolicy
BOARD_VENDOR_SEPOLICY_DIRS += $(DEVICE_PATH)/sepolicy/vendor

# Include the proprietary files BoardConfig.
include vendor/oneplus/aston/BoardConfigVendor.mk
