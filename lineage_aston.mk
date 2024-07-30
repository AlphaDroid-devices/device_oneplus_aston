#
# Copyright (C) 2021-2024 The LineageOS Project
#
# SPDX-License-Identifier: Apache-2.0
#

# Inherit from those products. Most specific first.
$(call inherit-product, $(SRC_TARGET_DIR)/product/core_64_bit.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/full_base_telephony.mk)

# Inherit from aston device
$(call inherit-product, device/oneplus/aston/device.mk)

# Inherit some common Lineage stuff.
$(call inherit-product, vendor/lineage/config/common_full_phone.mk)

PRODUCT_NAME := lineage_aston
PRODUCT_DEVICE := aston
PRODUCT_MANUFACTURER := OnePlus
PRODUCT_BRAND := OnePlus
PRODUCT_MODEL := CPH2585

PRODUCT_SYSTEM_NAME := $(PRODUCT_MODEL)
PRODUCT_SYSTEM_DEVICE := OP5D35L1

PRODUCT_GMS_CLIENTID_BASE := android-oneplus

# Device config
TARGET_HAS_UDFPS := true
TARGET_ENABLE_BLUR := true
TARGET_EXCLUDES_AUDIOFX := true
TARGET_FACE_UNLOCK_SUPPORTED := true

# TARGET_BUILD_PACKAGE options:
# 1 - vanilla (default)
# 2 - microg
# 3 - gapps
TARGET_BUILD_PACKAGE := 1

# TARGET_LAUNCHER options:
# 1 - stock (default)
# 2 - lawnchair
# 3 - pixel (valid only on gapps builds)
TARGET_LAUNCHER := 1

# GAPPS (valid only for GAPPS builds)
TARGET_SUPPORTS_CALL_RECORDING := true
TARGET_INCLUDE_STOCK_ARCORE := false
TARGET_INCLUDE_LIVE_WALLPAPERS := false
# TARGET_SUPPORTS_GOOGLE_RECORDER := true

# Debugging
TARGET_INCLUDE_MATLOG := true

# Maintainer
ALPHA_BUILD_TYPE := Official
ALPHA_MAINTAINER := elpaablo

PRODUCT_BUILD_PROP_OVERRIDES += \
    PRIVATE_BUILD_DESC="CPH2585IN-user 14 TP1A.220905.001 U.R4T3.19135c3-17dcf-a29fb release-keys" \
    TARGET_DEVICE=$(PRODUCT_SYSTEM_DEVICE) \
    TARGET_PRODUCT=$(PRODUCT_SYSTEM_NAME)

BUILD_FINGERPRINT := OnePlus/CPH2585IN/OP5D35L1:14/TP1A.220905.001/U.R4T3.19135c3-17dcf-a29fb:user/release-keys
