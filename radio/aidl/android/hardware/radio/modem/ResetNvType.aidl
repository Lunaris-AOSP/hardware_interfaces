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

package android.hardware.radio.modem;

/**
 * @hide
 */
@VintfStability
@Backing(type="int")
@JavaDerive(toString=true)
@SuppressWarnings(value={"redundant-name"})
enum ResetNvType {
    /**
     * Reboot modem.
     *
     * Historically, this has been also reloading all NV items.
     */
    RELOAD,
    /**
     * Erase NV reset (SCRTN)
     *
     * @deprecated NV APIs are deprecated starting from Android U.
     */
    ERASE,
    /**
     * Factory reset (RTN)
     *
     * @deprecated NV APIs are deprecated starting from Android U.
     */
    FACTORY_RESET,
}
