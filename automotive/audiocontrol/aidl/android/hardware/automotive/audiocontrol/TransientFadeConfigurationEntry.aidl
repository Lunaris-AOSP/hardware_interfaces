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

package android.hardware.automotive.audiocontrol;

import android.hardware.automotive.audiocontrol.AudioFadeConfiguration;
import android.media.audio.common.AudioUsage;

/**
 * Encapsulates the transient audio fade configuration entry.
 */
@JavaDerive(equals=true, toString=true)
@VintfStability
parcelable TransientFadeConfigurationEntry {
    /**
     * List of audio usages gainers that should be used for this configuration entry.
     */
    AudioUsage[] transientUsages;

    /**
     * Defines the transient fade configuration that should be used for the focus interaction with
     * the usages defined in {@link #transientUsages}
     */
    AudioFadeConfiguration transientFadeConfiguration;
}
