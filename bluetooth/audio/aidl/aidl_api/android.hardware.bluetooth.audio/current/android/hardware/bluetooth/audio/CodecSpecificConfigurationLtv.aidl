/*
 * Copyright 2023 The Android Open Source Project
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
///////////////////////////////////////////////////////////////////////////////
// THIS FILE IS IMMUTABLE. DO NOT EDIT IN ANY CASE.                          //
///////////////////////////////////////////////////////////////////////////////

// This file is a snapshot of an AIDL file. Do not edit it manually. There are
// two cases:
// 1). this is a frozen version file - do not edit this in any case.
// 2). this is a 'current' file. If you make a backwards compatible change to
//     the interface (from the latest frozen version), the build system will
//     prompt you to update this file with `m <name>-update-api`.
//
// You must not make a backward incompatible change to any AIDL file built
// with the aidl_interface module type with versions property set. The module
// type is used to build AIDL files in a way that they can be used across
// independently updatable components of the system. If a device is shipped
// with such a backward incompatible change, it has a high risk of breaking
// later when a module using the interface is updated, e.g., Mainline modules.

package android.hardware.bluetooth.audio;
@VintfStability
union CodecSpecificConfigurationLtv {
  android.hardware.bluetooth.audio.CodecSpecificConfigurationLtv.CodecFrameBlocksPerSDU codecFrameBlocksPerSDU;
  android.hardware.bluetooth.audio.CodecSpecificConfigurationLtv.SamplingFrequency samplingFrequency;
  android.hardware.bluetooth.audio.CodecSpecificConfigurationLtv.FrameDuration frameDuration;
  android.hardware.bluetooth.audio.CodecSpecificConfigurationLtv.AudioChannelAllocation audioChannelAllocation;
  android.hardware.bluetooth.audio.CodecSpecificConfigurationLtv.OctetsPerCodecFrame octetsPerCodecFrame;
  @Backing(type="byte")
  enum SamplingFrequency {
    HZ8000 = 0x01,
    HZ11025 = 0x02,
    HZ16000 = 0x03,
    HZ22050 = 0x04,
    HZ24000 = 0x05,
    HZ32000 = 0x06,
    HZ44100 = 0x07,
    HZ48000 = 0x08,
    HZ88200 = 0x09,
    HZ96000 = 0x0A,
    HZ176400 = 0x0B,
    HZ192000 = 0x0C,
    HZ384000 = 0x0D,
  }
  @Backing(type="byte")
  enum FrameDuration {
    US7500 = 0x00,
    US10000 = 0x01,
    US20000 = 0x02,
  }
  parcelable AudioChannelAllocation {
    int bitmask;
    const int NOT_ALLOWED = 0x00000000;
    const int FRONT_LEFT = 0x00000001;
    const int FRONT_RIGHT = 0x00000002;
    const int FRONT_CENTER = 0x00000004;
    const int LOW_FREQUENCY_EFFECTS_1 = 0x00000008;
    const int BACK_LEFT = 0x00000010;
    const int BACK_RIGHT = 0x00000020;
    const int FRONT_LEFT_OF_CENTER = 0x00000040;
    const int FRONT_RIGHT_OF_CENTER = 0x00000080;
    const int BACK_CENTER = 0x00000100;
    const int LOW_FREQUENCY_EFFECTS_2 = 0x00000200;
    const int SIDE_LEFT = 0x00000400;
    const int SIDE_RIGHT = 0x00000800;
    const int TOP_FRONT_LEFT = 0x00001000;
    const int TOP_FRONT_RIGHT = 0x00002000;
    const int TOP_FRONT_CENTER = 0x00004000;
    const int TOP_CENTER = 0x00008000;
    const int TOP_BACK_LEFT = 0x00010000;
    const int TOP_BACK_RIGHT = 0x00020000;
    const int TOP_SIDE_LEFT = 0x00040000;
    const int TOP_SIDE_RIGHT = 0x00080000;
    const int TOP_BACK_CENTER = 0x00100000;
    const int BOTTOM_FRONT_CENTER = 0x00200000;
    const int BOTTOM_FRONT_LEFT = 0x00400000;
    const int BOTTOM_FRONT_RIGHT = 0x00800000;
    const int FRONT_LEFT_WIDE = 0x01000000;
    const int FRONT_RIGHT_WIDE = 0x02000000;
    const int LEFT_SURROUND = 0x04000000;
    const int RIGHT_SURROUND = 0x08000000;
    const int MONO = 0x00000000;
  }
  parcelable OctetsPerCodecFrame {
    int value;
  }
  parcelable CodecFrameBlocksPerSDU {
    int value;
  }
}
