/*
 * Copyright 2024 The Android Open Source Project
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
package android.hardware.security.see.hwcrypto.types;

/*
 * Implementation defined structure that represents a key and its associated metadata. It is only
 * valid on the current boot, and its reuse after a session is closed (or between sessions) is not
 * guaranteed.
 */
@VintfStability
parcelable OpaqueKeyToken {
    /*
     * Opaque type used to send IOpaqueKeys keys to different clients. Its format is implementation
     * dependant.
     */
    byte[] keyToken;
}
