/*
 * Copyright 2021 The Android Open Source Project
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

package android.hardware.bluetooth.audio;

import android.hardware.bluetooth.audio.AptxAdaptiveLeCapabilities;
import android.hardware.bluetooth.audio.AudioLocation;
import android.hardware.bluetooth.audio.CodecSpecificConfigurationLtv.AudioChannelAllocation;
import android.hardware.bluetooth.audio.CodecType;
import android.hardware.bluetooth.audio.Lc3Capabilities;
import android.hardware.bluetooth.audio.OpusCapabilities;

/**
 * Used to specify the le audio unicast codec capabilities for hardware offload.
 */
@VintfStability
parcelable UnicastCapability {
    @VintfStability
    parcelable VendorCapabilities {
        ParcelableHolder extension;
    }
    @VintfStability
    union LeAudioCodecCapabilities {
        Lc3Capabilities lc3Capabilities;
        VendorCapabilities vendorCapabillities;
        AptxAdaptiveLeCapabilities aptxAdaptiveLeCapabilities;
        OpusCapabilities opusCapabilities;
    }
    CodecType codecType;
    // @deprecated use audioLocation if present.
    AudioLocation supportedChannel;
    // The number of connected device
    int deviceCount;
    // Supported channel count for each device
    int channelCountPerDevice;
    LeAudioCodecCapabilities leAudioCodecCapabilities;
    // The new audio location type, replacing supportedChannel
    @nullable AudioChannelAllocation audioLocation;
}
