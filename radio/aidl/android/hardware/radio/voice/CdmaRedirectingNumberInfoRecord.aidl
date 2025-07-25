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

package android.hardware.radio.voice;

import android.hardware.radio.voice.CdmaNumberInfoRecord;

/** @hide */
@VintfStability
@JavaDerive(toString=true)
@SuppressWarnings(value={"redundant-name"})
@RustDerive(Clone=true, Eq=true, PartialEq=true)
parcelable CdmaRedirectingNumberInfoRecord {
    /**
     * Redirecting Number Information Record as defined in C.S0005 section 3.7.5.11
     * @deprecated Legacy CDMA is unsupported.
     */
    const int REDIRECTING_REASON_UNKNOWN = 0;
    /** @deprecated Legacy CDMA is unsupported. */
    const int REDIRECTING_REASON_CALL_FORWARDING_BUSY = 1;
    /** @deprecated Legacy CDMA is unsupported. */
    const int REDIRECTING_REASON_CALL_FORWARDING_NO_REPLY = 2;
    /** @deprecated Legacy CDMA is unsupported. */
    const int REDIRECTING_REASON_CALLED_DTE_OUT_OF_ORDER = 9;
    /** @deprecated Legacy CDMA is unsupported. */
    const int REDIRECTING_REASON_CALL_FORWARDING_BY_THE_CALLED_DTE = 10;
    /** @deprecated Legacy CDMA is unsupported. */
    const int REDIRECTING_REASON_CALL_FORWARDING_UNCONDITIONAL = 15;
    /** @deprecated Legacy CDMA is unsupported. */
    const int REDIRECTING_REASON_RESERVED = 16;

    /** @deprecated Legacy CDMA is unsupported. */
    CdmaNumberInfoRecord redirectingNumber;
    /**
     * Set to UNKNOWN if not included.
     * Values are REDIRECTING_REASON_
     * @deprecated Legacy CDMA is unsupported.
     */
    int redirectingReason;
}
