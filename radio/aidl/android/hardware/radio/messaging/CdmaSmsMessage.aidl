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

package android.hardware.radio.messaging;

import android.hardware.radio.messaging.CdmaSmsAddress;
import android.hardware.radio.messaging.CdmaSmsSubaddress;

/** @hide */
@VintfStability
@JavaDerive(toString=true)
@RustDerive(Clone=true, Eq=true, PartialEq=true)
parcelable CdmaSmsMessage {
    /** @deprecated Legacy CDMA is unsupported. */
    int teleserviceId;
    /** @deprecated Legacy CDMA is unsupported. */
    boolean isServicePresent;
    /** @deprecated Legacy CDMA is unsupported. */
    int serviceCategory;
    /** @deprecated Legacy CDMA is unsupported. */
    CdmaSmsAddress address;
    /** @deprecated Legacy CDMA is unsupported. */
    CdmaSmsSubaddress subAddress;
    /**
     * 3GPP2 C.S0015-B, v2.0
     * @deprecated Legacy CDMA is unsupported.
     */
    byte[] bearerData;
}
