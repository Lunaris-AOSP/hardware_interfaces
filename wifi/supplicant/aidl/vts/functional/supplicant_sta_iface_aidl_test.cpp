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

#include <VtsCoreUtil.h>
#include <aidl/Gtest.h>
#include <aidl/Vintf.h>
#include <aidl/android/hardware/wifi/supplicant/BnSupplicant.h>
#include <aidl/android/hardware/wifi/supplicant/BnSupplicantStaIfaceCallback.h>
#include <android/binder_manager.h>
#include <android/binder_status.h>
#include <binder/IServiceManager.h>
#include <binder/ProcessState.h>
#include <cutils/properties.h>

#include "supplicant_test_utils.h"

using aidl::android::hardware::wifi::supplicant::AnqpInfoId;
using aidl::android::hardware::wifi::supplicant::BnSupplicantStaIfaceCallback;
using aidl::android::hardware::wifi::supplicant::BtCoexistenceMode;
using aidl::android::hardware::wifi::supplicant::ConnectionCapabilities;
using aidl::android::hardware::wifi::supplicant::DebugLevel;
using aidl::android::hardware::wifi::supplicant::DppAkm;
using aidl::android::hardware::wifi::supplicant::DppConnectionKeys;
using aidl::android::hardware::wifi::supplicant::DppCurve;
using aidl::android::hardware::wifi::supplicant::DppNetRole;
using aidl::android::hardware::wifi::supplicant::DppResponderBootstrapInfo;
using aidl::android::hardware::wifi::supplicant::Hs20AnqpSubtypes;
using aidl::android::hardware::wifi::supplicant::IfaceType;
using aidl::android::hardware::wifi::supplicant::ISupplicant;
using aidl::android::hardware::wifi::supplicant::ISupplicantStaIface;
using aidl::android::hardware::wifi::supplicant::ISupplicantStaNetwork;
using aidl::android::hardware::wifi::supplicant::KeyMgmtMask;
using aidl::android::hardware::wifi::supplicant::MloLinksInfo;
using aidl::android::hardware::wifi::supplicant::MscsParams;
using aidl::android::hardware::wifi::supplicant::QosCharacteristics;
using aidl::android::hardware::wifi::supplicant::QosPolicyScsData;
using aidl::android::hardware::wifi::supplicant::QosPolicyScsRequestStatus;
using aidl::android::hardware::wifi::supplicant::SignalPollResult;
using aidl::android::hardware::wifi::supplicant::UsdBaseConfig;
using aidl::android::hardware::wifi::supplicant::UsdCapabilities;
using aidl::android::hardware::wifi::supplicant::UsdMessageInfo;
using aidl::android::hardware::wifi::supplicant::UsdPublishConfig;
using aidl::android::hardware::wifi::supplicant::UsdServiceDiscoveryInfo;
using aidl::android::hardware::wifi::supplicant::UsdSubscribeConfig;
using aidl::android::hardware::wifi::supplicant::UsdTerminateReasonCode;
using aidl::android::hardware::wifi::supplicant::WpaDriverCapabilitiesMask;
using aidl::android::hardware::wifi::supplicant::WpsConfigMethods;
using android::ProcessState;

static constexpr int TIMEOUT_PERIOD = 60;
class IfaceDppCallback;

namespace {
const std::vector<uint8_t> kTestMacAddr = {0x56, 0x67, 0x67, 0xf4, 0x56, 0x92};
const std::string kTestUri =
    "DPP:C:81/1,117/"
    "40;M:48d6d5bd1de1;I:G1197843;K:MDkwEwYHKoZIzj0CAQYIKoZIzj"
    "0DAQcDIgAD0edY4X3N//HhMFYsZfMbQJTiNFtNIWF/cIwMB/gzqOM=;;";
}  // namespace

class SupplicantStaIfaceCallback : public BnSupplicantStaIfaceCallback {
   public:
    SupplicantStaIfaceCallback() = default;

    ::ndk::ScopedAStatus onAnqpQueryDone(
        const std::vector<uint8_t>& /* bssid */,
        const ::aidl::android::hardware::wifi::supplicant::AnqpData& /* data */,
        const ::aidl::android::hardware::wifi::supplicant::
            Hs20AnqpData& /* hs20Data */) override {
        return ndk::ScopedAStatus::ok();
    }
    ::ndk::ScopedAStatus onAssociationRejected(
        const ::aidl::android::hardware::wifi::supplicant::
            AssociationRejectionData& /* assocRejectData */) override {
        return ndk::ScopedAStatus::ok();
    }
    ::ndk::ScopedAStatus onAuthenticationTimeout(
        const std::vector<uint8_t>& /* bssid */) override {
        return ndk::ScopedAStatus::ok();
    }
    ::ndk::ScopedAStatus onAuxiliarySupplicantEvent(
            ::aidl::android::hardware::wifi::supplicant ::
                    AuxiliarySupplicantEventCode /* eventCode */,
            const std::vector<uint8_t>& /* bssid */,
            const std::string& /* reasonString */) override {
        return ndk::ScopedAStatus::ok();
    }
    ::ndk::ScopedAStatus onBssTmHandlingDone(
        const ::aidl::android::hardware::wifi::supplicant::
            BssTmData& /* tmData */) override {
        return ndk::ScopedAStatus::ok();
    }
    ::ndk::ScopedAStatus onBssidChanged(
        ::aidl::android::hardware::wifi::supplicant::
            BssidChangeReason /* reason */,
        const std::vector<uint8_t>& /* bssid */) override {
        return ndk::ScopedAStatus::ok();
    }
    ::ndk::ScopedAStatus onDisconnected(
        const std::vector<uint8_t>& /* bssid */, bool /* locallyGenerated */,
        ::aidl::android::hardware::wifi::supplicant::
            StaIfaceReasonCode /* reasonCode */) override {
        return ndk::ScopedAStatus::ok();
    }
    ::ndk::ScopedAStatus onDppFailure(
        ::aidl::android::hardware::wifi::supplicant::DppFailureCode /* code */,
        const std::string& /* ssid */, const std::string& /* channelList */,
        const std::vector<char16_t>& /* bandList */) override {
        return ndk::ScopedAStatus::ok();
    }
    ::ndk::ScopedAStatus onDppProgress(
        ::aidl::android::hardware::wifi::supplicant::DppProgressCode /* code */)
        override {
        return ndk::ScopedAStatus::ok();
    }
    ::ndk::ScopedAStatus onDppSuccess(
        ::aidl::android::hardware::wifi::supplicant::DppEventType /* type */)
        override {
        return ndk::ScopedAStatus::ok();
    }
    ::ndk::ScopedAStatus onDppSuccessConfigReceived(
            const std::vector<uint8_t>& /* ssid */, const std::string& /* password */,
            const std::vector<uint8_t>& /* psk */,
            ::aidl::android::hardware::wifi::supplicant::DppAkm /* securityAkm */,
            const ::aidl::android::hardware::wifi::supplicant::
                    DppConnectionKeys& /* DppConnectionKeys */) override {
        return ndk::ScopedAStatus::ok();
    }
    ::ndk::ScopedAStatus onDppSuccessConfigSent() override {
        return ndk::ScopedAStatus::ok();
    }
    ::ndk::ScopedAStatus onEapFailure(const std::vector<uint8_t>& /* bssid */,
                                      int32_t /* errorCode */) override {
        return ndk::ScopedAStatus::ok();
    }
    ::ndk::ScopedAStatus onExtRadioWorkStart(int32_t /* id */) override {
        return ndk::ScopedAStatus::ok();
    }
    ::ndk::ScopedAStatus onExtRadioWorkTimeout(int32_t /* id */) override {
        return ndk::ScopedAStatus::ok();
    }
    ::ndk::ScopedAStatus onHs20DeauthImminentNotice(
        const std::vector<uint8_t>& /* bssid */, int32_t /* reasonCode */,
        int32_t /* reAuthDelayInSec */, const std::string& /* url */) override {
        return ndk::ScopedAStatus::ok();
    }
    ::ndk::ScopedAStatus onHs20IconQueryDone(
        const std::vector<uint8_t>& /* bssid */,
        const std::string& /* fileName */,
        const std::vector<uint8_t>& /* data */) override {
        return ndk::ScopedAStatus::ok();
    }
    ::ndk::ScopedAStatus onHs20SubscriptionRemediation(
        const std::vector<uint8_t>& /* bssid */,
        ::aidl::android::hardware::wifi::supplicant::OsuMethod /* osuMethod */,
        const std::string& /* url */) override {
        return ndk::ScopedAStatus::ok();
    }
    ::ndk::ScopedAStatus
    onHs20TermsAndConditionsAcceptanceRequestedNotification(
        const std::vector<uint8_t>& /* bssid */,
        const std::string& /* url */) override {
        return ndk::ScopedAStatus::ok();
    }
    ::ndk::ScopedAStatus onNetworkAdded(int32_t /* id */) override {
        return ndk::ScopedAStatus::ok();
    }
    ::ndk::ScopedAStatus onNetworkNotFound(
        const std::vector<uint8_t>& /* ssid */) override {
        return ndk::ScopedAStatus::ok();
    }
    ::ndk::ScopedAStatus onNetworkRemoved(int32_t /* id */) override {
        return ndk::ScopedAStatus::ok();
    }
    ::ndk::ScopedAStatus onPmkCacheAdded(
        int64_t /* expirationTimeInSec */,
        const std::vector<uint8_t>& /* serializedEntry */) override {
        return ndk::ScopedAStatus::ok();
    }
    ::ndk::ScopedAStatus onStateChanged(
        ::aidl::android::hardware::wifi::supplicant::
            StaIfaceCallbackState /* newState */,
        const std::vector<uint8_t>& /* bssid */, int32_t /* id */,
        const std::vector<uint8_t>& /* ssid */,
        bool /* filsHlpSent */) override {
        return ndk::ScopedAStatus::ok();
    }
    ::ndk::ScopedAStatus onWpsEventFail(
        const std::vector<uint8_t>& /* bssid */,
        ::aidl::android::hardware::wifi::supplicant::
            WpsConfigError /* configError */,
        ::aidl::android::hardware::wifi::supplicant::
            WpsErrorIndication /* errorInd */) override {
        return ndk::ScopedAStatus::ok();
    }
    ::ndk::ScopedAStatus onWpsEventPbcOverlap() override {
        return ndk::ScopedAStatus::ok();
    }
    ::ndk::ScopedAStatus onWpsEventSuccess() override {
        return ndk::ScopedAStatus::ok();
    }
    ::ndk::ScopedAStatus onQosPolicyReset() override { return ndk::ScopedAStatus::ok(); }
    ::ndk::ScopedAStatus onQosPolicyRequest(
            int32_t /* qosPolicyRequestId */,
            const std::vector<::aidl::android::hardware::wifi::supplicant ::
                                      QosPolicyData /* qosPolicyData */>&) override {
        return ndk::ScopedAStatus::ok();
    }
    ::ndk::ScopedAStatus onSupplicantStateChanged(
            const ::aidl::android::hardware::wifi::supplicant::
                    SupplicantStateChangeData& /* stateChangeData */) override {
        return ndk::ScopedAStatus::ok();
    }
    ::ndk::ScopedAStatus onMloLinksInfoChanged(
            ::aidl::android::hardware::wifi::supplicant::ISupplicantStaIfaceCallback::
                    MloLinkInfoChangeReason /* reason */) override {
        return ndk::ScopedAStatus::ok();
    }
    ::ndk::ScopedAStatus onDppConfigReceived(
            const ::aidl::android::hardware::wifi::supplicant::
                    DppConfigurationData& /* configData */) override {
        return ndk::ScopedAStatus::ok();
    }
    ::ndk::ScopedAStatus onDppConnectionStatusResultSent(
            ::aidl::android::hardware::wifi::supplicant::DppStatusErrorCode /* code */) override {
        return ndk::ScopedAStatus::ok();
    }
    ::ndk::ScopedAStatus onBssFrequencyChanged(int32_t /* frequencyMhz */) override {
        return ndk::ScopedAStatus::ok();
    }
    ::ndk::ScopedAStatus onQosPolicyResponseForScs(
            const std::vector<::aidl::android::hardware::wifi::supplicant::
                                      QosPolicyScsResponseStatus>& /* qosPolicyScsResponseStatus */)
            override {
        return ndk::ScopedAStatus::ok();
    }
    ::ndk::ScopedAStatus onPmkSaCacheAdded(
            const ::aidl::android::hardware::wifi::supplicant::PmkSaCacheData& /* pmkSaData */)
            override {
        return ndk::ScopedAStatus::ok();
    }
    ::ndk::ScopedAStatus onUsdPublishStarted(int32_t /* cmdId */,
                                             int32_t /* publishId */) override {
        return ndk::ScopedAStatus::ok();
    }
    ::ndk::ScopedAStatus onUsdSubscribeStarted(int32_t /* cmdId */,
                                               int32_t /* subscribeId */) override {
        return ndk::ScopedAStatus::ok();
    }
    ::ndk::ScopedAStatus onUsdPublishConfigFailed(int32_t /* cmdId */,
                                                  UsdConfigErrorCode /* errorCode */) override {
        return ndk::ScopedAStatus::ok();
    }
    ::ndk::ScopedAStatus onUsdSubscribeConfigFailed(int32_t /* cmdId */,
                                                    UsdConfigErrorCode /* errorCode */) override {
        return ndk::ScopedAStatus::ok();
    }
    ::ndk::ScopedAStatus onUsdPublishTerminated(int32_t /* publishId */,
                                                UsdTerminateReasonCode /* reasonCode */) override {
        return ndk::ScopedAStatus::ok();
    }
    ::ndk::ScopedAStatus onUsdSubscribeTerminated(
            int32_t /* subscribeId */, UsdTerminateReasonCode /* reasonCode */) override {
        return ndk::ScopedAStatus::ok();
    }
    ::ndk::ScopedAStatus onUsdPublishReplied(const UsdServiceDiscoveryInfo& /* info */) override {
        return ndk::ScopedAStatus::ok();
    }
    ::ndk::ScopedAStatus onUsdServiceDiscovered(
            const UsdServiceDiscoveryInfo& /* info */) override {
        return ndk::ScopedAStatus::ok();
    }
    ::ndk::ScopedAStatus onUsdMessageReceived(const UsdMessageInfo& /* messageInfo */) override {
        return ndk::ScopedAStatus::ok();
    }
};

class SupplicantStaIfaceAidlTest : public testing::TestWithParam<std::string> {
   public:
    void SetUp() override {
        initializeService();
        supplicant_ = getSupplicant(GetParam().c_str());
        ASSERT_NE(supplicant_, nullptr);
        ASSERT_TRUE(supplicant_
                        ->setDebugParams(DebugLevel::EXCESSIVE,
                                         true,  // show timestamps
                                         true)
                        .isOk());
        ASSERT_TRUE(supplicant_->getInterfaceVersion(&interface_version_).isOk());
        EXPECT_TRUE(supplicant_->getStaInterface(getStaIfaceName(), &sta_iface_)
                        .isOk());
        ASSERT_NE(sta_iface_, nullptr);
    }

    void TearDown() override {
        stopSupplicantService();
        startWifiFramework();
    }

    enum DppCallbackType {
        ANY_CALLBACK = -2,
        INVALID = -1,
        EVENT_SUCCESS = 0,
        EVENT_PROGRESS,
        EVENT_FAILURE,
    };

    DppCallbackType dppCallbackType;
    uint32_t code;

    // Used as a mechanism to inform the test about data/event callback
    inline void notify() {
        std::unique_lock<std::mutex> lock(mtx_);
        cv_.notify_one();
    }

    // Test code calls this function to wait for data/event callback
    inline std::cv_status wait(DppCallbackType waitForCallbackType) {
        std::unique_lock<std::mutex> lock(mtx_);
        EXPECT_NE(INVALID, waitForCallbackType);  // can't ASSERT in a
                                                  // non-void-returning method
        auto now = std::chrono::system_clock::now();
        std::cv_status status =
            cv_.wait_until(lock, now + std::chrono::seconds(TIMEOUT_PERIOD));
        return status;
    }

   protected:
    std::shared_ptr<ISupplicant> supplicant_;
    std::shared_ptr<ISupplicantStaIface> sta_iface_;
    int interface_version_;

   private:
    // synchronization objects
    std::mutex mtx_;
    std::condition_variable cv_;
};

/*
 * RegisterCallback
 */
TEST_P(SupplicantStaIfaceAidlTest, RegisterCallback) {
    std::shared_ptr<SupplicantStaIfaceCallback> callback =
        ndk::SharedRefBase::make<SupplicantStaIfaceCallback>();
    ASSERT_NE(callback, nullptr);
    EXPECT_TRUE(sta_iface_->registerCallback(callback).isOk());
}

/*
 * GetConnectionCapabilities
 */
TEST_P(SupplicantStaIfaceAidlTest, GetConnectionCapabilities) {
    ConnectionCapabilities cap;
    EXPECT_TRUE(sta_iface_->getConnectionCapabilities(&cap).isOk());
}

/*
 * GetWpaDriverCapabilities
 */
TEST_P(SupplicantStaIfaceAidlTest, GetWpaDriverCapabilities) {
    WpaDriverCapabilitiesMask cap;
    EXPECT_TRUE(sta_iface_->getWpaDriverCapabilities(&cap).isOk());
}

/*
 * GetKeyMgmtCapabilities
 */
TEST_P(SupplicantStaIfaceAidlTest, GetKeyMgmtCapabilities) {
    KeyMgmtMask cap;
    EXPECT_TRUE(sta_iface_->getKeyMgmtCapabilities(&cap).isOk());

    // Even though capabilities vary, these two are always set.
    EXPECT_TRUE(!!(static_cast<uint32_t>(cap) &
                   static_cast<uint32_t>(KeyMgmtMask::NONE)));
    EXPECT_TRUE(!!(static_cast<uint32_t>(cap) &
                   static_cast<uint32_t>(KeyMgmtMask::IEEE8021X)));
}

/*
 * GetName
 */
TEST_P(SupplicantStaIfaceAidlTest, GetName) {
    std::string name;
    EXPECT_TRUE(sta_iface_->getName(&name).isOk());
    EXPECT_NE(name.size(), 0);
}

/*
 * GetType
 */
TEST_P(SupplicantStaIfaceAidlTest, GetType) {
    IfaceType type;
    EXPECT_TRUE(sta_iface_->getType(&type).isOk());
    EXPECT_EQ(type, IfaceType::STA);
}

/*
 * GetMacAddress
 */
TEST_P(SupplicantStaIfaceAidlTest, GetMacAddress) {
    std::vector<uint8_t> macAddr;
    EXPECT_TRUE(sta_iface_->getMacAddress(&macAddr).isOk());
    EXPECT_EQ(macAddr.size(), 6);
}

/*
 * ListNetworks
 */
TEST_P(SupplicantStaIfaceAidlTest, ListNetworks) {
    std::vector<int32_t> networks;
    EXPECT_TRUE(sta_iface_->listNetworks(&networks).isOk());
}

/*
 * SetBtCoexistenceMode
 */
TEST_P(SupplicantStaIfaceAidlTest, SetBtCoexistenceMode) {
    EXPECT_TRUE(
        sta_iface_->setBtCoexistenceMode(BtCoexistenceMode::ENABLED).isOk());
    EXPECT_TRUE(
        sta_iface_->setBtCoexistenceMode(BtCoexistenceMode::DISABLED).isOk());
    EXPECT_TRUE(
        sta_iface_->setBtCoexistenceMode(BtCoexistenceMode::SENSE).isOk());
}

/*
 * SetBtCoexistenceScanModeEnabled
 */
TEST_P(SupplicantStaIfaceAidlTest, SetBtCoexistenceScanModeEnabled) {
    EXPECT_TRUE(sta_iface_->setBtCoexistenceScanModeEnabled(true).isOk());
    EXPECT_TRUE(sta_iface_->setBtCoexistenceScanModeEnabled(false).isOk());
}

/*
 * SetSuspendModeEnabled
 */
TEST_P(SupplicantStaIfaceAidlTest, SetSuspendModeEnabled) {
    EXPECT_TRUE(sta_iface_->setSuspendModeEnabled(true).isOk());
    EXPECT_TRUE(sta_iface_->setSuspendModeEnabled(false).isOk());
}

/*
 * SetCountryCode
 */
TEST_P(SupplicantStaIfaceAidlTest, SetCountryCode) {
    const std::vector<uint8_t> countryCode = {'U', 'S'};
    EXPECT_TRUE(sta_iface_->setCountryCode(countryCode).isOk());
}

/*
 * SetWpsDeviceName
 */
TEST_P(SupplicantStaIfaceAidlTest, SetWpsDeviceName) {
    const std::string deviceName = "TestWpsDeviceName";
    EXPECT_TRUE(sta_iface_->setWpsDeviceName(deviceName).isOk());
}

/*
 * SetWpsDeviceType
 */
TEST_P(SupplicantStaIfaceAidlTest, SetWpsDeviceType) {
    const std::vector<uint8_t> deviceType = {8, 0x1};
    EXPECT_TRUE(sta_iface_->setWpsDeviceType(deviceType).isOk());
}

/*
 * SetWpsManufacturer
 */
TEST_P(SupplicantStaIfaceAidlTest, SetWpsManufacturer) {
    const std::string wpsManufacturer = "TestManufacturer";
    EXPECT_TRUE(sta_iface_->setWpsManufacturer(wpsManufacturer).isOk());
}

/*
 * SetWpsModelName
 */
TEST_P(SupplicantStaIfaceAidlTest, SetWpsModelName) {
    const std::string modelName = "TestModelName";
    EXPECT_TRUE(sta_iface_->setWpsModelName(modelName).isOk());
}

/*
 * SetWpsModelNumber
 */
TEST_P(SupplicantStaIfaceAidlTest, SetWpsModelNumber) {
    const std::string modelNumber = "TestModelNumber";
    EXPECT_TRUE(sta_iface_->setWpsModelNumber(modelNumber).isOk());
}

/*
 * SetWpsSerialNumber
 */
TEST_P(SupplicantStaIfaceAidlTest, SetWpsSerialNumber) {
    const std::string serialNumber = "TestSerialNumber";
    EXPECT_TRUE(sta_iface_->setWpsSerialNumber(serialNumber).isOk());
}

/*
 * SetWpsConfigMethods
 */
TEST_P(SupplicantStaIfaceAidlTest, SetWpsConfigMethods) {
    const WpsConfigMethods configMethods = WpsConfigMethods::KEYPAD;
    EXPECT_TRUE(sta_iface_->setWpsConfigMethods(configMethods).isOk());
}

/*
 * SetExternalSim
 */
TEST_P(SupplicantStaIfaceAidlTest, SetExternalSim) {
    EXPECT_TRUE(sta_iface_->setExternalSim(true).isOk());
    EXPECT_TRUE(sta_iface_->setExternalSim(false).isOk());
}

/*
 * SetMboCellularDataStatus
 */
TEST_P(SupplicantStaIfaceAidlTest, SetMboCellularDataStatus) {
    WpaDriverCapabilitiesMask cap;
    EXPECT_TRUE(sta_iface_->getWpaDriverCapabilities(&cap).isOk());

    // Operation should succeed if MBO is supported, or fail if it's not.
    bool mboSupported =
        !!(static_cast<uint32_t>(cap) &
           static_cast<uint32_t>(WpaDriverCapabilitiesMask::MBO));
    EXPECT_EQ(mboSupported, sta_iface_->setMboCellularDataStatus(true).isOk());
}

/*
 * InitiateTdlsDiscover
 */
TEST_P(SupplicantStaIfaceAidlTest, InitiateTdlsDiscover) {
    EXPECT_TRUE(sta_iface_->initiateTdlsDiscover(kTestMacAddr).isOk());
}

/*
 * InitiateTdlsSetup
 */
TEST_P(SupplicantStaIfaceAidlTest, InitiateTdlsSetup) {
    EXPECT_TRUE(sta_iface_->initiateTdlsSetup(kTestMacAddr).isOk());
}

/*
 * InitiateTdlsTeardown
 */
TEST_P(SupplicantStaIfaceAidlTest, InitiateTdlsTeardown) {
    EXPECT_TRUE(sta_iface_->initiateTdlsTeardown(kTestMacAddr).isOk());
}

/*
 * InitiateAnqpQuery
 */
TEST_P(SupplicantStaIfaceAidlTest, InitiateAnqpQuery) {
    const std::vector<AnqpInfoId> anqpIds = {
        AnqpInfoId::VENUE_NAME, AnqpInfoId::NAI_REALM, AnqpInfoId::DOMAIN_NAME};
    const std::vector<Hs20AnqpSubtypes> hsTypes = {
        Hs20AnqpSubtypes::WAN_METRICS,
        Hs20AnqpSubtypes::OPERATOR_FRIENDLY_NAME};

    // Request should fail since the BSSID mentioned
    // is not present in the scan results
    EXPECT_FALSE(
        sta_iface_->initiateAnqpQuery(kTestMacAddr, anqpIds, hsTypes).isOk());
}

/*
 * InitiateHs20IconQuery
 */
TEST_P(SupplicantStaIfaceAidlTest, InitiateHs20IconQuery) {
    // Request should fail since the BSSID mentioned
    // is not present in the scan results
    const std::string hs20IconFile = "TestFile";
    EXPECT_FALSE(
        sta_iface_->initiateHs20IconQuery(kTestMacAddr, hs20IconFile).isOk());
}

/*
 * InitiateVenueUrlAnqpQuery.
 */
TEST_P(SupplicantStaIfaceAidlTest, InitiateVenueUrlAnqpQuery) {
    // Request should fail since the BSSID mentioned
    // is not present in the scan results
    EXPECT_FALSE(sta_iface_->initiateVenueUrlAnqpQuery(kTestMacAddr).isOk());
}

/*
 * Reassociate
 */
TEST_P(SupplicantStaIfaceAidlTest, Reassociate) {
    EXPECT_TRUE(sta_iface_->reassociate().isOk());
}

/*
 * Reconnect
 */
TEST_P(SupplicantStaIfaceAidlTest, Reconnect) {
    EXPECT_FALSE(sta_iface_->reconnect().isOk());
}

/*
 * Disconnect
 */
TEST_P(SupplicantStaIfaceAidlTest, Disconnect) {
    EXPECT_TRUE(sta_iface_->disconnect().isOk());
}

/*
 * SetPowerSave
 */
TEST_P(SupplicantStaIfaceAidlTest, SetPowerSave) {
    EXPECT_TRUE(sta_iface_->setPowerSave(true).isOk());
    EXPECT_TRUE(sta_iface_->setPowerSave(false).isOk());
}

/*
 * AddExtRadioWork
 */
TEST_P(SupplicantStaIfaceAidlTest, AddExtRadioWork) {
    const std::string radioWorkName = "TestRadioWork";
    const int32_t radioWorkFreq = 2412;
    const int32_t radioWorkTimeout = 8;
    int32_t radioWorkId;
    EXPECT_TRUE(sta_iface_
                    ->addExtRadioWork(radioWorkName, radioWorkFreq,
                                      radioWorkTimeout, &radioWorkId)
                    .isOk());
    // removeExtRadio only succeeds if the added radio work hasn't started yet,
    // so there is no guaranteed result from calling removeExtRadioWork here.
    // Given that, we can't currently test removeExtRadioWork following
    // a call to addExtRadioWork.
}

/*
 * RemoveExtRadioWork
 */
TEST_P(SupplicantStaIfaceAidlTest, RemoveExtRadioWork) {
    // This fails because there is no ongoing radio work with radioWorkId
    const int32_t radioWorkId = 16;
    EXPECT_FALSE(sta_iface_->removeExtRadioWork(radioWorkId).isOk());
}

/*
 * Add/Remove DppPeerUri
 */
TEST_P(SupplicantStaIfaceAidlTest, AddRemoveDppPeerUri) {
    if (!keyMgmtSupported(sta_iface_, KeyMgmtMask::DPP)) {
        GTEST_SKIP() << "Missing DPP support";
    }
    // Add a peer URI and then remove it.
    int32_t peerId;
    EXPECT_TRUE(sta_iface_->addDppPeerUri(kTestUri, &peerId).isOk());
    EXPECT_TRUE(sta_iface_->removeDppUri(peerId).isOk());
}

/*
 * FilsHlpAddRequest
 */
TEST_P(SupplicantStaIfaceAidlTest, FilsHlpAddRequest) {
    if (!isFilsSupported(sta_iface_)) {
        GTEST_SKIP()
            << "Skipping test since driver/supplicant doesn't support FILS";
    }
    const std::vector<uint8_t> destMacAddr = {0x00, 0x11, 0x22,
                                              0x33, 0x44, 0x55};
    const std::vector<uint8_t> pktBuffer = std::vector<uint8_t>(300, 0x3a);
    EXPECT_TRUE(sta_iface_->filsHlpAddRequest(destMacAddr, pktBuffer).isOk());
}

/*
 * FilsHlpFlushRequest
 */
TEST_P(SupplicantStaIfaceAidlTest, FilsHlpFlushRequest) {
    if (!isFilsSupported(sta_iface_)) {
        GTEST_SKIP()
            << "Skipping test since driver/supplicant doesn't support FILS";
    }
    EXPECT_TRUE(sta_iface_->filsHlpFlushRequest().isOk());
}

/*
 * StartDppEnrolleeResponder
 */
TEST_P(SupplicantStaIfaceAidlTest, StartDppEnrolleeResponder) {
    if (!keyMgmtSupported(sta_iface_, KeyMgmtMask::DPP)) {
        GTEST_SKIP() << "Missing DPP support";
    }

    const std::string deviceInfo = "DPP_Responder_Mode_VTS_Test";
    const std::vector<uint8_t> mac_address = {0x22, 0x33, 0x44,
                                              0x55, 0x66, 0x77};

    // Generate DPP bootstrap information.
    DppResponderBootstrapInfo bootstrapInfo;
    EXPECT_TRUE(
        sta_iface_
            ->generateDppBootstrapInfoForResponder(
                mac_address, deviceInfo, DppCurve::PRIME256V1, &bootstrapInfo)
            .isOk());
    EXPECT_NE(-1, bootstrapInfo.bootstrapId);
    EXPECT_NE(0, bootstrapInfo.bootstrapId);
    EXPECT_NE(0, bootstrapInfo.listenChannel);
    const uint32_t bootstrap_id = bootstrapInfo.bootstrapId;
    const uint32_t listen_channel = bootstrapInfo.listenChannel;

    // Start DPP as Enrollee-Responder.
    EXPECT_TRUE(sta_iface_->startDppEnrolleeResponder(listen_channel).isOk());

    // Stop DPP Enrollee-Responder mode, ie remove the URI and stop listen.
    EXPECT_TRUE(sta_iface_->stopDppResponder(bootstrap_id).isOk());
}

class IfaceDppCallback : public SupplicantStaIfaceCallback {
    SupplicantStaIfaceAidlTest& parent_;
    ::ndk::ScopedAStatus onDppSuccess(
        ::aidl::android::hardware::wifi::supplicant::DppEventType event)
        override {
        parent_.code = (uint32_t)event;
        parent_.dppCallbackType =
            SupplicantStaIfaceAidlTest::DppCallbackType::EVENT_SUCCESS;
        parent_.notify();
        return ndk::ScopedAStatus::ok();
    }
    ::ndk::ScopedAStatus onDppProgress(
        aidl::android::hardware::wifi::supplicant::DppProgressCode code)
        override {
        parent_.code = (uint32_t)code;
        parent_.dppCallbackType =
            SupplicantStaIfaceAidlTest::DppCallbackType::EVENT_PROGRESS;
        parent_.notify();
        return ndk::ScopedAStatus::ok();
    }
    ::ndk::ScopedAStatus onDppFailure(
        aidl::android::hardware::wifi::supplicant::DppFailureCode code,
        const std::string& ssid __attribute__((unused)),
        const std::string& channelList __attribute__((unused)),
        const std::vector<char16_t>& bandList
        __attribute__((unused))) override {
        parent_.code = (uint32_t)code;
        parent_.dppCallbackType =
            SupplicantStaIfaceAidlTest::DppCallbackType::EVENT_FAILURE;
        parent_.notify();
        return ndk::ScopedAStatus::ok();
    }

   public:
    IfaceDppCallback(SupplicantStaIfaceAidlTest& parent) : parent_(parent){};
};

/*
 * StartDppEnrolleeInitiator
 */
TEST_P(SupplicantStaIfaceAidlTest, StartDppEnrolleeInitiator) {
    if (!keyMgmtSupported(sta_iface_, KeyMgmtMask::DPP)) {
        GTEST_SKIP() << "Missing DPP support";
    }

    // Register callback
    std::shared_ptr<IfaceDppCallback> callback =
        ndk::SharedRefBase::make<IfaceDppCallback>(*this);
    EXPECT_NE(callback, nullptr);
    EXPECT_TRUE(sta_iface_->registerCallback(callback).isOk());

    // Add a peer URI
    int32_t peer_id = 0;
    EXPECT_TRUE(sta_iface_->addDppPeerUri(kTestUri, &peer_id).isOk());
    EXPECT_NE(0, peer_id);
    EXPECT_NE(-1, peer_id);

    // Start DPP as Enrollee-Initiator. Since this operation requires two
    // devices, we start the operation and expect a timeout.
    EXPECT_TRUE(sta_iface_->startDppEnrolleeInitiator(peer_id, 0).isOk());

    // Wait for the timeout callback
    EXPECT_EQ(std::cv_status::no_timeout,
              wait(SupplicantStaIfaceAidlTest::DppCallbackType::EVENT_FAILURE));
    EXPECT_EQ(SupplicantStaIfaceAidlTest::DppCallbackType::EVENT_FAILURE,
              dppCallbackType);

    // ...and then remove the peer URI.
    EXPECT_TRUE(sta_iface_->removeDppUri(peer_id).isOk());
}

/*
 * StartDppConfiguratorInitiator
 */
TEST_P(SupplicantStaIfaceAidlTest, StartDppConfiguratorInitiator) {
    if (!keyMgmtSupported(sta_iface_, KeyMgmtMask::DPP)) {
        GTEST_SKIP() << "Missing DPP support";
    }

    // Register callback
    std::shared_ptr<IfaceDppCallback> callback =
        ndk::SharedRefBase::make<IfaceDppCallback>(*this);
    EXPECT_NE(callback, nullptr);
    EXPECT_TRUE(sta_iface_->registerCallback(callback).isOk());

    // Add a peer URI
    int32_t peer_id = 0;
    EXPECT_TRUE(sta_iface_->addDppPeerUri(kTestUri, &peer_id).isOk());
    EXPECT_NE(0, peer_id);
    EXPECT_NE(-1, peer_id);

    const std::string ssid =
        "6D795F746573745F73736964";  // 'my_test_ssid' encoded in hex
    const std::string password =
        "746F70736563726574";  // 'topsecret' encoded in hex
    const std::vector<uint8_t> eckey_in = {0x2, 0x3, 0x4};
    std::vector<uint8_t> eckey_out = {};

    // Start DPP as Configurator-Initiator. Since this operation requires two
    // devices, we start the operation and expect a timeout.
    EXPECT_TRUE(sta_iface_
                        ->startDppConfiguratorInitiator(peer_id, 0, ssid, password, "",
                                                        DppNetRole::STA, DppAkm::PSK, eckey_in,
                                                        &eckey_out)
                        .isOk());

    // Wait for the timeout callback
    ASSERT_EQ(std::cv_status::no_timeout,
              wait(SupplicantStaIfaceAidlTest::DppCallbackType::EVENT_FAILURE));
    ASSERT_EQ(SupplicantStaIfaceAidlTest::DppCallbackType::EVENT_FAILURE,
              dppCallbackType);

    // ...and then remove the peer URI.
    EXPECT_TRUE(sta_iface_->removeDppUri(peer_id).isOk());
}

/*
 * Configure and Disable MSCS
 */
TEST_P(SupplicantStaIfaceAidlTest, ConfigureAndDisableMscs) {
    if (interface_version_ < 3) {
        GTEST_SKIP() << "MSCS configure/disable is available as of Supplicant V3";
    }
    MscsParams params;
    params.upBitmap = 0;
    params.upLimit = 7;
    params.streamTimeoutUs = 1000;  // 1 ms
    params.frameClassifierMask = 0;
    EXPECT_TRUE(sta_iface_->configureMscs(params).isOk());
    EXPECT_TRUE(sta_iface_->disableMscs().isOk());
}

/*
 * Add and remove QoS policy with traffic characteristics
 */
TEST_P(SupplicantStaIfaceAidlTest, AddAndRemoveQosWithTrafficChars) {
    if (interface_version_ < 3) {
        GTEST_SKIP() << "QosCharacteristics is available as of Supplicant V3";
    }

    QosCharacteristics qosChars;
    qosChars.minServiceIntervalUs = 2000;
    qosChars.maxServiceIntervalUs = 5000;
    qosChars.minDataRateKbps = 500;
    qosChars.delayBoundUs = 200;
    qosChars.optionalFieldMask = 0;  // no optional fields

    uint8_t policyId = 5;
    QosPolicyScsData qosPolicy;
    qosPolicy.policyId = policyId;
    qosPolicy.direction = QosPolicyScsData::LinkDirection::UPLINK;
    qosPolicy.QosCharacteristics = qosChars;

    std::vector<uint8_t> policyIdList{policyId};
    std::vector<QosPolicyScsData> policyList{qosPolicy};
    std::vector<QosPolicyScsRequestStatus> responseList;

    // Check that we receive some reply for this request.
    // Policy may not be accepted (ex. policy with this id already exists).
    EXPECT_TRUE(sta_iface_->addQosPolicyRequestForScs(policyList, &responseList).isOk());
    EXPECT_EQ(1, responseList.size());
    EXPECT_TRUE(sta_iface_->removeQosPolicyForScs(policyIdList, &responseList).isOk());
    EXPECT_EQ(1, responseList.size());
}

/*
 * Verify that all USD methods check the Service Specific Info (SSI) length
 * and fail if the provided SSI is too long.
 */
TEST_P(SupplicantStaIfaceAidlTest, InvalidUsdServiceSpecificInfo) {
    if (interface_version_ < 4) {
        GTEST_SKIP() << "USD is available as of Supplicant V4";
    }

    UsdCapabilities caps;
    EXPECT_TRUE(sta_iface_->getUsdCapabilities(&caps).isOk());
    if (!caps.isUsdPublisherSupported && !caps.isUsdSubscriberSupported) {
        GTEST_SKIP() << "USD publish and subscribe are not supported";
    }

    int commandId = 123;
    std::vector<uint8_t> invalidSsi(caps.maxLocalSsiLengthBytes + 1);
    UsdBaseConfig invalidBaseConfig;
    invalidBaseConfig.serviceSpecificInfo = invalidSsi;

    if (caps.isUsdPublisherSupported) {
        UsdPublishConfig publishConfig;
        publishConfig.usdBaseConfig = invalidBaseConfig;
        EXPECT_FALSE(sta_iface_->startUsdPublish(commandId, publishConfig).isOk());
        EXPECT_FALSE(sta_iface_->updateUsdPublish(commandId, invalidSsi).isOk());
    }

    if (caps.isUsdSubscriberSupported) {
        UsdSubscribeConfig subscribeConfig;
        subscribeConfig.usdBaseConfig = invalidBaseConfig;
        EXPECT_FALSE(sta_iface_->startUsdSubscribe(commandId, subscribeConfig).isOk());
    }

    UsdMessageInfo messageInfo;
    messageInfo.message = invalidSsi;
    EXPECT_FALSE(sta_iface_->sendUsdMessage(messageInfo).isOk());
}

/*
 * Cancel a USD Publish and Subscribe session.
 */
TEST_P(SupplicantStaIfaceAidlTest, CancelUsdSession) {
    if (interface_version_ < 4) {
        GTEST_SKIP() << "USD is available as of Supplicant V4";
    }

    UsdCapabilities caps;
    EXPECT_TRUE(sta_iface_->getUsdCapabilities(&caps).isOk());
    if (!caps.isUsdPublisherSupported && !caps.isUsdSubscriberSupported) {
        GTEST_SKIP() << "USD publish and subscribe are not supported";
    }

    int sessionId = 123;
    if (caps.isUsdPublisherSupported) {
        // Method is expected to succeed, even if the session does not exist.
        EXPECT_TRUE(sta_iface_->cancelUsdPublish(sessionId).isOk());
    }
    if (caps.isUsdSubscriberSupported) {
        EXPECT_TRUE(sta_iface_->cancelUsdSubscribe(sessionId).isOk());
    }
}

/*
 * GenerateSelfDppConfiguration
 */
TEST_P(SupplicantStaIfaceAidlTest, GenerateSelfDppConfiguration) {
    if (!keyMgmtSupported(sta_iface_, KeyMgmtMask::DPP)) {
        GTEST_SKIP() << "Missing DPP support";
    }
    const std::string ssid = "my_test_ssid";
    const std::vector<uint8_t> eckey_in = {0x2, 0x3, 0x4};

    // Expect to fail as this test requires a DPP AKM supported AP and a valid private EC
    // key generated by wpa_supplicant.
    EXPECT_FALSE(sta_iface_->generateSelfDppConfiguration(ssid, eckey_in).isOk());
}

/*
 * getSignalPollResults
 */
TEST_P(SupplicantStaIfaceAidlTest, GetSignalPollResults) {
    if (interface_version_ < 2) {
        GTEST_SKIP() << "getSignalPollResults is available as of Supplicant V2";
    }

    std::vector<SignalPollResult> results;
    EXPECT_TRUE(sta_iface_->getSignalPollResults(&results).isOk());
}

/*
 * Test that we can start and cancel all WPS methods.
 */
TEST_P(SupplicantStaIfaceAidlTest, StartAndCancelWps) {
    std::vector<uint8_t> zeroMacAddr = {0, 0, 0, 0, 0, 0};
    std::string pin = "12345678";

    EXPECT_TRUE(sta_iface_->startWpsPbc(zeroMacAddr).isOk());
    EXPECT_TRUE(sta_iface_->cancelWps().isOk());

    std::string generatedPin;
    EXPECT_TRUE(sta_iface_->startWpsPinDisplay(zeroMacAddr, &generatedPin).isOk());
    EXPECT_TRUE(sta_iface_->cancelWps().isOk());

    EXPECT_TRUE(sta_iface_->startWpsPinKeypad(pin).isOk());
    EXPECT_TRUE(sta_iface_->cancelWps().isOk());

    EXPECT_TRUE(sta_iface_->startWpsRegistrar(zeroMacAddr, pin).isOk());
    EXPECT_TRUE(sta_iface_->cancelWps().isOk());
}

/*
 * Test that we can add, list, get, and remove a network.
 */
TEST_P(SupplicantStaIfaceAidlTest, ManageNetwork) {
    std::shared_ptr<ISupplicantStaNetwork> network;
    EXPECT_TRUE(sta_iface_->addNetwork(&network).isOk());
    EXPECT_NE(network, nullptr);

    std::vector<int32_t> networkList;
    EXPECT_TRUE(sta_iface_->listNetworks(&networkList).isOk());
    EXPECT_EQ(networkList.size(), 1);

    int networkId;
    EXPECT_TRUE(network->getId(&networkId).isOk());
    EXPECT_EQ(networkId, networkList[0]);

    std::shared_ptr<ISupplicantStaNetwork> retrievedNetwork;
    EXPECT_TRUE(sta_iface_->getNetwork(networkId, &retrievedNetwork).isOk());
    EXPECT_NE(retrievedNetwork, nullptr);
    EXPECT_TRUE(sta_iface_->removeNetwork(networkId).isOk());
}

/*
 * EnableAutoReconnect
 */
TEST_P(SupplicantStaIfaceAidlTest, EnableAutoReconnect) {
    EXPECT_TRUE(sta_iface_->enableAutoReconnect(true).isOk());
    EXPECT_TRUE(sta_iface_->enableAutoReconnect(false).isOk());
}

/*
 * SetQosPolicyFeatureEnabled
 */
TEST_P(SupplicantStaIfaceAidlTest, SetQosPolicyFeatureEnabled) {
    EXPECT_TRUE(sta_iface_->setQosPolicyFeatureEnabled(true).isOk());
    EXPECT_TRUE(sta_iface_->setQosPolicyFeatureEnabled(false).isOk());
}

/*
 * GetConnectionMloLinksInfo
 */
TEST_P(SupplicantStaIfaceAidlTest, GetConnectionMloLinksInfo) {
    MloLinksInfo mloInfo;
    EXPECT_TRUE(sta_iface_->getConnectionMloLinksInfo(&mloInfo).isOk());
}

/*
 * StopDppInitiator
 */
TEST_P(SupplicantStaIfaceAidlTest, StopDppInitiator) {
    if (!keyMgmtSupported(sta_iface_, KeyMgmtMask::DPP)) {
        GTEST_SKIP() << "Missing DPP support";
    }
    EXPECT_TRUE(sta_iface_->stopDppInitiator().isOk());
}

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(SupplicantStaIfaceAidlTest);
INSTANTIATE_TEST_SUITE_P(Supplicant, SupplicantStaIfaceAidlTest,
                         testing::ValuesIn(android::getAidlHalInstanceNames(
                             ISupplicant::descriptor)),
                         android::PrintInstanceNameToString);

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    ProcessState::self()->setThreadPoolMaxThreadCount(1);
    ProcessState::self()->startThreadPool();
    return RUN_ALL_TESTS();
}
