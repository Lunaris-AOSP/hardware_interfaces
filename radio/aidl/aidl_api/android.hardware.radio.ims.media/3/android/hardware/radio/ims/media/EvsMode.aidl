/*
 * Copyright (C) 2021 The Android Open Source Project
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

package android.hardware.radio.ims.media;
/* @hide */
@Backing(type="int") @JavaDerive(toString=true) @SuppressWarnings(value={"redundant-name"}) @VintfStability
enum EvsMode {
  INVALID = 0,
  EVS_MODE_0 = (1 << 0) /* 1 */,
  EVS_MODE_1 = (1 << 1) /* 2 */,
  EVS_MODE_2 = (1 << 2) /* 4 */,
  EVS_MODE_3 = (1 << 3) /* 8 */,
  EVS_MODE_4 = (1 << 4) /* 16 */,
  EVS_MODE_5 = (1 << 5) /* 32 */,
  EVS_MODE_6 = (1 << 6) /* 64 */,
  EVS_MODE_7 = (1 << 7) /* 128 */,
  EVS_MODE_8 = (1 << 8) /* 256 */,
  EVS_MODE_9 = (1 << 9) /* 512 */,
  EVS_MODE_10 = (1 << 10) /* 1024 */,
  EVS_MODE_11 = (1 << 11) /* 2048 */,
  EVS_MODE_12 = (1 << 12) /* 4096 */,
  EVS_MODE_13 = (1 << 13) /* 8192 */,
  EVS_MODE_14 = (1 << 14) /* 16384 */,
  EVS_MODE_15 = (1 << 15) /* 32768 */,
  EVS_MODE_16 = (1 << 16) /* 65536 */,
  EVS_MODE_17 = (1 << 17) /* 131072 */,
  EVS_MODE_18 = (1 << 18) /* 262144 */,
  EVS_MODE_19 = (1 << 19) /* 524288 */,
  EVS_MODE_20 = (1 << 20) /* 1048576 */,
}
