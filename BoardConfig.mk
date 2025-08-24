#
# Copyright (C) 2021-2025 The LineageOS Project
#
# SPDX-License-Identifier: Apache-2.0
#

# Partitions
BOARD_SUPER_PARTITION_SIZE := 16940199936

# Include the common OEM chipset BoardConfig.
include device/oneplus/sm8550-common/BoardConfigCommon.mk

DEVICE_PATH := device/oneplus/aston

# Assert
TARGET_OTA_ASSERT_DEVICE := OP5D35L1,OP5CF9L1

# Display
TARGET_SCREEN_DENSITY := 420

# Kernel
TARGET_KERNEL_ADDITIONAL_FLAGS += CONFIG_ASTON_DTB=y

ifeq ($(TARGET_BUILD_PERMISSIVE),true)
  BOARD_BOOTCONFIG += androidboot.selinux=permissive
endif

# Partitions
BOARD_ONEPLUS_DYNAMIC_PARTITIONS_SIZE := 16638803968
BOARD_SUPER_PARTITION_SIZE := 16642998272

# Properties
TARGET_ODM_PROP += $(DEVICE_PATH)/odm.prop
TARGET_SYSTEM_EXT_PROP += $(DEVICE_PATH)/system_ext.prop
TARGET_VENDOR_PROP += $(DEVICE_PATH)/vendor.prop

# Recovery
TARGET_RECOVERY_UI_MARGIN_HEIGHT := 103

# Include the proprietary files BoardConfig.
include vendor/oneplus/aston/BoardConfigVendor.mk
