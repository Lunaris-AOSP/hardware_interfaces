/*
 * Copyright (C) 2022 The Android Open Source Project
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

package android.hardware.radio.network;

/** @hide */
@VintfStability
@Backing(type="int")
@JavaDerive(toString=true)
@SuppressWarnings(value={"redundant-name"})
enum EmergencyMode {
    /**
     * Mode Type Emergency WWAN, indicates that the current domain selected for the Emergency call
     * is cellular.
     */
    EMERGENCY_WWAN = 1,

    /**
     * Mode Type Emergency WLAN, indicates that the current domain selected for the Emergency call
     * is WLAN/WIFI.
     */
    EMERGENCY_WLAN = 2,

    /**
     * Mode Type Emergency Callback, indicates that the current mode set request is for Emergency
     * callback.
     */
    EMERGENCY_CALLBACK = 3,
}
