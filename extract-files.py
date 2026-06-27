#!/usr/bin/env -S PYTHONPATH=../../../tools/extract-utils python3
#
# SPDX-FileCopyrightText: 2024 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#

from extract_utils.fixups_blob import (
    blob_fixup,
    blob_fixups_user_type,
)
from extract_utils.fixups_lib import (
    lib_fixups,
    lib_fixups_user_type,
)
from extract_utils.main import (
    ExtractUtils,
    ExtractUtilsModule,
)

namespace_imports = [
    'hardware/oplus',
    'hardware/pixelworks/interfaces',
    'hardware/qcom-caf/sm8550',
    'vendor/oneplus/sm8550-common',
    # odm camera consumers (libEIS, libHIS, ...) are repointed at libui_oplus, which is declared in
    # the vendor/oplus/camera soong namespace -> import it so those shared_libs deps resolve.
    'vendor/oplus/camera',
    'vendor/qcom/opensource/display',
    'vendor/qcom/opensource/commonsys-intf/display',
]


def lib_fixup_vendor_suffix(lib: str, partition: str, *args, **kwargs):
    return f'{lib}_{partition}' if partition == 'vendor' else None


lib_fixups: lib_fixups_user_type = {
    **lib_fixups,
    (
        'libhwconfigurationutil',
        'vendor.oplus.hardware.cammidasservice-V1-ndk',
    ): lib_fixup_vendor_suffix,
}

blob_fixups: blob_fixups_user_type = {
    'odm/etc/camera/CameraHWConfiguration.config': blob_fixup()
        .regex_replace('SystemCamera =  0;  0;  0;  1;  0;  1;', 'SystemCamera =  0;  0;  0;  0;  0;  0;'),
    (
        'odm/etc/libnfc-mtp-SN220.conf_23801',
        'odm/etc/libnfc-mtp-SN220.conf_23861'
    ): blob_fixup()
        .regex_replace('(NXPLOG_.*_LOGLEVEL)=0x03', '\\1=0x02')
        .regex_replace('NFC_DEBUG_ENABLED=1', 'NFC_DEBUG_ENABLED=0'),
    'odm/lib64/libAlgoProcess.so': blob_fixup()
        .replace_needed('android.hardware.graphics.common-V3-ndk.so', 'android.hardware.graphics.common-V7-ndk.so')
        # Pull our GOT-interposer into com.oplus.camera so it can clamp the P010 LSB->MSB
        # over-walk (APSFormatConverter::p010LSB2MSB) that SIGSEGVs the algo CapThread.
        .add_needed('libapsfixup.so'),
    # OOS camera stack expects the small-ABI ColorOS libui (sizeof(GraphicBuffer)==264); AOSP-16's
    # platform libui is larger, so its GraphicBuffer ctor overran libEIS's 264-byte alloc ->
    # heap-header corruption -> the hold-to-record SIGSEGV. The small libui is shipped SEPARATELY
    # as libui_oplus.so by vendor/oplus/camera (renamed so it cannot shadow the platform libui for
    # the whole vendor partition via the /odm/${LIB} search path -> that caused a boot loop).
    # Repoint these odm camera consumers at the isolated libui_oplus.so.
    (
        'odm/lib64/libEIS.so',
        'odm/lib64/libHIS.so',
        'odm/lib64/libsharebuffer_impl.so',
        'odm/lib64/hw/camera.oemlayer.so',
        'odm/lib64/camera/components/com.oplus.node.sstabphoto.so',
    ): blob_fixup()
        .replace_needed('libui.so', 'libui_oplus.so'),
    (
        'odm/lib64/libCOppLceTonemapAPI.so',
        'odm/lib64/libSuperRaw.so',
        'odm/lib64/libYTCommon.so',
        'odm/lib64/libyuv2.so'
    ): blob_fixup()
        .replace_needed('libstdc++.so', 'libstdc++_vendor.so'),
    (
        'odm/lib64/libEISLive.so',
        'odm/lib64/libHIS.so',
        'odm/lib64/libOGLManager.so',
        'odm/lib64/libOPAlgoCamFaceBeautyCap.so'
    ): blob_fixup()
        .clear_symbol_version('AHardwareBuffer_allocate')
        .clear_symbol_version('AHardwareBuffer_describe')
        .clear_symbol_version('AHardwareBuffer_lock')
        .clear_symbol_version('AHardwareBuffer_release')
        .clear_symbol_version('AHardwareBuffer_unlock'),
    'odm/lib64/libarcsoft_high_dynamic_range_v4.so': blob_fixup()
        .clear_symbol_version('remote_handle_close')
        .clear_symbol_version('remote_handle_invoke')
        .clear_symbol_version('remote_handle_open')
        .clear_symbol_version('remote_register_buf_attr')
        .clear_symbol_version('remote_register_buf'),
    'odm/lib64/libextensionlayer.so': blob_fixup()
        .replace_needed('libziparchive.so', 'libziparchive_odm.so'),
    (
        'vendor/bin/hw/vendor.qti.camera.provider-service_64',
        'vendor/lib64/camx.provider-impl.so',
    ): blob_fixup()
        .replace_needed('libtinyxml2.so', 'libtinyxml2-v34.so'),
    # dodge fixup parity: AOSP-16 ships AIDL graphics.allocator-V2-ndk; OOS-built CHI/Feature2
    # libs may DT_NEEDED the V1-ndk variant. patchelf --replace-needed is a no-op when the
    # source DT_NEEDED isn't present, so this is safe even though the current 12R blobs use
    # the HIDL allocator@4.0 path. Kept for robustness against blob revisions and to match the
    # proven dodge configuration.
    (
        'vendor/lib64/camera/components/com.qti.node.dewarp.so',
        'vendor/lib64/hw/com.qti.chi.override.so',
        'vendor/lib64/libcamximageformatutils.so',
        'vendor/lib64/libchifeature2.so',
    ): blob_fixup()
        .replace_needed('android.hardware.graphics.allocator-V1-ndk.so', 'android.hardware.graphics.allocator-V2-ndk.so'),
    'vendor/etc/libnfc-nci.conf': blob_fixup()
        .regex_replace('NFC_DEBUG_ENABLED=1', 'NFC_DEBUG_ENABLED=0')
}  # fmt: skip

module = ExtractUtilsModule(
    'aston',
    'oneplus',
    namespace_imports=namespace_imports,
    blob_fixups=blob_fixups,
    lib_fixups=lib_fixups,
    add_firmware_proprietary_file=True,
)

if __name__ == '__main__':
    utils = ExtractUtils.device(module)
    utils.run()
