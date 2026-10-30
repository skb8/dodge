#
# Copyright (C) 2025 The Android Open Source Project
# Copyright (C) 2025 SebaUbuntu's TWRP device tree generator
#
# SPDX-License-Identifier: Apache-2.0
#
# Copyright (C) 2024 The OrangeFox Recovery Project
# SPDX-License-Identifier: GPL-3.0-or-later
#

LOCAL_PATH := device/oneplus/dodge

# Shipping API level
BOARD_SHIPPING_API_LEVEL := 34
PRODUCT_SHIPPING_API_LEVEL := 34
PRODUCT_TARGET_VNDK_VERSION := 34

# Dynamic partitions
PRODUCT_USE_DYNAMIC_PARTITIONS := true

PRODUCT_PACKAGES += \
    lpflash \
    lpmake \
    lpunpack

# OTA certs
PRODUCT_EXTRA_RECOVERY_KEYS += \
	$(LOCAL_PATH)/security/local_OTA \
	$(LOCAL_PATH)/security/special_OTA

# Soong namespaces
PRODUCT_SOONG_NAMESPACES += $(LOCAL_PATH)

# some OrangeFox-specific settings
$(call inherit-product, $(LOCAL_PATH)/fox_dodge.mk)
#

# Built as a recovery executable by patches/wpa-recovery.patch.
PRODUCT_PACKAGES += wpa_supplicant_dodge

# Optional source-built module closure, staged by scripts/build-oneplus-wifi.sh.
DODGE_WIFI_MODULES := $(LOCAL_PATH)/wifi/modules
ifneq ($(wildcard $(DODGE_WIFI_MODULES)/modules.dep),)
PRODUCT_COPY_FILES += $(call find-copy-subdir-files,*,$(DODGE_WIFI_MODULES),$(TARGET_COPY_OUT_RECOVERY)/root/vendor/lib/dodge-wifi-modules)
endif
