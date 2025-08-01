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

package android.hardware.contexthub;
@VintfStability
interface IContextHub {
  List<android.hardware.contexthub.ContextHubInfo> getContextHubs();
  void loadNanoapp(in int contextHubId, in android.hardware.contexthub.NanoappBinary appBinary, in int transactionId);
  void unloadNanoapp(in int contextHubId, in long appId, in int transactionId);
  void disableNanoapp(in int contextHubId, in long appId, in int transactionId);
  void enableNanoapp(in int contextHubId, in long appId, in int transactionId);
  void onSettingChanged(in android.hardware.contexthub.Setting setting, in boolean enabled);
  void queryNanoapps(in int contextHubId);
  void registerCallback(in int contextHubId, in android.hardware.contexthub.IContextHubCallback cb);
  void sendMessageToHub(in int contextHubId, in android.hardware.contexthub.ContextHubMessage message);
  void onHostEndpointConnected(in android.hardware.contexthub.HostEndpointInfo hostEndpointInfo);
  void onHostEndpointDisconnected(char hostEndpointId);
  long[] getPreloadedNanoappIds(in int contextHubId);
  void onNanSessionStateChanged(in android.hardware.contexthub.NanSessionStateUpdate update);
  void setTestMode(in boolean enable);
  void sendMessageDeliveryStatusToHub(in int contextHubId, in android.hardware.contexthub.MessageDeliveryStatus messageDeliveryStatus);
  List<android.hardware.contexthub.HubInfo> getHubs();
  List<android.hardware.contexthub.EndpointInfo> getEndpoints();
  @PropagateAllowBlocking android.hardware.contexthub.IEndpointCommunication registerEndpointHub(in android.hardware.contexthub.IEndpointCallback callback, in android.hardware.contexthub.HubInfo hubInfo);
  const int EX_CONTEXT_HUB_UNSPECIFIED = (-1) /* -1 */;
}
