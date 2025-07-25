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

package android.hardware.radio.ims;

/** @hide */
@VintfStability
@JavaDerive(toString=true)
@Backing(type="int")
@SuppressWarnings(value={"redundant-name"})
enum ImsDeregistrationReason {
    /**
     * Radio shall send this reason to IMS stack to perform graceful de-registration
     * due to SIM card is removed.
     */
    REASON_SIM_REMOVED = 1,
    /**
     * Radio shall send this reason to IMS stack to perform graceful de-registration
     * due to SIM refresh that needs a NAS detach and re-attach.
     */
    REASON_SIM_REFRESH = 2,
    /**
     * Radio shall send this reason to IMS stack to perform graceful de-registration
     * due to allowed network types bitmask changed that results in NAS detach.
     */
    REASON_ALLOWED_NETWORK_TYPES_CHANGED = 3,
}
