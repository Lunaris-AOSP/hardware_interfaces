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

package android.hardware.radio.data;
/* @hide */
@Backing(type="int") @JavaDerive(toString=true) @SuppressWarnings(value={"redundant-name"}) @VintfStability
enum DataCallFailCause {
  NONE = 0,
  OPERATOR_BARRED = 0x08,
  NAS_SIGNALLING = 0x0E,
  INSUFFICIENT_RESOURCES = 0x1A,
  MISSING_UNKNOWN_APN = 0x1B,
  UNKNOWN_PDP_ADDRESS_TYPE = 0x1C,
  USER_AUTHENTICATION = 0x1D,
  ACTIVATION_REJECT_GGSN = 0x1E,
  ACTIVATION_REJECT_UNSPECIFIED = 0x1F,
  SERVICE_OPTION_NOT_SUPPORTED = 0x20,
  SERVICE_OPTION_NOT_SUBSCRIBED = 0x21,
  SERVICE_OPTION_OUT_OF_ORDER = 0x22,
  NSAPI_IN_USE = 0x23,
  REGULAR_DEACTIVATION = 0x24,
  QOS_NOT_ACCEPTED = 0x25,
  NETWORK_FAILURE = 0x26,
  UMTS_REACTIVATION_REQ = 0x27,
  FEATURE_NOT_SUPP = 0x28,
  TFT_SEMANTIC_ERROR = 0x29,
  TFT_SYTAX_ERROR = 0x2A,
  UNKNOWN_PDP_CONTEXT = 0x2B,
  FILTER_SEMANTIC_ERROR = 0x2C,
  FILTER_SYTAX_ERROR = 0x2D,
  PDP_WITHOUT_ACTIVE_TFT = 0x2E,
  ONLY_IPV4_ALLOWED = 0x32,
  ONLY_IPV6_ALLOWED = 0x33,
  ONLY_SINGLE_BEARER_ALLOWED = 0x34,
  ESM_INFO_NOT_RECEIVED = 0x35,
  PDN_CONN_DOES_NOT_EXIST = 0x36,
  MULTI_CONN_TO_SAME_PDN_NOT_ALLOWED = 0x37,
  MAX_ACTIVE_PDP_CONTEXT_REACHED = 0x41,
  UNSUPPORTED_APN_IN_CURRENT_PLMN = 0x42,
  INVALID_TRANSACTION_ID = 0x51,
  MESSAGE_INCORRECT_SEMANTIC = 0x5F,
  INVALID_MANDATORY_INFO = 0x60,
  MESSAGE_TYPE_UNSUPPORTED = 0x61,
  MSG_TYPE_NONCOMPATIBLE_STATE = 0x62,
  UNKNOWN_INFO_ELEMENT = 0x63,
  CONDITIONAL_IE_ERROR = 0x64,
  MSG_AND_PROTOCOL_STATE_UNCOMPATIBLE = 0x65,
  PROTOCOL_ERRORS = 0x6F,
  APN_TYPE_CONFLICT = 0x70,
  INVALID_PCSCF_ADDR = 0x71,
  INTERNAL_CALL_PREEMPT_BY_HIGH_PRIO_APN = 0x72,
  EMM_ACCESS_BARRED = 0x73,
  EMERGENCY_IFACE_ONLY = 0x74,
  IFACE_MISMATCH = 0x75,
  COMPANION_IFACE_IN_USE = 0x76,
  IP_ADDRESS_MISMATCH = 0x77,
  IFACE_AND_POL_FAMILY_MISMATCH = 0x78,
  EMM_ACCESS_BARRED_INFINITE_RETRY = 0x79,
  AUTH_FAILURE_ON_EMERGENCY_CALL = 0x7A,
  OEM_DCFAILCAUSE_1 = 0x1001,
  OEM_DCFAILCAUSE_2 = 0x1002,
  OEM_DCFAILCAUSE_3 = 0x1003,
  OEM_DCFAILCAUSE_4 = 0x1004,
  OEM_DCFAILCAUSE_5 = 0x1005,
  OEM_DCFAILCAUSE_6 = 0x1006,
  OEM_DCFAILCAUSE_7 = 0x1007,
  OEM_DCFAILCAUSE_8 = 0x1008,
  OEM_DCFAILCAUSE_9 = 0x1009,
  OEM_DCFAILCAUSE_10 = 0x100A,
  OEM_DCFAILCAUSE_11 = 0x100B,
  OEM_DCFAILCAUSE_12 = 0x100C,
  OEM_DCFAILCAUSE_13 = 0x100D,
  OEM_DCFAILCAUSE_14 = 0x100E,
  OEM_DCFAILCAUSE_15 = 0x100F,
  VOICE_REGISTRATION_FAIL = (-1) /* -1 */,
  DATA_REGISTRATION_FAIL = (-2) /* -2 */,
  SIGNAL_LOST = (-3) /* -3 */,
  PREF_RADIO_TECH_CHANGED = (-4) /* -4 */,
  RADIO_POWER_OFF = (-5) /* -5 */,
  TETHERED_CALL_ACTIVE = (-6) /* -6 */,
  ERROR_UNSPECIFIED = 0xffff,
  LLC_SNDCP = 0x19,
  ACTIVATION_REJECTED_BCM_VIOLATION = 0x30,
  COLLISION_WITH_NETWORK_INITIATED_REQUEST = 0x38,
  ONLY_IPV4V6_ALLOWED = 0x39,
  ONLY_NON_IP_ALLOWED = 0x3A,
  UNSUPPORTED_QCI_VALUE = 0x3B,
  BEARER_HANDLING_NOT_SUPPORTED = 0x3C,
  INVALID_DNS_ADDR = 0x7B,
  INVALID_PCSCF_OR_DNS_ADDRESS = 0x7C,
  CALL_PREEMPT_BY_EMERGENCY_APN = 0x7F,
  UE_INITIATED_DETACH_OR_DISCONNECT = 0x80,
  MIP_FA_REASON_UNSPECIFIED = 0x7D0,
  MIP_FA_ADMIN_PROHIBITED = 0x7D1,
  MIP_FA_INSUFFICIENT_RESOURCES = 0x7D2,
  MIP_FA_MOBILE_NODE_AUTHENTICATION_FAILURE = 0x7D3,
  MIP_FA_HOME_AGENT_AUTHENTICATION_FAILURE = 0x7D4,
  MIP_FA_REQUESTED_LIFETIME_TOO_LONG = 0x7D5,
  MIP_FA_MALFORMED_REQUEST = 0x7D6,
  MIP_FA_MALFORMED_REPLY = 0x7D7,
  MIP_FA_ENCAPSULATION_UNAVAILABLE = 0x7D8,
  MIP_FA_VJ_HEADER_COMPRESSION_UNAVAILABLE = 0x7D9,
  MIP_FA_REVERSE_TUNNEL_UNAVAILABLE = 0x7DA,
  MIP_FA_REVERSE_TUNNEL_IS_MANDATORY = 0x7DB,
  MIP_FA_DELIVERY_STYLE_NOT_SUPPORTED = 0x7DC,
  MIP_FA_MISSING_NAI = 0x7DD,
  MIP_FA_MISSING_HOME_AGENT = 0x7DE,
  MIP_FA_MISSING_HOME_ADDRESS = 0x7DF,
  MIP_FA_UNKNOWN_CHALLENGE = 0x7E0,
  MIP_FA_MISSING_CHALLENGE = 0x7E1,
  MIP_FA_STALE_CHALLENGE = 0x7E2,
  MIP_HA_REASON_UNSPECIFIED = 0x7E3,
  MIP_HA_ADMIN_PROHIBITED = 0x7E4,
  MIP_HA_INSUFFICIENT_RESOURCES = 0x7E5,
  MIP_HA_MOBILE_NODE_AUTHENTICATION_FAILURE = 0x7E6,
  MIP_HA_FOREIGN_AGENT_AUTHENTICATION_FAILURE = 0x7E7,
  MIP_HA_REGISTRATION_ID_MISMATCH = 0x7E8,
  MIP_HA_MALFORMED_REQUEST = 0x7E9,
  MIP_HA_UNKNOWN_HOME_AGENT_ADDRESS = 0x7EA,
  MIP_HA_REVERSE_TUNNEL_UNAVAILABLE = 0x7EB,
  MIP_HA_REVERSE_TUNNEL_IS_MANDATORY = 0x7EC,
  MIP_HA_ENCAPSULATION_UNAVAILABLE = 0x7ED,
  CLOSE_IN_PROGRESS = 0x7EE,
  NETWORK_INITIATED_TERMINATION = 0x7EF,
  MODEM_APP_PREEMPTED = 0x7F0,
  PDN_IPV4_CALL_DISALLOWED = 0x7F1,
  PDN_IPV4_CALL_THROTTLED = 0x7F2,
  PDN_IPV6_CALL_DISALLOWED = 0x7F3,
  PDN_IPV6_CALL_THROTTLED = 0x7F4,
  MODEM_RESTART = 0x7F5,
  PDP_PPP_NOT_SUPPORTED = 0x7F6,
  UNPREFERRED_RAT = 0x7F7,
  PHYSICAL_LINK_CLOSE_IN_PROGRESS = 0x7F8,
  APN_PENDING_HANDOVER = 0x7F9,
  PROFILE_BEARER_INCOMPATIBLE = 0x7FA,
  SIM_CARD_CHANGED = 0x7FB,
  LOW_POWER_MODE_OR_POWERING_DOWN = 0x7FC,
  APN_DISABLED = 0x7FD,
  MAX_PPP_INACTIVITY_TIMER_EXPIRED = 0x7FE,
  IPV6_ADDRESS_TRANSFER_FAILED = 0x7FF,
  TRAT_SWAP_FAILED = 0x800,
  /**
   * @deprecated Legacy CDMA is unsupported.
   */
  EHRPD_TO_HRPD_FALLBACK = 0x801,
  MIP_CONFIG_FAILURE = 0x802,
  PDN_INACTIVITY_TIMER_EXPIRED = 0x803,
  MAX_IPV4_CONNECTIONS = 0x804,
  MAX_IPV6_CONNECTIONS = 0x805,
  APN_MISMATCH = 0x806,
  IP_VERSION_MISMATCH = 0x807,
  DUN_CALL_DISALLOWED = 0x808,
  INTERNAL_EPC_NONEPC_TRANSITION = 0x809,
  INTERFACE_IN_USE = 0x80A,
  APN_DISALLOWED_ON_ROAMING = 0x80B,
  APN_PARAMETERS_CHANGED = 0x80C,
  NULL_APN_DISALLOWED = 0x80D,
  THERMAL_MITIGATION = 0x80E,
  DATA_SETTINGS_DISABLED = 0x80F,
  DATA_ROAMING_SETTINGS_DISABLED = 0x810,
  DDS_SWITCHED = 0x811,
  FORBIDDEN_APN_NAME = 0x812,
  DDS_SWITCH_IN_PROGRESS = 0x813,
  CALL_DISALLOWED_IN_ROAMING = 0x814,
  NON_IP_NOT_SUPPORTED = 0x815,
  PDN_NON_IP_CALL_THROTTLED = 0x816,
  PDN_NON_IP_CALL_DISALLOWED = 0x817,
  /**
   * @deprecated Legacy CDMA is unsupported.
   */
  CDMA_LOCK = 0x818,
  /**
   * @deprecated Legacy CDMA is unsupported.
   */
  CDMA_INTERCEPT = 0x819,
  /**
   * @deprecated Legacy CDMA is unsupported.
   */
  CDMA_REORDER = 0x81A,
  /**
   * @deprecated Legacy CDMA is unsupported.
   */
  CDMA_RELEASE_DUE_TO_SO_REJECTION = 0x81B,
  /**
   * @deprecated Legacy CDMA is unsupported.
   */
  CDMA_INCOMING_CALL = 0x81C,
  /**
   * @deprecated Legacy CDMA is unsupported.
   */
  CDMA_ALERT_STOP = 0x81D,
  CHANNEL_ACQUISITION_FAILURE = 0x81E,
  MAX_ACCESS_PROBE = 0x81F,
  CONCURRENT_SERVICE_NOT_SUPPORTED_BY_BASE_STATION = 0x820,
  NO_RESPONSE_FROM_BASE_STATION = 0x821,
  REJECTED_BY_BASE_STATION = 0x822,
  CONCURRENT_SERVICES_INCOMPATIBLE = 0x823,
  /**
   * @deprecated Legacy CDMA is unsupported.
   */
  NO_CDMA_SERVICE = 0x824,
  RUIM_NOT_PRESENT = 0x825,
  /**
   * @deprecated Legacy CDMA is unsupported.
   */
  CDMA_RETRY_ORDER = 0x826,
  ACCESS_BLOCK = 0x827,
  ACCESS_BLOCK_ALL = 0x828,
  IS707B_MAX_ACCESS_PROBES = 0x829,
  THERMAL_EMERGENCY = 0x82A,
  CONCURRENT_SERVICES_NOT_ALLOWED = 0x82B,
  INCOMING_CALL_REJECTED = 0x82C,
  NO_SERVICE_ON_GATEWAY = 0x82D,
  NO_GPRS_CONTEXT = 0x82E,
  ILLEGAL_MS = 0x82F,
  ILLEGAL_ME = 0x830,
  GPRS_SERVICES_AND_NON_GPRS_SERVICES_NOT_ALLOWED = 0x831,
  GPRS_SERVICES_NOT_ALLOWED = 0x832,
  MS_IDENTITY_CANNOT_BE_DERIVED_BY_THE_NETWORK = 0x833,
  IMPLICITLY_DETACHED = 0x834,
  PLMN_NOT_ALLOWED = 0x835,
  LOCATION_AREA_NOT_ALLOWED = 0x836,
  GPRS_SERVICES_NOT_ALLOWED_IN_THIS_PLMN = 0x837,
  PDP_DUPLICATE = 0x838,
  UE_RAT_CHANGE = 0x839,
  CONGESTION = 0x83A,
  NO_PDP_CONTEXT_ACTIVATED = 0x83B,
  ACCESS_CLASS_DSAC_REJECTION = 0x83C,
  PDP_ACTIVATE_MAX_RETRY_FAILED = 0x83D,
  RADIO_ACCESS_BEARER_FAILURE = 0x83E,
  ESM_UNKNOWN_EPS_BEARER_CONTEXT = 0x83F,
  DRB_RELEASED_BY_RRC = 0x840,
  CONNECTION_RELEASED = 0x841,
  EMM_DETACHED = 0x842,
  EMM_ATTACH_FAILED = 0x843,
  EMM_ATTACH_STARTED = 0x844,
  LTE_NAS_SERVICE_REQUEST_FAILED = 0x845,
  DUPLICATE_BEARER_ID = 0x846,
  ESM_COLLISION_SCENARIOS = 0x847,
  ESM_BEARER_DEACTIVATED_TO_SYNC_WITH_NETWORK = 0x848,
  ESM_NW_ACTIVATED_DED_BEARER_WITH_ID_OF_DEF_BEARER = 0x849,
  ESM_BAD_OTA_MESSAGE = 0x84A,
  ESM_DOWNLOAD_SERVER_REJECTED_THE_CALL = 0x84B,
  ESM_CONTEXT_TRANSFERRED_DUE_TO_IRAT = 0x84C,
  DS_EXPLICIT_DEACTIVATION = 0x84D,
  ESM_LOCAL_CAUSE_NONE = 0x84E,
  LTE_THROTTLING_NOT_REQUIRED = 0x84F,
  ACCESS_CONTROL_LIST_CHECK_FAILURE = 0x850,
  SERVICE_NOT_ALLOWED_ON_PLMN = 0x851,
  EMM_T3417_EXPIRED = 0x852,
  EMM_T3417_EXT_EXPIRED = 0x853,
  RRC_UPLINK_DATA_TRANSMISSION_FAILURE = 0x854,
  RRC_UPLINK_DELIVERY_FAILED_DUE_TO_HANDOVER = 0x855,
  RRC_UPLINK_CONNECTION_RELEASE = 0x856,
  RRC_UPLINK_RADIO_LINK_FAILURE = 0x857,
  RRC_UPLINK_ERROR_REQUEST_FROM_NAS = 0x858,
  RRC_CONNECTION_ACCESS_STRATUM_FAILURE = 0x859,
  RRC_CONNECTION_ANOTHER_PROCEDURE_IN_PROGRESS = 0x85A,
  RRC_CONNECTION_ACCESS_BARRED = 0x85B,
  RRC_CONNECTION_CELL_RESELECTION = 0x85C,
  RRC_CONNECTION_CONFIG_FAILURE = 0x85D,
  RRC_CONNECTION_TIMER_EXPIRED = 0x85E,
  RRC_CONNECTION_LINK_FAILURE = 0x85F,
  RRC_CONNECTION_CELL_NOT_CAMPED = 0x860,
  RRC_CONNECTION_SYSTEM_INTERVAL_FAILURE = 0x861,
  RRC_CONNECTION_REJECT_BY_NETWORK = 0x862,
  RRC_CONNECTION_NORMAL_RELEASE = 0x863,
  RRC_CONNECTION_RADIO_LINK_FAILURE = 0x864,
  RRC_CONNECTION_REESTABLISHMENT_FAILURE = 0x865,
  RRC_CONNECTION_OUT_OF_SERVICE_DURING_CELL_REGISTER = 0x866,
  RRC_CONNECTION_ABORT_REQUEST = 0x867,
  RRC_CONNECTION_SYSTEM_INFORMATION_BLOCK_READ_ERROR = 0x868,
  NETWORK_INITIATED_DETACH_WITH_AUTO_REATTACH = 0x869,
  NETWORK_INITIATED_DETACH_NO_AUTO_REATTACH = 0x86A,
  ESM_PROCEDURE_TIME_OUT = 0x86B,
  INVALID_CONNECTION_ID = 0x86C,
  MAXIMIUM_NSAPIS_EXCEEDED = 0x86D,
  INVALID_PRIMARY_NSAPI = 0x86E,
  CANNOT_ENCODE_OTA_MESSAGE = 0x86F,
  RADIO_ACCESS_BEARER_SETUP_FAILURE = 0x870,
  PDP_ESTABLISH_TIMEOUT_EXPIRED = 0x871,
  PDP_MODIFY_TIMEOUT_EXPIRED = 0x872,
  PDP_INACTIVE_TIMEOUT_EXPIRED = 0x873,
  PDP_LOWERLAYER_ERROR = 0x874,
  PDP_MODIFY_COLLISION = 0x875,
  /**
   * @deprecated use MAXIMUM_SIZE_OF_L2_MESSAGE_EXCEEDED instead.
   */
  MAXINUM_SIZE_OF_L2_MESSAGE_EXCEEDED = 0x876,
  MAXIMUM_SIZE_OF_L2_MESSAGE_EXCEEDED = 0x876,
  NAS_REQUEST_REJECTED_BY_NETWORK = 0x877,
  RRC_CONNECTION_INVALID_REQUEST = 0x878,
  RRC_CONNECTION_TRACKING_AREA_ID_CHANGED = 0x879,
  RRC_CONNECTION_RF_UNAVAILABLE = 0x87A,
  RRC_CONNECTION_ABORTED_DUE_TO_IRAT_CHANGE = 0x87B,
  RRC_CONNECTION_RELEASED_SECURITY_NOT_ACTIVE = 0x87C,
  RRC_CONNECTION_ABORTED_AFTER_HANDOVER = 0x87D,
  RRC_CONNECTION_ABORTED_AFTER_IRAT_CELL_CHANGE = 0x87E,
  RRC_CONNECTION_ABORTED_DURING_IRAT_CELL_CHANGE = 0x87F,
  IMSI_UNKNOWN_IN_HOME_SUBSCRIBER_SERVER = 0x880,
  IMEI_NOT_ACCEPTED = 0x881,
  EPS_SERVICES_AND_NON_EPS_SERVICES_NOT_ALLOWED = 0x882,
  EPS_SERVICES_NOT_ALLOWED_IN_PLMN = 0x883,
  MSC_TEMPORARILY_NOT_REACHABLE = 0x884,
  CS_DOMAIN_NOT_AVAILABLE = 0x885,
  ESM_FAILURE = 0x886,
  MAC_FAILURE = 0x887,
  SYNCHRONIZATION_FAILURE = 0x888,
  UE_SECURITY_CAPABILITIES_MISMATCH = 0x889,
  SECURITY_MODE_REJECTED = 0x88A,
  UNACCEPTABLE_NON_EPS_AUTHENTICATION = 0x88B,
  CS_FALLBACK_CALL_ESTABLISHMENT_NOT_ALLOWED = 0x88C,
  NO_EPS_BEARER_CONTEXT_ACTIVATED = 0x88D,
  INVALID_EMM_STATE = 0x88E,
  NAS_LAYER_FAILURE = 0x88F,
  MULTIPLE_PDP_CALL_NOT_ALLOWED = 0x890,
  EMBMS_NOT_ENABLED = 0x891,
  IRAT_HANDOVER_FAILED = 0x892,
  EMBMS_REGULAR_DEACTIVATION = 0x893,
  TEST_LOOPBACK_REGULAR_DEACTIVATION = 0x894,
  LOWER_LAYER_REGISTRATION_FAILURE = 0x895,
  DATA_PLAN_EXPIRED = 0x896,
  UMTS_HANDOVER_TO_IWLAN = 0x897,
  /**
   * @deprecated Legacy CDMA is unsupported.
   */
  EVDO_CONNECTION_DENY_BY_GENERAL_OR_NETWORK_BUSY = 0x898,
  /**
   * @deprecated Legacy CDMA is unsupported.
   */
  EVDO_CONNECTION_DENY_BY_BILLING_OR_AUTHENTICATION_FAILURE = 0x899,
  /**
   * @deprecated Legacy CDMA is unsupported.
   */
  EVDO_HDR_CHANGED = 0x89A,
  /**
   * @deprecated Legacy CDMA is unsupported.
   */
  EVDO_HDR_EXITED = 0x89B,
  /**
   * @deprecated Legacy CDMA is unsupported.
   */
  EVDO_HDR_NO_SESSION = 0x89C,
  /**
   * @deprecated Legacy CDMA is unsupported.
   */
  EVDO_USING_GPS_FIX_INSTEAD_OF_HDR_CALL = 0x89D,
  /**
   * @deprecated Legacy CDMA is unsupported.
   */
  EVDO_HDR_CONNECTION_SETUP_TIMEOUT = 0x89E,
  FAILED_TO_ACQUIRE_COLOCATED_HDR = 0x89F,
  OTASP_COMMIT_IN_PROGRESS = 0x8A0,
  NO_HYBRID_HDR_SERVICE = 0x8A1,
  HDR_NO_LOCK_GRANTED = 0x8A2,
  DBM_OR_SMS_IN_PROGRESS = 0x8A3,
  HDR_FADE = 0x8A4,
  HDR_ACCESS_FAILURE = 0x8A5,
  UNSUPPORTED_1X_PREV = 0x8A6,
  LOCAL_END = 0x8A7,
  NO_SERVICE = 0x8A8,
  FADE = 0x8A9,
  NORMAL_RELEASE = 0x8AA,
  ACCESS_ATTEMPT_ALREADY_IN_PROGRESS = 0x8AB,
  REDIRECTION_OR_HANDOFF_IN_PROGRESS = 0x8AC,
  EMERGENCY_MODE = 0x8AD,
  PHONE_IN_USE = 0x8AE,
  INVALID_MODE = 0x8AF,
  INVALID_SIM_STATE = 0x8B0,
  NO_COLLOCATED_HDR = 0x8B1,
  UE_IS_ENTERING_POWERSAVE_MODE = 0x8B2,
  DUAL_SWITCH = 0x8B3,
  PPP_TIMEOUT = 0x8B4,
  PPP_AUTH_FAILURE = 0x8B5,
  PPP_OPTION_MISMATCH = 0x8B6,
  PPP_PAP_FAILURE = 0x8B7,
  PPP_CHAP_FAILURE = 0x8B8,
  PPP_CLOSE_IN_PROGRESS = 0x8B9,
  LIMITED_TO_IPV4 = 0x8BA,
  LIMITED_TO_IPV6 = 0x8BB,
  VSNCP_TIMEOUT = 0x8BC,
  VSNCP_GEN_ERROR = 0x8BD,
  VSNCP_APN_UNAUTHORIZED = 0x8BE,
  VSNCP_PDN_LIMIT_EXCEEDED = 0x8BF,
  VSNCP_NO_PDN_GATEWAY_ADDRESS = 0x8C0,
  VSNCP_PDN_GATEWAY_UNREACHABLE = 0x8C1,
  VSNCP_PDN_GATEWAY_REJECT = 0x8C2,
  VSNCP_INSUFFICIENT_PARAMETERS = 0x8C3,
  VSNCP_RESOURCE_UNAVAILABLE = 0x8C4,
  VSNCP_ADMINISTRATIVELY_PROHIBITED = 0x8C5,
  VSNCP_PDN_ID_IN_USE = 0x8C6,
  VSNCP_SUBSCRIBER_LIMITATION = 0x8C7,
  VSNCP_PDN_EXISTS_FOR_THIS_APN = 0x8C8,
  VSNCP_RECONNECT_NOT_ALLOWED = 0x8C9,
  IPV6_PREFIX_UNAVAILABLE = 0x8CA,
  HANDOFF_PREFERENCE_CHANGED = 0x8CB,
  SLICE_REJECTED = 0x8CC,
  MATCH_ALL_RULE_NOT_ALLOWED = 0x8CD,
  ALL_MATCHING_RULES_FAILED = 0x8CE,
}
