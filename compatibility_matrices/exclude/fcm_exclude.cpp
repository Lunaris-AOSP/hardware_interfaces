/*
 * Copyright (C) 2020 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <functional>
#include <string>
#include <vector>

#include <android-base/strings.h>
#include <vintf/fcm_exclude.h>

namespace android::vintf::details {

// The predicate to VintfObject::checkMissingHalsInMatrices.
bool ShouldCheckMissingHidlHalsInFcm(const std::string& packageAndVersion) {
    static std::vector<std::string> included_prefixes{
            // Other AOSP HALs (e.g. android.frameworks.*) are not added because only framework
            // matrix is checked.
            "android.hardware.",
    };

    static std::vector<std::string> excluded_prefixes{
            // Packages without top level interfaces (including types-only packages) are exempted.
            "android.hardware.camera.device@",
            "android.hardware.gnss.measurement_corrections@1.",
            "android.hardware.graphics.bufferqueue@",

            // Test packages are exempted.
            "android.hardware.tests.",
    };

    static std::vector<std::string> excluded_exact{
            // Packages without top level interfaces (including types-only packages) are exempted.
            // HIDL
            "android.hardware.cas.native@1.0",
            "android.hardware.gnss.visibility_control@1.0",
            "android.hardware.media.bufferpool@1.0",
            "android.hardware.media.bufferpool@2.0",
            "android.hardware.radio.config@1.2",

            // Fastboot HAL is only used by recovery. Recovery is owned by OEM. Framework
            // does not depend on this HAL, hence it is not declared in any manifests or matrices.
            "android.hardware.fastboot@1.0",
            "android.hardware.fastboot@1.1",

            // Deprecated HALs.
            // HIDL
            // TODO(b/171260360) Remove when HAL definition is removed
            "android.hardware.audio.effect@2.0",
            "android.hardware.audio@2.0",
            // Health 1.0 HAL is deprecated. The top level interface are deleted.
            "android.hardware.health@1.0",
            // TODO(b/171260670) Remove when HAL definition is removed
            "android.hardware.nfc@1.0",
            // TODO(b/171260715) Remove when HAL definition is removed
            "android.hardware.radio.deprecated@1.0",

            // TODO(b/205175891): File individual bugs for these HALs deprecated in P
            "android.hardware.audio.effect@4.0",
            "android.hardware.audio@4.0",
            "android.hardware.bluetooth.a2dp@1.0",
            "android.hardware.cas@1.0",
            "android.hardware.configstore@1.0",
            "android.hardware.gnss@1.0",
            "android.hardware.gnss@1.1",
            "android.hardware.graphics.mapper@2.0",
            "android.hardware.nfc@1.1",
            "android.hardware.radio.config@1.0",
            "android.hardware.radio@1.0",
            "android.hardware.radio@1.1",
            "android.hardware.radio@1.3",
            "android.hardware.thermal@1.0",
            "android.hardware.thermal@1.1",
            "android.hardware.wifi.offload@1.0",

            // b/279809679 for HALS deprecated in Q
            "android.hardware.audio.effect@5.0",
            "android.hardware.audio@5.0",
            "android.hardware.boot@1.0",
            "android.hardware.configstore@1.1",
            "android.hardware.drm@1.0",
            "android.hardware.drm@1.1",
            "android.hardware.drm@1.2",
            "android.hardware.dumpstate@1.0",
            "android.hardware.health@2.0",
            "android.hardware.light@2.0",
            "android.hardware.power@1.0",
            "android.hardware.power@1.1",
            "android.hardware.power@1.2",
            "android.hardware.power@1.3",
            "android.hardware.vibrator@1.0",
            "android.hardware.vibrator@1.1",
            "android.hardware.vibrator@1.2",
            "android.hardware.vibrator@1.3",

            // b/392700935 for HALs deprecated in R
            "android.hardware.automotive.audiocontrol@1.0",
            "android.hardware.automotive.audiocontrol@2.0",
            "android.hardware.boot@1.1",
            "android.hardware.contexthub@1.0",
            "android.hardware.contexthub@1.1",
            "android.hardware.health.storage@1.0",
            "android.hardware.memtrack@1.0",
            "android.hardware.power.stats@1.0",
            "android.hardware.radio@1.4",
            "android.hardware.radio@1.5",
            "android.hardware.soundtrigger@2.0",
            "android.hardware.soundtrigger@2.1",
            "android.hardware.soundtrigger@2.2",
            "android.hardware.tetheroffload.control@1.0",
            "android.hardware.vr@1.0",
            "android.hardware.wifi.supplicant@1.0",
            "android.hardware.wifi.supplicant@1.1",
            "android.hardware.wifi@1.0",
            "android.hardware.wifi@1.1",
            "android.hardware.wifi@1.2",
    };

    auto package_has_prefix = [&](const std::string& prefix) {
        return android::base::StartsWith(packageAndVersion, prefix);
    };

    // Only check packageAndVersions that are in the include list and not in the exclude list.
    if (!std::any_of(included_prefixes.begin(), included_prefixes.end(), package_has_prefix)) {
        return false;
    }

    if (std::find(excluded_exact.begin(), excluded_exact.end(), packageAndVersion) !=
        excluded_exact.end()) {
        return false;
    }

    return !std::any_of(excluded_prefixes.begin(), excluded_prefixes.end(), package_has_prefix);
}

// The predicate to VintfObject::checkMissingHalsInMatrices.
bool ShouldCheckMissingAidlHalsInFcm(const std::string& packageAndVersion) {
    static std::vector<std::string> included_prefixes{
            // Other AOSP HALs (e.g. android.frameworks.*) are not added because only framework
            // matrix is checked.
            "android.hardware.",
    };

    static std::vector<std::string> excluded_prefixes{
            // Packages without top level interfaces (including types-only packages) are exempted.
            "android.hardware.audio.common@",
            "android.hardware.biometrics.common@",
            "android.hardware.camera.metadata@",
            "android.hardware.camera.device@",
            "android.hardware.camera.common@",
            "android.hardware.common@",
            "android.hardware.common.fmq@",
            "android.hardware.gnss.gnss_assistance@",
            "android.hardware.gnss.measurement_corrections@",
            "android.hardware.gnss.visibility_control@",
            "android.hardware.graphics.common@",
            "android.hardware.input.common@",
            "android.hardware.keymaster@",
            "android.hardware.media.bufferpool2@",
            "android.hardware.radio@",
            "android.hardware.uwb.fira_android@",
            "android.hardware.wifi.common@",
            "android.hardware.biometrics.fingerprint.virtualhal@",

            // Test packages are exempted.
            "android.hardware.tests.",

            // Fastboot HAL is only used by recovery. Recovery is owned by OEM. Framework
            // does not depend on this HAL, hence it is not declared in any manifests or matrices.
            "android.hardware.fastboot@",
            "android.hardware.security.see.hwcrypto.types",
            "android.hardware.security.see.storage",
    };

    static std::vector<std::string> excluded_exact{
            // Packages without top level interfaces (including types-only packages) are exempted.

            // AIDL
            "android.hardware.audio.core.sounddose@1",
            "android.hardware.audio.core.sounddose@2",
            "android.hardware.audio.core.sounddose@3",
            // This is only used by a trusty VM
            "android.hardware.security.see.authmgr@1",
            "android.hardware.security.see.hdcp@1",

            // Deprecated HALs.
            "android.hardware.audio.sounddose@3",
            "android.hardware.bluetooth.audio@1",
    };

    auto package_has_prefix = [&](const std::string& prefix) {
        return android::base::StartsWith(packageAndVersion, prefix);
    };

    // Only check packageAndVersions that are in the include list and not in the exclude list.
    if (!std::any_of(included_prefixes.begin(), included_prefixes.end(), package_has_prefix)) {
        return false;
    }

    if (std::find(excluded_exact.begin(), excluded_exact.end(), packageAndVersion) !=
        excluded_exact.end()) {
        return false;
    }

    return !std::any_of(excluded_prefixes.begin(), excluded_prefixes.end(), package_has_prefix);
}

}  // namespace android::vintf::details
