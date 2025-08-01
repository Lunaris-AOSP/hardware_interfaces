/*
 * Copyright (C) 2024 The Android Open Source Project
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

package android.hardware.wifi;

/**
 * Authentication and Key Management types.
 */
@VintfStability
parcelable Akm {
    const long NONE = 0;
    const long PASN = 1 << 0;
    const long SAE = 1 << 1;
    const long FT_EAP_SHA256 = 1 << 2;
    const long FT_PSK_SHA256 = 1 << 3;
    const long FT_EAP_SHA384 = 1 << 4;
    const long FT_PSK_SHA384 = 1 << 5;
    const long FILS_EAP_SHA256 = 1 << 6;
    const long FILS_EAP_SHA384 = 1 << 7;
}
