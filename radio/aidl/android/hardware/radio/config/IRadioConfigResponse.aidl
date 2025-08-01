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

package android.hardware.radio.config;

import android.hardware.radio.config.PhoneCapability;
import android.hardware.radio.config.SimSlotStatus;
import android.hardware.radio.config.SimTypeInfo;

/**
 * Interface declaring response functions to solicited radio config requests.
 * @hide
 */
@VintfStability
oneway interface IRadioConfigResponse {
    /**
     * @param info Response info struct containing response type, serial number and error
     * @param modemReducedFeatureSet1 True indicates that the modem does NOT support the following
     *        features:
     *        - Providing either LinkCapacityEstimate#secondaryDownlinkCapacityKbps
     *          or LinkCapacityEstimate#secondaryUplinkCapacityKbps when given from
     *          IRadioNetworkIndication#currentLinkCapacityEstimate
     *        - Calling IRadioNetwork#setNrDualConnectivityState or querying
     *          IRadioNetwork#isNrDualConnectivityEnabled
     *        - Requesting IRadioData#setDataThrottling
     *        - Providing SlicingConfig through IRadioData#getSlicingConfig
     *        - Providing PhysicalChannelConfig through
     *          IRadioNetworkIndication#currentPhysicalChannelConfigs
     *
     * Valid errors returned:
     *   RadioError:REQUEST_NOT_SUPPORTED when android.hardware.telephony is not defined
     *   RadioError:NONE
     *   RadioError:RADIO_NOT_AVAILABLE
     *   RadioError:INTERNAL_ERR
     */
    void getHalDeviceCapabilitiesResponse(
            in android.hardware.radio.RadioResponseInfo info, in boolean modemReducedFeatureSet1);

    /**
     * @param info Response info struct containing response type, serial number and error
     * @param numOfLiveModems <byte> indicate the number of live modems i.e. modems that
     *        are enabled and actively working as part of a working connectivity stack
     *
     * Valid errors returned:
     *   RadioError:REQUEST_NOT_SUPPORTED when android.hardware.telephony is not defined
     *   RadioError:NONE
     *   RadioError:RADIO_NOT_AVAILABLE
     */
    void getNumOfLiveModemsResponse(
            in android.hardware.radio.RadioResponseInfo info, in byte numOfLiveModems);

    /**
     * @param info Response info struct containing response type, serial number and error
     * @param phoneCapability <PhoneCapability> it defines the modem's capability. For example,
     *        how many logical modems it has, how many data connections it supports.
     *
     * Valid errors returned:
     *   RadioError:REQUEST_NOT_SUPPORTED when android.hardware.telephony is not defined
     *   RadioError:NONE
     *   RadioError:RADIO_NOT_AVAILABLE
     *   RadioError:INTERNAL_ERR
     */
    void getPhoneCapabilityResponse(
            in android.hardware.radio.RadioResponseInfo info, in PhoneCapability phoneCapability);

    /**
     * @param info Response info struct containing response type, serial number and error
     * @param slotStatus Sim slot struct containing all the physical SIM slots info with size
     *        equal to the number of physical slots on the device
     *
     * Valid errors returned:
     *   RadioError:REQUEST_NOT_SUPPORTED when android.hardware.telephony.subscription is not
     *                                    defined
     *   RadioError:NONE
     *   RadioError:RADIO_NOT_AVAILABLE
     *   RadioError:NO_MEMORY
     *   RadioError:INTERNAL_ERR
     *   RadioError:MODEM_ERR
     */
    void getSimSlotsStatusResponse(
            in android.hardware.radio.RadioResponseInfo info, in SimSlotStatus[] slotStatus);

    /**
     * @param info Response info struct containing response type, serial number and error
     *
     * Valid errors returned:
     *   RadioError:REQUEST_NOT_SUPPORTED when android.hardware.telephony is not defined
     *   RadioError:NONE
     *   RadioError:RADIO_NOT_AVAILABLE
     *   RadioError:INVALID_ARGUMENTS
     */
    void setNumOfLiveModemsResponse(in android.hardware.radio.RadioResponseInfo info);

    /**
     * @param info Response info struct containing response type, serial number and error
     *
     * Valid errors returned:
     *   RadioError:REQUEST_NOT_SUPPORTED when android.hardware.telephony.data is not defined
     *   RadioError:NONE
     *   RadioError:RADIO_NOT_AVAILABLE
     *   RadioError:INTERNAL_ERR
     *   RadioError:INVALID_ARGUMENTS
     */
    void setPreferredDataModemResponse(in android.hardware.radio.RadioResponseInfo info);

    /**
     * @param info Response info struct containing response type, serial number and error
     *
     * Valid errors returned:
     *   RadioError:REQUEST_NOT_SUPPORTED when android.hardware.telephony.subscription is not
     *                                    defined
     *   RadioError:NONE
     *   RadioError:RADIO_NOT_AVAILABLE
     *   RadioError:NO_MEMORY
     *   RadioError:INTERNAL_ERR
     *   RadioError:MODEM_ERR
     *   RadioError:INVALID_ARGUMENTS
     */
    void setSimSlotsMappingResponse(in android.hardware.radio.RadioResponseInfo info);

    /**
     * Response to the asynchronous
     * {@link IRadioConfig#getSimultaneousCallingSupport} request.
     *
     * @param info Response info struct containing response type, serial number and error
     * @param enabledLogicalSlots The slots that have simultaneous cellular calling enabled. If
     * there is a call active on logical slot X, then a simultaneous cellular call is only possible
     * on logical slot Y if BOTH slot X and slot Y are in enabledLogicalSlots. If simultaneous
     * cellular calling is not currently supported, the expected value of enabledLogicalSLots is an
     * empty int array. Sending only one radio slot is not acceptable in any case.
     *
     * Valid errors returned:
     *   RadioError:REQUEST_NOT_SUPPORTED when android.hardware.telephony is not defined
     *   RadioError:NONE
     *   RadioError:RADIO_NOT_AVAILABLE
     *   RadioError:INTERNAL_ERR
     *   RadioError:MODEM_ERR
     *
     * @see IRadioConfig#getSimultaneousCallingSupport for more information.
     */
    void getSimultaneousCallingSupportResponse(
            in android.hardware.radio.RadioResponseInfo info, in int[] enabledLogicalSlots);

    /**
     * Response to the asynchronous {@link IRadioConfig#getSimTypeInfo} request.
     *
     * @param info Response info struct containing response type, serial number and error
     * @param simTypeInfos Currently active and supported sim types associated with active
     * physical slot ids.
     *
     * Valid errors returned:
     *   RadioError:REQUEST_NOT_SUPPORTED when android.hardware.telephony.subscription is not
     *                                    defined
     *   RadioError:NONE
     *   RadioError:RADIO_NOT_AVAILABLE
     *   RadioError:NO_MEMORY
     *   RadioError:INTERNAL_ERR
     *   RadioError:MODEM_ERR
     */
    void getSimTypeInfoResponse(
            in android.hardware.radio.RadioResponseInfo info, in SimTypeInfo[] simTypeInfo);

    /**
     * Response to the asynchronous {@link IRadioConfig#setSimType} request.
     *
     * @param info Response info struct containing response type, serial number and error
     *
     * Valid errors returned:
     *   RadioError:REQUEST_NOT_SUPPORTED when android.hardware.telephony.subscription is not
     *                                    defined
     *   RadioError:NONE
     *   RadioError:RADIO_NOT_AVAILABLE
     *   RadioError:NO_MEMORY
     *   RadioError:INTERNAL_ERR
     *   RadioError:INVALID_ARGUMENTS
     *   RadioError:MODEM_ERR
     */
    void setSimTypeResponse(in android.hardware.radio.RadioResponseInfo info);
}
