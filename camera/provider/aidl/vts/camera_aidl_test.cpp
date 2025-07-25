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

#include "camera_aidl_test.h"

#include <inttypes.h>

#include <CameraParameters.h>
#include <HandleImporter.h>
#include <aidl/android/hardware/camera/device/ICameraDevice.h>
#include <aidl/android/hardware/camera/metadata/CameraMetadataTag.h>
#include <aidl/android/hardware/camera/metadata/RequestAvailableColorSpaceProfilesMap.h>
#include <aidl/android/hardware/camera/metadata/RequestAvailableDynamicRangeProfilesMap.h>
#include <aidl/android/hardware/camera/metadata/SensorInfoColorFilterArrangement.h>
#include <aidl/android/hardware/camera/metadata/SensorPixelMode.h>
#include <aidl/android/hardware/camera/provider/BnCameraProviderCallback.h>
#include <aidlcommonsupport/NativeHandle.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <com_android_internal_camera_flags.h>
#include <device_cb.h>
#include <empty_device_cb.h>
#include <grallocusage/GrallocUsageConversion.h>
#include <hardware/gralloc1.h>
#include <simple_device_cb.h>
#include <ui/Fence.h>
#include <ui/GraphicBufferAllocator.h>
#include <regex>
#include <typeinfo>
#include "utils/Errors.h"
#include <nativebase/nativebase.h>

using ::aidl::android::hardware::camera::common::CameraDeviceStatus;
using ::aidl::android::hardware::camera::common::TorchModeStatus;
using ::aidl::android::hardware::camera::device::CameraMetadata;
using ::aidl::android::hardware::camera::device::ICameraDevice;
using ::aidl::android::hardware::camera::metadata::CameraMetadataTag;
using ::aidl::android::hardware::camera::metadata::SensorInfoColorFilterArrangement;
using ::aidl::android::hardware::camera::metadata::SensorPixelMode;
using ::aidl::android::hardware::camera::provider::BnCameraProviderCallback;
using ::aidl::android::hardware::camera::provider::ConcurrentCameraIdCombination;
using ::aidl::android::hardware::camera::provider::ICameraProvider;
using ::aidl::android::hardware::common::NativeHandle;
using ::android::hardware::camera::common::V1_0::helper::Size;
using ::ndk::ScopedAStatus;
using ::ndk::SpAIBinder;

namespace {
namespace flags = com::android::internal::camera::flags;

bool parseProviderName(const std::string& serviceDescriptor, std::string* type /*out*/,
                       uint32_t* id /*out*/) {
    if (!type || !id) {
        ADD_FAILURE();
        return false;
    }

    // expected format: <service_name>/<type>/<id>
    std::string::size_type slashIdx1 = serviceDescriptor.find('/');
    if (slashIdx1 == std::string::npos || slashIdx1 == serviceDescriptor.size() - 1) {
        ADD_FAILURE() << "Provider name does not have / separator between name, type, and id";
        return false;
    }

    std::string::size_type slashIdx2 = serviceDescriptor.find('/', slashIdx1 + 1);
    if (slashIdx2 == std::string::npos || slashIdx2 == serviceDescriptor.size() - 1) {
        ADD_FAILURE() << "Provider name does not have / separator between type and id";
        return false;
    }

    std::string typeVal = serviceDescriptor.substr(slashIdx1 + 1, slashIdx2 - slashIdx1 - 1);

    char* endPtr;
    errno = 0;
    int64_t idVal = strtol(serviceDescriptor.c_str() + slashIdx2 + 1, &endPtr, 10);
    if (errno != 0) {
        ADD_FAILURE() << "cannot parse provider id as an integer:" << serviceDescriptor.c_str()
                      << strerror(errno) << errno;
        return false;
    }
    if (endPtr != serviceDescriptor.c_str() + serviceDescriptor.size()) {
        ADD_FAILURE() << "provider id has unexpected length " << serviceDescriptor.c_str();
        return false;
    }
    if (idVal < 0) {
        ADD_FAILURE() << "id is negative: " << serviceDescriptor.c_str() << idVal;
        return false;
    }

    *type = typeVal;
    *id = static_cast<uint32_t>(idVal);

    return true;
}

namespace flags = com::android::internal::camera::flags;

const std::vector<int64_t> kMandatoryUseCases = {
        ANDROID_SCALER_AVAILABLE_STREAM_USE_CASES_DEFAULT,
        ANDROID_SCALER_AVAILABLE_STREAM_USE_CASES_PREVIEW,
        ANDROID_SCALER_AVAILABLE_STREAM_USE_CASES_STILL_CAPTURE,
        ANDROID_SCALER_AVAILABLE_STREAM_USE_CASES_VIDEO_RECORD,
        ANDROID_SCALER_AVAILABLE_STREAM_USE_CASES_PREVIEW_VIDEO_STILL,
        ANDROID_SCALER_AVAILABLE_STREAM_USE_CASES_VIDEO_CALL};
}  // namespace

void CameraAidlTest::SetUp() {
    std::string serviceDescriptor = GetParam();
    ALOGI("get service with name: %s", serviceDescriptor.c_str());

    bool success = ABinderProcess_setThreadPoolMaxThreadCount(5);
    ALOGI("ABinderProcess_setThreadPoolMaxThreadCount returns %s", success ? "true" : "false");
    ASSERT_TRUE(success);
    ABinderProcess_startThreadPool();

    SpAIBinder cameraProviderBinder =
            SpAIBinder(AServiceManager_waitForService(serviceDescriptor.c_str()));
    ASSERT_NE(cameraProviderBinder.get(), nullptr);

    std::shared_ptr<ICameraProvider> cameraProvider =
            ICameraProvider::fromBinder(cameraProviderBinder);
    ASSERT_NE(cameraProvider.get(), nullptr);
    mProvider = cameraProvider;
    uint32_t id;
    ASSERT_TRUE(parseProviderName(serviceDescriptor, &mProviderType, &id));

    notifyDeviceState(ICameraProvider::DEVICE_STATE_NORMAL);
}

void CameraAidlTest::TearDown() {
    if (mSession != nullptr) {
        ndk::ScopedAStatus ret = mSession->close();
        ASSERT_TRUE(ret.isOk());
    }
}

void CameraAidlTest::waitForReleaseFence(
        std::vector<InFlightRequest::StreamBufferAndTimestamp>& resultOutputBuffers) {
    for (auto& bufferAndTimestamp : resultOutputBuffers) {
        // wait for the fence timestamp and store it along with the buffer
        android::sp<android::Fence> releaseFence = nullptr;
        const native_handle_t* releaseFenceHandle = bufferAndTimestamp.buffer.releaseFence;
        if (releaseFenceHandle != nullptr && releaseFenceHandle->numFds == 1 &&
            releaseFenceHandle->data[0] >= 0) {
            releaseFence = new android::Fence(dup(releaseFenceHandle->data[0]));
        }
        if (releaseFence && releaseFence->isValid()) {
            releaseFence->wait(/*ms*/ 300);
            nsecs_t releaseTime = releaseFence->getSignalTime();
            if (bufferAndTimestamp.timeStamp < releaseTime)
                bufferAndTimestamp.timeStamp = releaseTime;
        }
    }
}

std::vector<std::string> CameraAidlTest::getCameraDeviceNames(
        std::shared_ptr<ICameraProvider>& provider, bool addSecureOnly) {
    std::vector<std::string> cameraDeviceNames;

    ScopedAStatus ret = provider->getCameraIdList(&cameraDeviceNames);
    if (!ret.isOk()) {
        ADD_FAILURE() << "Could not get camera id list";
    }

    // External camera devices are reported through cameraDeviceStatusChange
    struct ProviderCb : public BnCameraProviderCallback {
        ScopedAStatus cameraDeviceStatusChange(const std::string& devName,
                                               CameraDeviceStatus newStatus) override {
            ALOGI("camera device status callback name %s, status %d", devName.c_str(),
                  (int)newStatus);
            if (newStatus == CameraDeviceStatus::PRESENT) {
                externalCameraDeviceNames.push_back(devName);
            }
            return ScopedAStatus::ok();
        }

        ScopedAStatus torchModeStatusChange(const std::string&, TorchModeStatus) override {
            return ScopedAStatus::ok();
        }

        ScopedAStatus physicalCameraDeviceStatusChange(
                const std::string&, const std::string&,
                ::aidl::android::hardware::camera::common::CameraDeviceStatus) override {
            return ScopedAStatus::ok();
        }

        std::vector<std::string> externalCameraDeviceNames;
    };
    std::shared_ptr<ProviderCb> cb = ndk::SharedRefBase::make<ProviderCb>();
    auto status = mProvider->setCallback(cb);

    for (const auto& devName : cb->externalCameraDeviceNames) {
        if (cameraDeviceNames.end() ==
            std::find(cameraDeviceNames.begin(), cameraDeviceNames.end(), devName)) {
            cameraDeviceNames.push_back(devName);
        }
    }

    std::vector<std::string> retList;
    for (auto& cameraDeviceName : cameraDeviceNames) {
        bool isSecureOnlyCamera = isSecureOnly(mProvider, cameraDeviceName);
        if (addSecureOnly) {
            if (isSecureOnlyCamera) {
                retList.emplace_back(cameraDeviceName);
            }
        } else if (!isSecureOnlyCamera) {
            retList.emplace_back(cameraDeviceName);
        }
    }
    return retList;
}

bool CameraAidlTest::isSecureOnly(const std::shared_ptr<ICameraProvider>& provider,
                                  const std::string& name) {
    std::shared_ptr<ICameraDevice> cameraDevice = nullptr;
    ScopedAStatus retInterface = provider->getCameraDeviceInterface(name, &cameraDevice);
    if (!retInterface.isOk()) {
        ADD_FAILURE() << "Failed to get camera device interface for " << name;
    }

    CameraMetadata cameraCharacteristics;
    ScopedAStatus retChars = cameraDevice->getCameraCharacteristics(&cameraCharacteristics);
    if (!retChars.isOk()) {
        ADD_FAILURE() << "Failed to get camera characteristics for device " << name;
    }

    camera_metadata_t* chars =
            reinterpret_cast<camera_metadata_t*>(cameraCharacteristics.metadata.data());

    SystemCameraKind systemCameraKind = SystemCameraKind::PUBLIC;
    Status retCameraKind = getSystemCameraKind(chars, &systemCameraKind);
    if (retCameraKind != Status::OK) {
        ADD_FAILURE() << "Failed to get camera kind for " << name;
    }

    return systemCameraKind == SystemCameraKind::HIDDEN_SECURE_CAMERA;
}

std::map<std::string, std::string> CameraAidlTest::getCameraDeviceIdToNameMap(
        std::shared_ptr<ICameraProvider> provider) {
    std::vector<std::string> cameraDeviceNames = getCameraDeviceNames(provider);

    std::map<std::string, std::string> idToNameMap;
    for (auto& name : cameraDeviceNames) {
        std::string version, cameraId;
        if (!matchDeviceName(name, mProviderType, &version, &cameraId)) {
            ADD_FAILURE();
        }
        idToNameMap.insert(std::make_pair(std::string(cameraId), name));
    }
    return idToNameMap;
}

void CameraAidlTest::verifyMonochromeCameraResult(
        const ::android::hardware::camera::common::V1_0::helper::CameraMetadata& metadata) {
    camera_metadata_ro_entry entry;

    // Check tags that are not applicable for monochrome camera
    ASSERT_FALSE(metadata.exists(ANDROID_SENSOR_GREEN_SPLIT));
    ASSERT_FALSE(metadata.exists(ANDROID_SENSOR_NEUTRAL_COLOR_POINT));
    ASSERT_FALSE(metadata.exists(ANDROID_COLOR_CORRECTION_MODE));
    ASSERT_FALSE(metadata.exists(ANDROID_COLOR_CORRECTION_TRANSFORM));
    ASSERT_FALSE(metadata.exists(ANDROID_COLOR_CORRECTION_GAINS));

    // Check dynamicBlackLevel
    entry = metadata.find(ANDROID_SENSOR_DYNAMIC_BLACK_LEVEL);
    if (entry.count > 0) {
        ASSERT_EQ(entry.count, 4);
        for (size_t i = 1; i < entry.count; i++) {
            ASSERT_FLOAT_EQ(entry.data.f[i], entry.data.f[0]);
        }
    }

    // Check noiseProfile
    entry = metadata.find(ANDROID_SENSOR_NOISE_PROFILE);
    if (entry.count > 0) {
        ASSERT_EQ(entry.count, 2);
    }

    // Check lensShadingMap
    entry = metadata.find(ANDROID_STATISTICS_LENS_SHADING_MAP);
    if (entry.count > 0) {
        ASSERT_EQ(entry.count % 4, 0);
        for (size_t i = 0; i < entry.count / 4; i++) {
            ASSERT_FLOAT_EQ(entry.data.f[i * 4 + 1], entry.data.f[i * 4]);
            ASSERT_FLOAT_EQ(entry.data.f[i * 4 + 2], entry.data.f[i * 4]);
            ASSERT_FLOAT_EQ(entry.data.f[i * 4 + 3], entry.data.f[i * 4]);
        }
    }

    // Check tonemapCurve
    camera_metadata_ro_entry curveRed = metadata.find(ANDROID_TONEMAP_CURVE_RED);
    camera_metadata_ro_entry curveGreen = metadata.find(ANDROID_TONEMAP_CURVE_GREEN);
    camera_metadata_ro_entry curveBlue = metadata.find(ANDROID_TONEMAP_CURVE_BLUE);
    if (curveRed.count > 0 && curveGreen.count > 0 && curveBlue.count > 0) {
        ASSERT_EQ(curveRed.count, curveGreen.count);
        ASSERT_EQ(curveRed.count, curveBlue.count);
        for (size_t i = 0; i < curveRed.count; i++) {
            ASSERT_FLOAT_EQ(curveGreen.data.f[i], curveRed.data.f[i]);
            ASSERT_FLOAT_EQ(curveBlue.data.f[i], curveRed.data.f[i]);
        }
    }
}

void CameraAidlTest::verifyStreamUseCaseCharacteristics(const camera_metadata_t* metadata) {
    camera_metadata_ro_entry entry;
    bool hasStreamUseCaseCap = supportsStreamUseCaseCap(metadata);

    bool supportMandatoryUseCases = false;
    int retcode = find_camera_metadata_ro_entry(metadata, ANDROID_SCALER_AVAILABLE_STREAM_USE_CASES,
                                                &entry);
    if ((0 == retcode) && (entry.count > 0)) {
        supportMandatoryUseCases = true;
        for (size_t i = 0; i < kMandatoryUseCases.size(); i++) {
            if (std::find(entry.data.i64, entry.data.i64 + entry.count, kMandatoryUseCases[i]) ==
                entry.data.i64 + entry.count) {
                supportMandatoryUseCases = false;
                break;
            }
        }
        bool supportDefaultUseCase = false;
        for (size_t i = 0; i < entry.count; i++) {
            if (entry.data.i64[i] == ANDROID_SCALER_AVAILABLE_STREAM_USE_CASES_DEFAULT) {
                supportDefaultUseCase = true;
            }
            ASSERT_TRUE(entry.data.i64[i] <= ANDROID_SCALER_AVAILABLE_STREAM_USE_CASES_CROPPED_RAW
                        || entry.data.i64[i] >=
                                ANDROID_SCALER_AVAILABLE_STREAM_USE_CASES_VENDOR_START);
        }
        ASSERT_TRUE(supportDefaultUseCase);
    }

    ASSERT_EQ(hasStreamUseCaseCap, supportMandatoryUseCases);
}

void CameraAidlTest::verifySettingsOverrideCharacteristics(const camera_metadata_t* metadata) {
    camera_metadata_ro_entry entry;
    int retcode = find_camera_metadata_ro_entry(metadata,
            ANDROID_CONTROL_AVAILABLE_SETTINGS_OVERRIDES, &entry);
    bool supportSettingsOverride = false;
    if (0 == retcode) {
        supportSettingsOverride = true;
        bool hasOff = false;
        for (size_t i = 0; i < entry.count; i++) {
            if (entry.data.u8[i] == ANDROID_CONTROL_SETTINGS_OVERRIDE_OFF) {
                hasOff = true;
            }
        }
        ASSERT_TRUE(hasOff);
    }

    // Check availableRequestKeys
    retcode = find_camera_metadata_ro_entry(metadata,
            ANDROID_REQUEST_AVAILABLE_REQUEST_KEYS, &entry);
    bool hasSettingsOverrideRequestKey = false;
    if ((0 == retcode) && (entry.count > 0)) {
        hasSettingsOverrideRequestKey =
                std::find(entry.data.i32, entry.data.i32 + entry.count,
                        ANDROID_CONTROL_SETTINGS_OVERRIDE) != entry.data.i32 + entry.count;
    } else {
        ADD_FAILURE() << "Get camera availableRequestKeys failed!";
    }

    // Check availableResultKeys
    retcode = find_camera_metadata_ro_entry(metadata,
            ANDROID_REQUEST_AVAILABLE_RESULT_KEYS, &entry);
    bool hasSettingsOverrideResultKey = false;
    bool hasOverridingFrameNumberKey = false;
    if ((0 == retcode) && (entry.count > 0)) {
        hasSettingsOverrideResultKey =
                std::find(entry.data.i32, entry.data.i32 + entry.count,
                        ANDROID_CONTROL_SETTINGS_OVERRIDE) != entry.data.i32 + entry.count;
        hasOverridingFrameNumberKey =
                std::find(entry.data.i32, entry.data.i32 + entry.count,
                        ANDROID_CONTROL_SETTINGS_OVERRIDING_FRAME_NUMBER)
                        != entry.data.i32 + entry.count;
    } else {
        ADD_FAILURE() << "Get camera availableResultKeys failed!";
    }

    // Check availableCharacteristicKeys
    retcode = find_camera_metadata_ro_entry(metadata,
            ANDROID_REQUEST_AVAILABLE_CHARACTERISTICS_KEYS, &entry);
    bool hasSettingsOverrideCharacteristicsKey= false;
    if ((0 == retcode) && (entry.count > 0)) {
        hasSettingsOverrideCharacteristicsKey = std::find(entry.data.i32,
                entry.data.i32 + entry.count, ANDROID_CONTROL_AVAILABLE_SETTINGS_OVERRIDES)
                        != entry.data.i32 + entry.count;
    } else {
        ADD_FAILURE() << "Get camera availableCharacteristicsKeys failed!";
    }

    ASSERT_EQ(supportSettingsOverride, hasSettingsOverrideRequestKey);
    ASSERT_EQ(supportSettingsOverride, hasSettingsOverrideResultKey);
    ASSERT_EQ(supportSettingsOverride, hasOverridingFrameNumberKey);
    ASSERT_EQ(supportSettingsOverride, hasSettingsOverrideCharacteristicsKey);
}

Status CameraAidlTest::isMonochromeCamera(const camera_metadata_t* staticMeta) {
    Status ret = Status::OPERATION_NOT_SUPPORTED;
    if (nullptr == staticMeta) {
        return Status::ILLEGAL_ARGUMENT;
    }

    camera_metadata_ro_entry entry;
    int rc = find_camera_metadata_ro_entry(staticMeta, ANDROID_REQUEST_AVAILABLE_CAPABILITIES,
                                           &entry);

    if (0 != rc) {
        return Status::ILLEGAL_ARGUMENT;
    }

    for (size_t i = 0; i < entry.count; i++) {
        if (ANDROID_REQUEST_AVAILABLE_CAPABILITIES_MONOCHROME == entry.data.u8[i]) {
            ret = Status::OK;
            break;
        }
    }

    return ret;
}

Status CameraAidlTest::isLogicalMultiCamera(const camera_metadata_t* staticMeta) {
    Status ret = Status::OPERATION_NOT_SUPPORTED;
    if (nullptr == staticMeta) {
        return Status::ILLEGAL_ARGUMENT;
    }

    camera_metadata_ro_entry entry;
    int rc = find_camera_metadata_ro_entry(staticMeta, ANDROID_REQUEST_AVAILABLE_CAPABILITIES,
                                           &entry);
    if (0 != rc) {
        return Status::ILLEGAL_ARGUMENT;
    }

    for (size_t i = 0; i < entry.count; i++) {
        if (ANDROID_REQUEST_AVAILABLE_CAPABILITIES_LOGICAL_MULTI_CAMERA == entry.data.u8[i]) {
            ret = Status::OK;
            break;
        }
    }

    return ret;
}

bool CameraAidlTest::isReadoutTimestampSupported(const camera_metadata_t* staticMeta) {
    camera_metadata_ro_entry readoutTimestampEntry;
    int rc = find_camera_metadata_ro_entry(staticMeta, ANDROID_SENSOR_READOUT_TIMESTAMP,
                                           &readoutTimestampEntry);
    if (rc != 0) {
        ALOGI("%s: Failed to find ANDROID_SENSOR_READOUT_TIMESTAMP", __FUNCTION__);
        return true;
    }
    if (readoutTimestampEntry.count == 1 && !readoutTimestampEntry.data.u8[0]) {
        ALOGI("%s: readout timestamp not supported", __FUNCTION__);
        return false;
    }
    ALOGI("%s: readout timestamp supported", __FUNCTION__);
    return true;
}

void CameraAidlTest::verifyLogicalCameraResult(const camera_metadata_t* staticMetadata,
                                               const std::vector<uint8_t>& resultMetadata) {
    camera_metadata_t* metadata = (camera_metadata_t*)resultMetadata.data();

    std::unordered_set<std::string> physicalIds;
    Status rc = getPhysicalCameraIds(staticMetadata, &physicalIds);
    ASSERT_TRUE(Status::OK == rc);
    ASSERT_TRUE(physicalIds.size() > 1);

    camera_metadata_ro_entry entry;
    // Check mainPhysicalId
    find_camera_metadata_ro_entry(metadata, ANDROID_LOGICAL_MULTI_CAMERA_ACTIVE_PHYSICAL_ID,
                                  &entry);
    if (entry.count > 0) {
        std::string mainPhysicalId(reinterpret_cast<const char*>(entry.data.u8));
        ASSERT_NE(physicalIds.find(mainPhysicalId), physicalIds.end());
    } else {
        ADD_FAILURE() << "Get LOGICAL_MULTI_CAMERA_ACTIVE_PHYSICAL_ID failed!";
    }

    if (flags::concert_mode()) {
        auto ret = find_camera_metadata_ro_entry(
                metadata, ANDROID_LOGICAL_MULTI_CAMERA_ACTIVE_PHYSICAL_SENSOR_CROP_REGION, &entry);
        if ((ret == android::OK) && (entry.count > 0)) {
            ASSERT_TRUE(entry.count == 4);
            ASSERT_GE(entry.data.i32[0], 0);  // Top must be non-negative
            ASSERT_GE(entry.data.i32[1], 0);  // Left must be non-negative
            ASSERT_GT(entry.data.i32[2], 0);  // Width must be positive
            ASSERT_GT(entry.data.i32[3], 0);  // Height must be positive
        }
    }
}

void CameraAidlTest::verifyLensIntrinsicsResult(const std::vector<uint8_t>& resultMetadata) {
    if (flags::concert_mode()) {
        camera_metadata_t* metadata = (camera_metadata_t*)resultMetadata.data();

        camera_metadata_ro_entry timestampsEntry, intrinsicsEntry;
        auto tsRet = find_camera_metadata_ro_entry(
                metadata, ANDROID_STATISTICS_LENS_INTRINSIC_TIMESTAMPS, &timestampsEntry);
        auto inRet = find_camera_metadata_ro_entry(
                metadata, ANDROID_STATISTICS_LENS_INTRINSIC_SAMPLES, &intrinsicsEntry);
        ASSERT_EQ(tsRet, inRet);
        ASSERT_TRUE((intrinsicsEntry.count % 5) == 0);
        ASSERT_EQ(timestampsEntry.count, intrinsicsEntry.count / 5);
        if (timestampsEntry.count > 0) {
            for (size_t i = 0; i < timestampsEntry.count - 1; i++) {
                ASSERT_GE(timestampsEntry.data.i64[i + 1], timestampsEntry.data.i64[i]);
            }
        }
    }
}

Status CameraAidlTest::getPhysicalCameraIds(const camera_metadata_t* staticMeta,
                                            std::unordered_set<std::string>* physicalIds) {
    if ((nullptr == staticMeta) || (nullptr == physicalIds)) {
        return Status::ILLEGAL_ARGUMENT;
    }

    camera_metadata_ro_entry entry;
    int rc = find_camera_metadata_ro_entry(staticMeta, ANDROID_LOGICAL_MULTI_CAMERA_PHYSICAL_IDS,
                                           &entry);
    if (0 != rc) {
        return Status::ILLEGAL_ARGUMENT;
    }

    const uint8_t* ids = entry.data.u8;
    size_t start = 0;
    for (size_t i = 0; i < entry.count; i++) {
        if (ids[i] == '\0') {
            if (start != i) {
                std::string currentId(reinterpret_cast<const char*>(ids + start));
                physicalIds->emplace(currentId);
            }
            start = i + 1;
        }
    }

    return Status::OK;
}

Status CameraAidlTest::getSystemCameraKind(const camera_metadata_t* staticMeta,
                                           SystemCameraKind* systemCameraKind) {
    if (nullptr == staticMeta || nullptr == systemCameraKind) {
        return Status::ILLEGAL_ARGUMENT;
    }

    camera_metadata_ro_entry entry{};
    int rc = find_camera_metadata_ro_entry(staticMeta, ANDROID_REQUEST_AVAILABLE_CAPABILITIES,
                                           &entry);
    if (0 != rc) {
        return Status::ILLEGAL_ARGUMENT;
    }

    if (entry.count == 1 &&
        entry.data.u8[0] == ANDROID_REQUEST_AVAILABLE_CAPABILITIES_SECURE_IMAGE_DATA) {
        *systemCameraKind = SystemCameraKind::HIDDEN_SECURE_CAMERA;
        return Status::OK;
    }

    // Go through the capabilities and check if it has
    // ANDROID_REQUEST_AVAILABLE_CAPABILITIES_SYSTEM_CAMERA
    for (size_t i = 0; i < entry.count; ++i) {
        uint8_t capability = entry.data.u8[i];
        if (capability == ANDROID_REQUEST_AVAILABLE_CAPABILITIES_SYSTEM_CAMERA) {
            *systemCameraKind = SystemCameraKind::SYSTEM_ONLY_CAMERA;
            return Status::OK;
        }
    }
    *systemCameraKind = SystemCameraKind::PUBLIC;
    return Status::OK;
}

void CameraAidlTest::notifyDeviceState(int64_t state) {
    if (mProvider == nullptr) {
        return;
    }
    mProvider->notifyDeviceStateChange(state);
}

void CameraAidlTest::allocateGraphicBuffer(uint32_t width, uint32_t height, uint64_t usage,
                                           PixelFormat format, buffer_handle_t* buffer_handle) {
    ASSERT_NE(buffer_handle, nullptr);

    uint32_t stride;

    android::status_t err = android::GraphicBufferAllocator::get().allocateRawHandle(
            width, height, static_cast<int32_t>(format), 1u /*layerCount*/, usage, buffer_handle,
            &stride, "VtsHalCameraProviderV2");
    ASSERT_EQ(err, android::NO_ERROR);
}

bool CameraAidlTest::matchDeviceName(const std::string& deviceName, const std::string& providerType,
                                     std::string* deviceVersion, std::string* cameraId) {
    // expected format: device@<major>.<minor>/<type>/<id>
    std::stringstream pattern;
    pattern << "device@([0-9]+\\.[0-9]+)/" << providerType << "/(.+)";
    std::regex e(pattern.str());

    std::smatch sm;
    if (std::regex_match(deviceName, sm, e)) {
        if (deviceVersion != nullptr) {
            *deviceVersion = sm[1];
        }
        if (cameraId != nullptr) {
            *cameraId = sm[2];
        }
        return true;
    }
    return false;
}

void CameraAidlTest::verifyCameraCharacteristics(const CameraMetadata& chars) {
    const camera_metadata_t* metadata =
            reinterpret_cast<const camera_metadata_t*>(chars.metadata.data());

    size_t expectedSize = chars.metadata.size();
    int result = validate_camera_metadata_structure(metadata, &expectedSize);
    ASSERT_TRUE((result == 0) || (result == CAMERA_METADATA_VALIDATION_SHIFTED));
    size_t entryCount = get_camera_metadata_entry_count(metadata);
    // TODO: we can do better than 0 here. Need to check how many required
    // characteristics keys we've defined.
    ASSERT_GT(entryCount, 0u);

    camera_metadata_ro_entry entry;
    int retcode =
            find_camera_metadata_ro_entry(metadata, ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL, &entry);
    if ((0 == retcode) && (entry.count > 0)) {
        uint8_t hardwareLevel = entry.data.u8[0];
        ASSERT_TRUE(hardwareLevel == ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_LIMITED ||
                    hardwareLevel == ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_FULL ||
                    hardwareLevel == ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_3 ||
                    hardwareLevel == ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_EXTERNAL);
    } else {
        ADD_FAILURE() << "Get camera hardware level failed!";
    }

    entry.count = 0;
    retcode = find_camera_metadata_ro_entry(
            metadata, ANDROID_REQUEST_CHARACTERISTIC_KEYS_NEEDING_PERMISSION, &entry);
    if ((0 == retcode) || (entry.count > 0)) {
        ADD_FAILURE() << "ANDROID_REQUEST_CHARACTERISTIC_KEYS_NEEDING_PERMISSION "
                      << " per API contract should never be set by Hal!";
    }
    retcode = find_camera_metadata_ro_entry(
            metadata, ANDROID_DEPTH_AVAILABLE_DYNAMIC_DEPTH_STREAM_CONFIGURATIONS, &entry);
    if ((0 == retcode) || (entry.count > 0)) {
        ADD_FAILURE() << "ANDROID_DEPTH_AVAILABLE_DYNAMIC_DEPTH_STREAM_CONFIGURATIONS"
                      << " per API contract should never be set by Hal!";
    }
    retcode = find_camera_metadata_ro_entry(
            metadata, ANDROID_DEPTH_AVAILABLE_DYNAMIC_DEPTH_MIN_FRAME_DURATIONS, &entry);
    if ((0 == retcode) || (entry.count > 0)) {
        ADD_FAILURE() << "ANDROID_DEPTH_AVAILABLE_DYNAMIC_DEPTH_MIN_FRAME_DURATIONS"
                      << " per API contract should never be set by Hal!";
    }
    retcode = find_camera_metadata_ro_entry(
            metadata, ANDROID_DEPTH_AVAILABLE_DYNAMIC_DEPTH_STALL_DURATIONS, &entry);
    if ((0 == retcode) || (entry.count > 0)) {
        ADD_FAILURE() << "ANDROID_DEPTH_AVAILABLE_DYNAMIC_DEPTH_STALL_DURATIONS"
                      << " per API contract should never be set by Hal!";
    }

    retcode = find_camera_metadata_ro_entry(
            metadata, ANDROID_HEIC_AVAILABLE_HEIC_STREAM_CONFIGURATIONS, &entry);
    if (0 == retcode || entry.count > 0) {
        ADD_FAILURE() << "ANDROID_HEIC_AVAILABLE_HEIC_STREAM_CONFIGURATIONS "
                      << " per API contract should never be set by Hal!";
    }

    retcode = find_camera_metadata_ro_entry(
            metadata, ANDROID_HEIC_AVAILABLE_HEIC_MIN_FRAME_DURATIONS, &entry);
    if (0 == retcode || entry.count > 0) {
        ADD_FAILURE() << "ANDROID_HEIC_AVAILABLE_HEIC_MIN_FRAME_DURATIONS "
                      << " per API contract should never be set by Hal!";
    }

    retcode = find_camera_metadata_ro_entry(metadata, ANDROID_HEIC_AVAILABLE_HEIC_STALL_DURATIONS,
                                            &entry);
    if (0 == retcode || entry.count > 0) {
        ADD_FAILURE() << "ANDROID_HEIC_AVAILABLE_HEIC_STALL_DURATIONS "
                      << " per API contract should never be set by Hal!";
    }

    retcode = find_camera_metadata_ro_entry(metadata, ANDROID_HEIC_INFO_SUPPORTED, &entry);
    if (0 == retcode && entry.count > 0) {
        retcode = find_camera_metadata_ro_entry(
                metadata, ANDROID_HEIC_INFO_MAX_JPEG_APP_SEGMENTS_COUNT, &entry);
        if (0 == retcode && entry.count > 0) {
            uint8_t maxJpegAppSegmentsCount = entry.data.u8[0];
            ASSERT_TRUE(maxJpegAppSegmentsCount >= 1 && maxJpegAppSegmentsCount <= 16);
        } else {
            ADD_FAILURE() << "Get Heic maxJpegAppSegmentsCount failed!";
        }
    }

    retcode = find_camera_metadata_ro_entry(metadata, ANDROID_LENS_POSE_REFERENCE, &entry);
    if (0 == retcode && entry.count > 0) {
        uint8_t poseReference = entry.data.u8[0];
        ASSERT_TRUE(poseReference <= ANDROID_LENS_POSE_REFERENCE_AUTOMOTIVE &&
                poseReference >= ANDROID_LENS_POSE_REFERENCE_PRIMARY_CAMERA);
    }

    retcode =
            find_camera_metadata_ro_entry(metadata, ANDROID_INFO_DEVICE_STATE_ORIENTATIONS, &entry);
    if (0 == retcode && entry.count > 0) {
        ASSERT_TRUE((entry.count % 2) == 0);
        uint64_t maxPublicState = ((uint64_t)ICameraProvider::DEVICE_STATE_FOLDED) << 1;
        uint64_t vendorStateStart = 1UL << 31;  // Reserved for vendor specific states
        uint64_t stateMask = (1 << vendorStateStart) - 1;
        stateMask &= ~((1 << maxPublicState) - 1);
        for (int i = 0; i < entry.count; i += 2) {
            ASSERT_TRUE((entry.data.i64[i] & stateMask) == 0);
            ASSERT_TRUE((entry.data.i64[i + 1] % 90) == 0);
        }
    }

    verifyExtendedSceneModeCharacteristics(metadata);
    verifyZoomCharacteristics(metadata);
    verifyStreamUseCaseCharacteristics(metadata);
    verifySettingsOverrideCharacteristics(metadata);
}

void CameraAidlTest::verifyExtendedSceneModeCharacteristics(const camera_metadata_t* metadata) {
    camera_metadata_ro_entry entry;
    int retcode = 0;

    retcode = find_camera_metadata_ro_entry(metadata, ANDROID_CONTROL_AVAILABLE_MODES, &entry);
    if ((0 == retcode) && (entry.count > 0)) {
        for (auto i = 0; i < entry.count; i++) {
            ASSERT_TRUE(entry.data.u8[i] >= ANDROID_CONTROL_MODE_OFF &&
                        entry.data.u8[i] <= ANDROID_CONTROL_MODE_USE_EXTENDED_SCENE_MODE);
        }
    } else {
        ADD_FAILURE() << "Get camera controlAvailableModes failed!";
    }

    // Check key availability in capabilities, request and result.

    retcode =
            find_camera_metadata_ro_entry(metadata, ANDROID_REQUEST_AVAILABLE_REQUEST_KEYS, &entry);
    bool hasExtendedSceneModeRequestKey = false;
    if ((0 == retcode) && (entry.count > 0)) {
        hasExtendedSceneModeRequestKey =
                std::find(entry.data.i32, entry.data.i32 + entry.count,
                          ANDROID_CONTROL_EXTENDED_SCENE_MODE) != entry.data.i32 + entry.count;
    } else {
        ADD_FAILURE() << "Get camera availableRequestKeys failed!";
    }

    retcode =
            find_camera_metadata_ro_entry(metadata, ANDROID_REQUEST_AVAILABLE_RESULT_KEYS, &entry);
    bool hasExtendedSceneModeResultKey = false;
    if ((0 == retcode) && (entry.count > 0)) {
        hasExtendedSceneModeResultKey =
                std::find(entry.data.i32, entry.data.i32 + entry.count,
                          ANDROID_CONTROL_EXTENDED_SCENE_MODE) != entry.data.i32 + entry.count;
    } else {
        ADD_FAILURE() << "Get camera availableResultKeys failed!";
    }

    retcode = find_camera_metadata_ro_entry(metadata,
                                            ANDROID_REQUEST_AVAILABLE_CHARACTERISTICS_KEYS, &entry);
    bool hasExtendedSceneModeMaxSizesKey = false;
    bool hasExtendedSceneModeZoomRatioRangesKey = false;
    if ((0 == retcode) && (entry.count > 0)) {
        hasExtendedSceneModeMaxSizesKey =
                std::find(entry.data.i32, entry.data.i32 + entry.count,
                          ANDROID_CONTROL_AVAILABLE_EXTENDED_SCENE_MODE_MAX_SIZES) !=
                entry.data.i32 + entry.count;
        hasExtendedSceneModeZoomRatioRangesKey =
                std::find(entry.data.i32, entry.data.i32 + entry.count,
                          ANDROID_CONTROL_AVAILABLE_EXTENDED_SCENE_MODE_ZOOM_RATIO_RANGES) !=
                entry.data.i32 + entry.count;
    } else {
        ADD_FAILURE() << "Get camera availableCharacteristicsKeys failed!";
    }

    camera_metadata_ro_entry maxSizesEntry;
    retcode = find_camera_metadata_ro_entry(
            metadata, ANDROID_CONTROL_AVAILABLE_EXTENDED_SCENE_MODE_MAX_SIZES, &maxSizesEntry);
    bool hasExtendedSceneModeMaxSizes = (0 == retcode && maxSizesEntry.count > 0);

    camera_metadata_ro_entry zoomRatioRangesEntry;
    retcode = find_camera_metadata_ro_entry(
            metadata, ANDROID_CONTROL_AVAILABLE_EXTENDED_SCENE_MODE_ZOOM_RATIO_RANGES,
            &zoomRatioRangesEntry);
    bool hasExtendedSceneModeZoomRatioRanges = (0 == retcode && zoomRatioRangesEntry.count > 0);

    // Extended scene mode keys must all be available, or all be unavailable.
    bool noExtendedSceneMode =
            !hasExtendedSceneModeRequestKey && !hasExtendedSceneModeResultKey &&
            !hasExtendedSceneModeMaxSizesKey && !hasExtendedSceneModeZoomRatioRangesKey &&
            !hasExtendedSceneModeMaxSizes && !hasExtendedSceneModeZoomRatioRanges;
    if (noExtendedSceneMode) {
        return;
    }
    bool hasExtendedSceneMode = hasExtendedSceneModeRequestKey && hasExtendedSceneModeResultKey &&
                                hasExtendedSceneModeMaxSizesKey &&
                                hasExtendedSceneModeZoomRatioRangesKey &&
                                hasExtendedSceneModeMaxSizes && hasExtendedSceneModeZoomRatioRanges;
    ASSERT_TRUE(hasExtendedSceneMode);

    // Must have DISABLED, and must have one of BOKEH_STILL_CAPTURE, BOKEH_CONTINUOUS, or a VENDOR
    // mode.
    ASSERT_TRUE((maxSizesEntry.count == 6 && zoomRatioRangesEntry.count == 2) ||
                (maxSizesEntry.count == 9 && zoomRatioRangesEntry.count == 4));
    bool hasDisabledMode = false;
    bool hasBokehStillCaptureMode = false;
    bool hasBokehContinuousMode = false;
    bool hasVendorMode = false;
    std::vector<AvailableStream> outputStreams;
    ASSERT_EQ(Status::OK, getAvailableOutputStreams(metadata, outputStreams));
    for (int i = 0, j = 0; i < maxSizesEntry.count && j < zoomRatioRangesEntry.count; i += 3) {
        int32_t mode = maxSizesEntry.data.i32[i];
        int32_t maxWidth = maxSizesEntry.data.i32[i + 1];
        int32_t maxHeight = maxSizesEntry.data.i32[i + 2];
        switch (mode) {
            case ANDROID_CONTROL_EXTENDED_SCENE_MODE_DISABLED:
                hasDisabledMode = true;
                ASSERT_TRUE(maxWidth == 0 && maxHeight == 0);
                break;
            case ANDROID_CONTROL_EXTENDED_SCENE_MODE_BOKEH_STILL_CAPTURE:
                hasBokehStillCaptureMode = true;
                j += 2;
                break;
            case ANDROID_CONTROL_EXTENDED_SCENE_MODE_BOKEH_CONTINUOUS:
                hasBokehContinuousMode = true;
                j += 2;
                break;
            default:
                if (mode < ANDROID_CONTROL_EXTENDED_SCENE_MODE_VENDOR_START) {
                    ADD_FAILURE() << "Invalid extended scene mode advertised: " << mode;
                } else {
                    hasVendorMode = true;
                    j += 2;
                }
                break;
        }

        if (mode != ANDROID_CONTROL_EXTENDED_SCENE_MODE_DISABLED) {
            // Make sure size is supported.
            bool sizeSupported = false;
            for (const auto& stream : outputStreams) {
                if ((stream.format == static_cast<int32_t>(PixelFormat::YCBCR_420_888) ||
                     stream.format == static_cast<int32_t>(PixelFormat::IMPLEMENTATION_DEFINED)) &&
                    stream.width == maxWidth && stream.height == maxHeight) {
                    sizeSupported = true;
                    break;
                }
            }
            ASSERT_TRUE(sizeSupported);

            // Make sure zoom range is valid
            float minZoomRatio = zoomRatioRangesEntry.data.f[0];
            float maxZoomRatio = zoomRatioRangesEntry.data.f[1];
            ASSERT_GT(minZoomRatio, 0.0f);
            ASSERT_LE(minZoomRatio, maxZoomRatio);
        }
    }
    ASSERT_TRUE(hasDisabledMode);
    ASSERT_TRUE(hasBokehStillCaptureMode || hasBokehContinuousMode || hasVendorMode);
}

void CameraAidlTest::verifyHighSpeedRecordingCharacteristics(const std::string& cameraName,
                                                             const CameraMetadata& chars) {
    const camera_metadata_t* metadata =
            reinterpret_cast<const camera_metadata_t*>(chars.metadata.data());

    // Check capabilities
    bool hasHighSpeedRecordingCapability = false;
    bool hasUltraHighResolutionCapability = false;
    camera_metadata_ro_entry entry;
    int rc =
            find_camera_metadata_ro_entry(metadata, ANDROID_REQUEST_AVAILABLE_CAPABILITIES, &entry);
    if ((0 == rc) && (entry.count > 0)) {
        hasHighSpeedRecordingCapability =
                std::find(entry.data.u8, entry.data.u8 + entry.count,
                          ANDROID_REQUEST_AVAILABLE_CAPABILITIES_CONSTRAINED_HIGH_SPEED_VIDEO) !=
                entry.data.u8 + entry.count;

        hasUltraHighResolutionCapability =
                std::find(entry.data.u8, entry.data.u8 + entry.count,
                          ANDROID_REQUEST_AVAILABLE_CAPABILITIES_ULTRA_HIGH_RESOLUTION_SENSOR) !=
                entry.data.u8 + entry.count;
    }

    // Check high speed video configurations
    camera_metadata_ro_entry highSpeedEntry;
    rc = find_camera_metadata_ro_entry(
            metadata, ANDROID_CONTROL_AVAILABLE_HIGH_SPEED_VIDEO_CONFIGURATIONS, &highSpeedEntry);
    bool hasHighSpeedEntry = (0 == rc && highSpeedEntry.count > 0);

    camera_metadata_ro_entry highSpeedMaxResEntry;
    rc = find_camera_metadata_ro_entry(
            metadata, ANDROID_CONTROL_AVAILABLE_HIGH_SPEED_VIDEO_CONFIGURATIONS_MAXIMUM_RESOLUTION,
            &highSpeedMaxResEntry);
    bool hasHighSpeedMaxResEntry = (0 == rc && highSpeedMaxResEntry.count > 0);

    // High speed recording configuration entry must be available based on capabilities
    bool noHighSpeedRecording =
            !hasHighSpeedRecordingCapability && !hasHighSpeedEntry && !hasHighSpeedMaxResEntry;
    if (noHighSpeedRecording) {
        return;
    }
    bool hasHighSpeedRecording = hasHighSpeedRecordingCapability && hasHighSpeedEntry &&
                                 ((hasHighSpeedMaxResEntry && hasUltraHighResolutionCapability) ||
                                  !hasHighSpeedMaxResEntry);
    ASSERT_TRUE(hasHighSpeedRecording);

    std::string version, cameraId;
    ASSERT_TRUE(matchDeviceName(cameraName, mProviderType, &version, &cameraId));
    bool needBatchSizeCheck = (version != CAMERA_DEVICE_API_VERSION_1);

    // Check each entry item
    ASSERT_TRUE(highSpeedEntry.count > 0 && highSpeedEntry.count % 5 == 0);
    for (auto i = 4; i < highSpeedEntry.count; i += 5) {
        int32_t fps_min = highSpeedEntry.data.i32[i - 2];
        int32_t fps_max = highSpeedEntry.data.i32[i - 1];
        int32_t batch_size_max = highSpeedEntry.data.i32[i];
        int32_t allowedMaxBatchSize = fps_max / 30;

        ASSERT_GE(fps_max, 120);
        ASSERT_TRUE(fps_min % 30 == 0 && fps_max % 30 == 0);
        if (needBatchSizeCheck) {
            ASSERT_LE(batch_size_max, 32);
            ASSERT_TRUE(allowedMaxBatchSize % batch_size_max == 0);
        }
    }

    if (hasHighSpeedMaxResEntry) {
        ASSERT_TRUE(highSpeedMaxResEntry.count > 0 && highSpeedMaxResEntry.count % 5 == 0);
        for (auto i = 4; i < highSpeedMaxResEntry.count; i += 5) {
            int32_t fps_min = highSpeedMaxResEntry.data.i32[i - 2];
            int32_t fps_max = highSpeedMaxResEntry.data.i32[i - 1];
            int32_t batch_size_max = highSpeedMaxResEntry.data.i32[i];
            int32_t allowedMaxBatchSize = fps_max / 30;

            ASSERT_GE(fps_max, 120);
            ASSERT_TRUE(fps_min % 30 == 0 && fps_max % 30 == 0);
            if (needBatchSizeCheck) {
                ASSERT_LE(batch_size_max, 32);
                ASSERT_TRUE(allowedMaxBatchSize % batch_size_max == 0);
            }
        }
    }
}

Status CameraAidlTest::getAvailableOutputStreams(const camera_metadata_t* staticMeta,
                                                 std::vector<AvailableStream>& outputStreams,
                                                 const AvailableStream* threshold,
                                                 bool maxResolution) {
    if (nullptr == staticMeta) {
        return Status::ILLEGAL_ARGUMENT;
    }
    int scalerTag = maxResolution
                            ? ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_MAXIMUM_RESOLUTION
                            : ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS;
    int depthTag = maxResolution
                           ? ANDROID_DEPTH_AVAILABLE_DEPTH_STREAM_CONFIGURATIONS_MAXIMUM_RESOLUTION
                           : ANDROID_DEPTH_AVAILABLE_DEPTH_STREAM_CONFIGURATIONS;

    camera_metadata_ro_entry scalerEntry;
    camera_metadata_ro_entry depthEntry;
    int foundScaler = find_camera_metadata_ro_entry(staticMeta, scalerTag, &scalerEntry);
    int foundDepth = find_camera_metadata_ro_entry(staticMeta, depthTag, &depthEntry);
    if ((0 != foundScaler || (0 != (scalerEntry.count % 4))) &&
        (0 != foundDepth || (0 != (depthEntry.count % 4)))) {
        return Status::ILLEGAL_ARGUMENT;
    }

    if (foundScaler == 0 && (0 == (scalerEntry.count % 4))) {
        fillOutputStreams(&scalerEntry, outputStreams, threshold,
                          ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT);
    }

    if (foundDepth == 0 && (0 == (depthEntry.count % 4))) {
        AvailableStream depthPreviewThreshold = {kMaxPreviewWidth, kMaxPreviewHeight,
                                                 static_cast<int32_t>(PixelFormat::Y16)};
        const AvailableStream* depthThreshold =
                isDepthOnly(staticMeta) ? &depthPreviewThreshold : threshold;
        fillOutputStreams(&depthEntry, outputStreams, depthThreshold,
                          ANDROID_DEPTH_AVAILABLE_DEPTH_STREAM_CONFIGURATIONS_OUTPUT);
    }

    return Status::OK;
}

void CameraAidlTest::fillOutputStreams(camera_metadata_ro_entry_t* entry,
                                       std::vector<AvailableStream>& outputStreams,
                                       const AvailableStream* threshold,
                                       const int32_t availableConfigOutputTag) {
    for (size_t i = 0; i < entry->count; i += 4) {
        if (availableConfigOutputTag == entry->data.i32[i + 3]) {
            if (nullptr == threshold) {
                AvailableStream s = {entry->data.i32[i + 1], entry->data.i32[i + 2],
                                     entry->data.i32[i]};
                outputStreams.push_back(s);
            } else {
                if ((threshold->format == entry->data.i32[i]) &&
                    (threshold->width >= entry->data.i32[i + 1]) &&
                    (threshold->height >= entry->data.i32[i + 2])) {
                    AvailableStream s = {entry->data.i32[i + 1], entry->data.i32[i + 2],
                                         threshold->format};
                    outputStreams.push_back(s);
                }
            }
        }
    }
}

void CameraAidlTest::verifyZoomCharacteristics(const camera_metadata_t* metadata) {
    camera_metadata_ro_entry entry;
    int retcode = 0;

    // Check key availability in capabilities, request and result.
    retcode = find_camera_metadata_ro_entry(metadata, ANDROID_SCALER_AVAILABLE_MAX_DIGITAL_ZOOM,
                                            &entry);
    float maxDigitalZoom = 1.0;
    if ((0 == retcode) && (entry.count == 1)) {
        maxDigitalZoom = entry.data.f[0];
    } else {
        ADD_FAILURE() << "Get camera scalerAvailableMaxDigitalZoom failed!";
    }

    retcode =
            find_camera_metadata_ro_entry(metadata, ANDROID_REQUEST_AVAILABLE_REQUEST_KEYS, &entry);
    bool hasZoomRequestKey = false;
    if ((0 == retcode) && (entry.count > 0)) {
        hasZoomRequestKey = std::find(entry.data.i32, entry.data.i32 + entry.count,
                                      ANDROID_CONTROL_ZOOM_RATIO) != entry.data.i32 + entry.count;
    } else {
        ADD_FAILURE() << "Get camera availableRequestKeys failed!";
    }

    retcode =
            find_camera_metadata_ro_entry(metadata, ANDROID_REQUEST_AVAILABLE_RESULT_KEYS, &entry);
    bool hasZoomResultKey = false;
    if ((0 == retcode) && (entry.count > 0)) {
        hasZoomResultKey = std::find(entry.data.i32, entry.data.i32 + entry.count,
                                     ANDROID_CONTROL_ZOOM_RATIO) != entry.data.i32 + entry.count;
    } else {
        ADD_FAILURE() << "Get camera availableResultKeys failed!";
    }

    retcode = find_camera_metadata_ro_entry(metadata,
                                            ANDROID_REQUEST_AVAILABLE_CHARACTERISTICS_KEYS, &entry);
    bool hasZoomCharacteristicsKey = false;
    if ((0 == retcode) && (entry.count > 0)) {
        hasZoomCharacteristicsKey =
                std::find(entry.data.i32, entry.data.i32 + entry.count,
                          ANDROID_CONTROL_ZOOM_RATIO_RANGE) != entry.data.i32 + entry.count;
    } else {
        ADD_FAILURE() << "Get camera availableCharacteristicsKeys failed!";
    }

    retcode = find_camera_metadata_ro_entry(metadata, ANDROID_CONTROL_ZOOM_RATIO_RANGE, &entry);
    bool hasZoomRatioRange = (0 == retcode && entry.count == 2);

    // Zoom keys must all be available, or all be unavailable.
    bool noZoomRatio = !hasZoomRequestKey && !hasZoomResultKey && !hasZoomCharacteristicsKey &&
                       !hasZoomRatioRange;
    if (noZoomRatio) {
        return;
    }
    bool hasZoomRatio =
            hasZoomRequestKey && hasZoomResultKey && hasZoomCharacteristicsKey && hasZoomRatioRange;
    ASSERT_TRUE(hasZoomRatio);

    float minZoomRatio = entry.data.f[0];
    float maxZoomRatio = entry.data.f[1];
    constexpr float FLOATING_POINT_THRESHOLD = 0.00001f;
    if (maxDigitalZoom > maxZoomRatio + FLOATING_POINT_THRESHOLD) {
        ADD_FAILURE() << "Maximum digital zoom " << maxDigitalZoom
                      << " is larger than maximum zoom ratio " << maxZoomRatio << " + threshold "
                      << FLOATING_POINT_THRESHOLD << "!";
    }
    if (minZoomRatio > maxZoomRatio) {
        ADD_FAILURE() << "Maximum zoom ratio is less than minimum zoom ratio!";
    }
    if (minZoomRatio > 1.0f) {
        ADD_FAILURE() << "Minimum zoom ratio is more than 1.0!";
    }
    if (maxZoomRatio < 1.0f) {
        ADD_FAILURE() << "Maximum zoom ratio is less than 1.0!";
    }

    // Make sure CROPPING_TYPE is CENTER_ONLY
    retcode = find_camera_metadata_ro_entry(metadata, ANDROID_SCALER_CROPPING_TYPE, &entry);
    if ((0 == retcode) && (entry.count == 1)) {
        int8_t croppingType = entry.data.u8[0];
        ASSERT_EQ(croppingType, ANDROID_SCALER_CROPPING_TYPE_CENTER_ONLY);
    } else {
        ADD_FAILURE() << "Get camera scalerCroppingType failed!";
    }
}

void CameraAidlTest::verifyMonochromeCharacteristics(const CameraMetadata& chars) {
    const camera_metadata_t* metadata = (camera_metadata_t*)chars.metadata.data();
    Status rc = isMonochromeCamera(metadata);
    if (Status::OPERATION_NOT_SUPPORTED == rc) {
        return;
    }
    ASSERT_EQ(Status::OK, rc);

    camera_metadata_ro_entry entry;
    // Check capabilities
    int retcode =
            find_camera_metadata_ro_entry(metadata, ANDROID_REQUEST_AVAILABLE_CAPABILITIES, &entry);
    if ((0 == retcode) && (entry.count > 0)) {
        ASSERT_EQ(std::find(entry.data.u8, entry.data.u8 + entry.count,
                            ANDROID_REQUEST_AVAILABLE_CAPABILITIES_MANUAL_POST_PROCESSING),
                  entry.data.u8 + entry.count);
    }

    // Check Cfa
    retcode = find_camera_metadata_ro_entry(metadata, ANDROID_SENSOR_INFO_COLOR_FILTER_ARRANGEMENT,
                                            &entry);
    if ((0 == retcode) && (entry.count == 1)) {
        ASSERT_TRUE(entry.data.i32[0] ==
                            static_cast<int32_t>(
                                    SensorInfoColorFilterArrangement::
                                            ANDROID_SENSOR_INFO_COLOR_FILTER_ARRANGEMENT_MONO) ||
                    entry.data.i32[0] ==
                            static_cast<int32_t>(
                                    SensorInfoColorFilterArrangement::
                                            ANDROID_SENSOR_INFO_COLOR_FILTER_ARRANGEMENT_NIR));
    }

    // Check availableRequestKeys
    retcode =
            find_camera_metadata_ro_entry(metadata, ANDROID_REQUEST_AVAILABLE_REQUEST_KEYS, &entry);
    if ((0 == retcode) && (entry.count > 0)) {
        for (size_t i = 0; i < entry.count; i++) {
            ASSERT_NE(entry.data.i32[i], ANDROID_COLOR_CORRECTION_MODE);
            ASSERT_NE(entry.data.i32[i], ANDROID_COLOR_CORRECTION_TRANSFORM);
            ASSERT_NE(entry.data.i32[i], ANDROID_COLOR_CORRECTION_GAINS);
        }
    } else {
        ADD_FAILURE() << "Get camera availableRequestKeys failed!";
    }

    // Check availableResultKeys
    retcode =
            find_camera_metadata_ro_entry(metadata, ANDROID_REQUEST_AVAILABLE_RESULT_KEYS, &entry);
    if ((0 == retcode) && (entry.count > 0)) {
        for (size_t i = 0; i < entry.count; i++) {
            ASSERT_NE(entry.data.i32[i], ANDROID_SENSOR_GREEN_SPLIT);
            ASSERT_NE(entry.data.i32[i], ANDROID_SENSOR_NEUTRAL_COLOR_POINT);
            ASSERT_NE(entry.data.i32[i], ANDROID_COLOR_CORRECTION_MODE);
            ASSERT_NE(entry.data.i32[i], ANDROID_COLOR_CORRECTION_TRANSFORM);
            ASSERT_NE(entry.data.i32[i], ANDROID_COLOR_CORRECTION_GAINS);
        }
    } else {
        ADD_FAILURE() << "Get camera availableResultKeys failed!";
    }

    // Check availableCharacteristicKeys
    retcode = find_camera_metadata_ro_entry(metadata,
                                            ANDROID_REQUEST_AVAILABLE_CHARACTERISTICS_KEYS, &entry);
    if ((0 == retcode) && (entry.count > 0)) {
        for (size_t i = 0; i < entry.count; i++) {
            ASSERT_NE(entry.data.i32[i], ANDROID_SENSOR_REFERENCE_ILLUMINANT1);
            ASSERT_NE(entry.data.i32[i], ANDROID_SENSOR_REFERENCE_ILLUMINANT2);
            ASSERT_NE(entry.data.i32[i], ANDROID_SENSOR_CALIBRATION_TRANSFORM1);
            ASSERT_NE(entry.data.i32[i], ANDROID_SENSOR_CALIBRATION_TRANSFORM2);
            ASSERT_NE(entry.data.i32[i], ANDROID_SENSOR_COLOR_TRANSFORM1);
            ASSERT_NE(entry.data.i32[i], ANDROID_SENSOR_COLOR_TRANSFORM2);
            ASSERT_NE(entry.data.i32[i], ANDROID_SENSOR_FORWARD_MATRIX1);
            ASSERT_NE(entry.data.i32[i], ANDROID_SENSOR_FORWARD_MATRIX2);
        }
    } else {
        ADD_FAILURE() << "Get camera availableResultKeys failed!";
    }

    // Check blackLevelPattern
    retcode = find_camera_metadata_ro_entry(metadata, ANDROID_SENSOR_BLACK_LEVEL_PATTERN, &entry);
    if ((0 == retcode) && (entry.count > 0)) {
        ASSERT_EQ(entry.count, 4);
        for (size_t i = 1; i < entry.count; i++) {
            ASSERT_EQ(entry.data.i32[i], entry.data.i32[0]);
        }
    }
}

void CameraAidlTest::verifyManualFlashStrengthControlCharacteristics(
        const camera_metadata_t* staticMeta) {
    camera_metadata_ro_entry singleMaxEntry;
    camera_metadata_ro_entry singleDefEntry;
    camera_metadata_ro_entry torchMaxEntry;
    camera_metadata_ro_entry torchDefEntry;
    bool torch_supported = false;
    int32_t singleMaxLevel = 0;
    int32_t singleDefLevel = 0;
    int32_t torchMaxLevel = 0;
    int32_t torchDefLevel = 0;

    // determine whether the device supports torch or not
    torch_supported = isTorchSupported(staticMeta);

    int singleMaxRetCode = find_camera_metadata_ro_entry(staticMeta,
            ANDROID_FLASH_SINGLE_STRENGTH_MAX_LEVEL, &singleMaxEntry);
    int singleDefRetCode = find_camera_metadata_ro_entry(staticMeta,
            ANDROID_FLASH_SINGLE_STRENGTH_DEFAULT_LEVEL, &singleDefEntry);
    int torchMaxRetCode = find_camera_metadata_ro_entry(staticMeta,
            ANDROID_FLASH_TORCH_STRENGTH_MAX_LEVEL, &torchMaxEntry);
    int torchDefRetCode = find_camera_metadata_ro_entry(staticMeta,
            ANDROID_FLASH_TORCH_STRENGTH_DEFAULT_LEVEL, &torchDefEntry);
    if (torch_supported) {
        int expectedEntryCount;
        if(singleMaxRetCode == 0 && singleDefRetCode == 0 && torchMaxRetCode == 0 &&
                torchDefRetCode == 0) {
            singleMaxLevel = *singleMaxEntry.data.i32;
            singleDefLevel = *singleDefEntry.data.i32;
            torchMaxLevel = *torchMaxEntry.data.i32;
            torchDefLevel = *torchDefEntry.data.i32;
            expectedEntryCount = 1;
        } else {
            expectedEntryCount = 0;
        }
        ASSERT_EQ(singleMaxEntry.count, expectedEntryCount);
        ASSERT_EQ(singleDefEntry.count, expectedEntryCount);
        ASSERT_EQ(torchMaxEntry.count, expectedEntryCount);
        ASSERT_EQ(torchDefEntry.count, expectedEntryCount);
        // if the device supports this feature default levels should be greater than 0
        if (singleMaxLevel > 1) {
            ASSERT_GT(torchMaxLevel, 1);
            ASSERT_GT(torchDefLevel, 0);
            ASSERT_GT(singleDefLevel, 0);
            ASSERT_TRUE(torchDefLevel <= torchMaxLevel); // default levels should be <= max levels
            ASSERT_TRUE(singleDefLevel <= singleMaxLevel);
        }
    } else {
        ASSERT_TRUE(singleMaxRetCode != 0);
        ASSERT_TRUE(singleDefRetCode != 0);
        ASSERT_TRUE(torchMaxRetCode != 0);
        ASSERT_TRUE(torchDefRetCode != 0);
    }
}

void CameraAidlTest::verifyRecommendedConfigs(const CameraMetadata& chars) {
    size_t CONFIG_ENTRY_SIZE = 5;
    size_t CONFIG_ENTRY_TYPE_OFFSET = 3;
    size_t CONFIG_ENTRY_BITFIELD_OFFSET = 4;
    uint32_t maxPublicUsecase =
            ANDROID_SCALER_AVAILABLE_RECOMMENDED_STREAM_CONFIGURATIONS_PUBLIC_END_3_8;
    uint32_t vendorUsecaseStart =
            ANDROID_SCALER_AVAILABLE_RECOMMENDED_STREAM_CONFIGURATIONS_VENDOR_START;
    uint32_t usecaseMask = (1 << vendorUsecaseStart) - 1;
    usecaseMask &= ~((1 << maxPublicUsecase) - 1);

    const camera_metadata_t* metadata =
            reinterpret_cast<const camera_metadata_t*>(chars.metadata.data());

    camera_metadata_ro_entry recommendedConfigsEntry, recommendedDepthConfigsEntry, ioMapEntry;
    recommendedConfigsEntry.count = recommendedDepthConfigsEntry.count = ioMapEntry.count = 0;
    int retCode = find_camera_metadata_ro_entry(
            metadata, ANDROID_SCALER_AVAILABLE_RECOMMENDED_STREAM_CONFIGURATIONS,
            &recommendedConfigsEntry);
    int depthRetCode = find_camera_metadata_ro_entry(
            metadata, ANDROID_DEPTH_AVAILABLE_RECOMMENDED_DEPTH_STREAM_CONFIGURATIONS,
            &recommendedDepthConfigsEntry);
    int ioRetCode = find_camera_metadata_ro_entry(
            metadata, ANDROID_SCALER_AVAILABLE_RECOMMENDED_INPUT_OUTPUT_FORMATS_MAP, &ioMapEntry);
    if ((0 != retCode) && (0 != depthRetCode)) {
        // In case both regular and depth recommended configurations are absent,
        // I/O should be absent as well.
        ASSERT_NE(ioRetCode, 0);
        return;
    }

    camera_metadata_ro_entry availableKeysEntry;
    retCode = find_camera_metadata_ro_entry(
            metadata, ANDROID_REQUEST_AVAILABLE_CHARACTERISTICS_KEYS, &availableKeysEntry);
    ASSERT_TRUE((0 == retCode) && (availableKeysEntry.count > 0));
    std::vector<int32_t> availableKeys;
    availableKeys.reserve(availableKeysEntry.count);
    availableKeys.insert(availableKeys.end(), availableKeysEntry.data.i32,
                         availableKeysEntry.data.i32 + availableKeysEntry.count);

    if (recommendedConfigsEntry.count > 0) {
        ASSERT_NE(std::find(availableKeys.begin(), availableKeys.end(),
                            ANDROID_SCALER_AVAILABLE_RECOMMENDED_STREAM_CONFIGURATIONS),
                  availableKeys.end());
        ASSERT_EQ((recommendedConfigsEntry.count % CONFIG_ENTRY_SIZE), 0);
        for (size_t i = 0; i < recommendedConfigsEntry.count; i += CONFIG_ENTRY_SIZE) {
            int32_t entryType = recommendedConfigsEntry.data.i32[i + CONFIG_ENTRY_TYPE_OFFSET];
            uint32_t bitfield = recommendedConfigsEntry.data.i32[i + CONFIG_ENTRY_BITFIELD_OFFSET];
            ASSERT_TRUE((entryType == ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT) ||
                        (entryType == ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_INPUT));
            ASSERT_TRUE((bitfield & usecaseMask) == 0);
        }
    }

    if (recommendedDepthConfigsEntry.count > 0) {
        ASSERT_NE(std::find(availableKeys.begin(), availableKeys.end(),
                            ANDROID_DEPTH_AVAILABLE_RECOMMENDED_DEPTH_STREAM_CONFIGURATIONS),
                  availableKeys.end());
        ASSERT_EQ((recommendedDepthConfigsEntry.count % CONFIG_ENTRY_SIZE), 0);
        for (size_t i = 0; i < recommendedDepthConfigsEntry.count; i += CONFIG_ENTRY_SIZE) {
            int32_t entryType = recommendedDepthConfigsEntry.data.i32[i + CONFIG_ENTRY_TYPE_OFFSET];
            uint32_t bitfield =
                    recommendedDepthConfigsEntry.data.i32[i + CONFIG_ENTRY_BITFIELD_OFFSET];
            ASSERT_TRUE((entryType == ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT) ||
                        (entryType == ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_INPUT));
            ASSERT_TRUE((bitfield & usecaseMask) == 0);
        }

        if (recommendedConfigsEntry.count == 0) {
            // In case regular recommended configurations are absent but suggested depth
            // configurations are present, I/O should be absent.
            ASSERT_NE(ioRetCode, 0);
        }
    }

    if ((ioRetCode == 0) && (ioMapEntry.count > 0)) {
        ASSERT_NE(std::find(availableKeys.begin(), availableKeys.end(),
                            ANDROID_SCALER_AVAILABLE_RECOMMENDED_INPUT_OUTPUT_FORMATS_MAP),
                  availableKeys.end());
        ASSERT_EQ(isZSLModeAvailable(metadata), Status::OK);
    }
}

// Check whether ZSL is available using the static camera
// characteristics.
Status CameraAidlTest::isZSLModeAvailable(const camera_metadata_t* staticMeta) {
    if (Status::OK == isZSLModeAvailable(staticMeta, PRIV_REPROCESS)) {
        return Status::OK;
    } else {
        return isZSLModeAvailable(staticMeta, YUV_REPROCESS);
    }
}

Status CameraAidlTest::isZSLModeAvailable(const camera_metadata_t* staticMeta,
                                          ReprocessType reprocType) {
    Status ret = Status::OPERATION_NOT_SUPPORTED;
    if (nullptr == staticMeta) {
        return Status::ILLEGAL_ARGUMENT;
    }

    camera_metadata_ro_entry entry;
    int rc = find_camera_metadata_ro_entry(staticMeta, ANDROID_REQUEST_AVAILABLE_CAPABILITIES,
                                           &entry);
    if (0 != rc) {
        return Status::ILLEGAL_ARGUMENT;
    }

    for (size_t i = 0; i < entry.count; i++) {
        if ((reprocType == PRIV_REPROCESS &&
             ANDROID_REQUEST_AVAILABLE_CAPABILITIES_PRIVATE_REPROCESSING == entry.data.u8[i]) ||
            (reprocType == YUV_REPROCESS &&
             ANDROID_REQUEST_AVAILABLE_CAPABILITIES_YUV_REPROCESSING == entry.data.u8[i])) {
            ret = Status::OK;
            break;
        }
    }

    return ret;
}

// Verify logical or ultra high resolution camera static metadata
void CameraAidlTest::verifyLogicalOrUltraHighResCameraMetadata(
        const std::string& cameraName, const std::shared_ptr<ICameraDevice>& device,
        const CameraMetadata& chars, const std::vector<std::string>& deviceNames) {
    const camera_metadata_t* metadata =
            reinterpret_cast<const camera_metadata_t*>(chars.metadata.data());
    ASSERT_NE(nullptr, metadata);
    SystemCameraKind systemCameraKind = SystemCameraKind::PUBLIC;
    Status retStatus = getSystemCameraKind(metadata, &systemCameraKind);
    ASSERT_EQ(retStatus, Status::OK);
    Status rc = isLogicalMultiCamera(metadata);
    ASSERT_TRUE(Status::OK == rc || Status::OPERATION_NOT_SUPPORTED == rc);
    bool isMultiCamera = (Status::OK == rc);
    bool isUltraHighResCamera = isUltraHighResolution(metadata);
    if (!isMultiCamera && !isUltraHighResCamera) {
        return;
    }

    camera_metadata_ro_entry entry;
    int retcode = find_camera_metadata_ro_entry(metadata, ANDROID_CONTROL_ZOOM_RATIO_RANGE, &entry);
    bool hasZoomRatioRange = (0 == retcode && entry.count == 2);
    retcode = find_camera_metadata_ro_entry(
            metadata, ANDROID_INFO_SUPPORTED_BUFFER_MANAGEMENT_VERSION, &entry);
    bool hasHalBufferManager =
            (0 == retcode && 1 == entry.count &&
             entry.data.i32[0] == ANDROID_INFO_SUPPORTED_BUFFER_MANAGEMENT_VERSION_HIDL_DEVICE_3_5);
    bool sessionHalBufferManager =
            (0 == retcode && 1 == entry.count &&
             entry.data.i32[0] ==
                     ANDROID_INFO_SUPPORTED_BUFFER_MANAGEMENT_VERSION_SESSION_CONFIGURABLE);
    retcode = find_camera_metadata_ro_entry(
            metadata, ANDROID_SCALER_MULTI_RESOLUTION_STREAM_SUPPORTED, &entry);
    bool multiResolutionStreamSupported =
            (0 == retcode && 1 == entry.count &&
             entry.data.u8[0] == ANDROID_SCALER_MULTI_RESOLUTION_STREAM_SUPPORTED_TRUE);
    if (multiResolutionStreamSupported) {
        ASSERT_TRUE(hasHalBufferManager || sessionHalBufferManager);
    }

    std::string version, cameraId;
    ASSERT_TRUE(matchDeviceName(cameraName, mProviderType, &version, &cameraId));
    std::unordered_set<std::string> physicalIds;
    rc = getPhysicalCameraIds(metadata, &physicalIds);
    ASSERT_TRUE(isUltraHighResCamera || Status::OK == rc);
    for (const auto& physicalId : physicalIds) {
        ASSERT_NE(physicalId, cameraId);
    }
    if (physicalIds.size() == 0) {
        ASSERT_TRUE(isUltraHighResCamera && !isMultiCamera);
        physicalIds.insert(cameraId);
    }

    std::unordered_set<int32_t> physicalRequestKeyIDs;
    rc = getSupportedKeys(const_cast<camera_metadata_t*>(metadata),
                          ANDROID_REQUEST_AVAILABLE_PHYSICAL_CAMERA_REQUEST_KEYS,
                          &physicalRequestKeyIDs);
    ASSERT_TRUE(Status::OK == rc);
    bool hasTestPatternPhysicalRequestKey =
            physicalRequestKeyIDs.find(ANDROID_SENSOR_TEST_PATTERN_MODE) !=
            physicalRequestKeyIDs.end();
    std::unordered_set<int32_t> privacyTestPatternModes;
    getPrivacyTestPatternModes(metadata, &privacyTestPatternModes);

    // Map from image format to number of multi-resolution sizes for that format
    std::unordered_map<int32_t, size_t> multiResOutputFormatCounterMap;
    std::unordered_map<int32_t, size_t> multiResInputFormatCounterMap;
    for (const auto& physicalId : physicalIds) {
        bool isPublicId = false;
        std::string fullPublicId;
        SystemCameraKind physSystemCameraKind = SystemCameraKind::PUBLIC;
        for (auto& deviceName : deviceNames) {
            std::string publicVersion, publicId;
            ASSERT_TRUE(matchDeviceName(deviceName, mProviderType, &publicVersion, &publicId));
            if (physicalId == publicId) {
                isPublicId = true;
                fullPublicId = deviceName;
                break;
            }
        }

        camera_metadata_ro_entry physicalMultiResStreamConfigs;
        camera_metadata_ro_entry physicalStreamConfigs;
        camera_metadata_ro_entry physicalMaxResolutionStreamConfigs;
        CameraMetadata physChars;
        bool isUltraHighRes = false;
        std::unordered_set<int32_t> subCameraPrivacyTestPatterns;
        if (isPublicId) {
            std::shared_ptr<ICameraDevice> subDevice;
            ndk::ScopedAStatus ret = mProvider->getCameraDeviceInterface(fullPublicId, &subDevice);
            ASSERT_TRUE(ret.isOk());
            ASSERT_NE(subDevice, nullptr);

            ret = subDevice->getCameraCharacteristics(&physChars);
            ASSERT_TRUE(ret.isOk());

            const camera_metadata_t* staticMetadata =
                    reinterpret_cast<const camera_metadata_t*>(physChars.metadata.data());
            retStatus = getSystemCameraKind(staticMetadata, &physSystemCameraKind);
            ASSERT_EQ(retStatus, Status::OK);

            // Make sure that the system camera kind of a non-hidden
            // physical cameras is the same as the logical camera associated
            // with it.
            ASSERT_EQ(physSystemCameraKind, systemCameraKind);
            retcode = find_camera_metadata_ro_entry(staticMetadata,
                                                    ANDROID_CONTROL_ZOOM_RATIO_RANGE, &entry);
            bool subCameraHasZoomRatioRange = (0 == retcode && entry.count == 2);
            ASSERT_EQ(hasZoomRatioRange, subCameraHasZoomRatioRange);

            getMultiResolutionStreamConfigurations(
                    &physicalMultiResStreamConfigs, &physicalStreamConfigs,
                    &physicalMaxResolutionStreamConfigs, staticMetadata);
            isUltraHighRes = isUltraHighResolution(staticMetadata);

            getPrivacyTestPatternModes(staticMetadata, &subCameraPrivacyTestPatterns);
        } else {
            // Check camera characteristics for hidden camera id
            ndk::ScopedAStatus ret =
                    device->getPhysicalCameraCharacteristics(physicalId, &physChars);
            ASSERT_TRUE(ret.isOk());
            verifyCameraCharacteristics(physChars);
            verifyMonochromeCharacteristics(physChars);

            auto staticMetadata = (const camera_metadata_t*)physChars.metadata.data();
            retcode = find_camera_metadata_ro_entry(staticMetadata,
                                                    ANDROID_CONTROL_ZOOM_RATIO_RANGE, &entry);
            bool subCameraHasZoomRatioRange = (0 == retcode && entry.count == 2);
            ASSERT_EQ(hasZoomRatioRange, subCameraHasZoomRatioRange);

            getMultiResolutionStreamConfigurations(
                    &physicalMultiResStreamConfigs, &physicalStreamConfigs,
                    &physicalMaxResolutionStreamConfigs, staticMetadata);
            isUltraHighRes = isUltraHighResolution(staticMetadata);
            getPrivacyTestPatternModes(staticMetadata, &subCameraPrivacyTestPatterns);

            // Check calling getCameraDeviceInterface_V3_x() on hidden camera id returns
            // ILLEGAL_ARGUMENT.
            std::stringstream s;
            s << "device@" << version << "/" << mProviderType << "/" << physicalId;
            std::string fullPhysicalId(s.str());
            std::shared_ptr<ICameraDevice> subDevice;
            ret = mProvider->getCameraDeviceInterface(fullPhysicalId, &subDevice);
            ASSERT_TRUE(static_cast<int32_t>(Status::ILLEGAL_ARGUMENT) ==
                        ret.getServiceSpecificError());
            ASSERT_EQ(subDevice, nullptr);
        }

        if (hasTestPatternPhysicalRequestKey) {
            ASSERT_TRUE(privacyTestPatternModes == subCameraPrivacyTestPatterns);
        }

        if (physicalMultiResStreamConfigs.count > 0) {
            ASSERT_EQ(physicalMultiResStreamConfigs.count % 4, 0);

            // Each supported size must be max size for that format,
            for (size_t i = 0; i < physicalMultiResStreamConfigs.count / 4; i++) {
                int32_t multiResFormat = physicalMultiResStreamConfigs.data.i32[i * 4];
                int32_t multiResWidth = physicalMultiResStreamConfigs.data.i32[i * 4 + 1];
                int32_t multiResHeight = physicalMultiResStreamConfigs.data.i32[i * 4 + 2];
                int32_t multiResInput = physicalMultiResStreamConfigs.data.i32[i * 4 + 3];

                // Check if the resolution is the max resolution in stream
                // configuration map
                bool supported = false;
                bool isMaxSize = true;
                for (size_t j = 0; j < physicalStreamConfigs.count / 4; j++) {
                    int32_t format = physicalStreamConfigs.data.i32[j * 4];
                    int32_t width = physicalStreamConfigs.data.i32[j * 4 + 1];
                    int32_t height = physicalStreamConfigs.data.i32[j * 4 + 2];
                    int32_t input = physicalStreamConfigs.data.i32[j * 4 + 3];
                    if (format == multiResFormat && input == multiResInput) {
                        if (width == multiResWidth && height == multiResHeight) {
                            supported = true;
                        } else if (width * height > multiResWidth * multiResHeight) {
                            isMaxSize = false;
                        }
                    }
                }
                // Check if the resolution is the max resolution in max
                // resolution stream configuration map
                bool supportedUltraHighRes = false;
                bool isUltraHighResMaxSize = true;
                for (size_t j = 0; j < physicalMaxResolutionStreamConfigs.count / 4; j++) {
                    int32_t format = physicalMaxResolutionStreamConfigs.data.i32[j * 4];
                    int32_t width = physicalMaxResolutionStreamConfigs.data.i32[j * 4 + 1];
                    int32_t height = physicalMaxResolutionStreamConfigs.data.i32[j * 4 + 2];
                    int32_t input = physicalMaxResolutionStreamConfigs.data.i32[j * 4 + 3];
                    if (format == multiResFormat && input == multiResInput) {
                        if (width == multiResWidth && height == multiResHeight) {
                            supportedUltraHighRes = true;
                        } else if (width * height > multiResWidth * multiResHeight) {
                            isUltraHighResMaxSize = false;
                        }
                    }
                }

                if (isUltraHighRes) {
                    // For ultra high resolution camera, the configuration must
                    // be the maximum size in stream configuration map, or max
                    // resolution stream configuration map
                    ASSERT_TRUE((supported && isMaxSize) ||
                                (supportedUltraHighRes && isUltraHighResMaxSize));
                } else {
                    // The configuration must be the maximum size in stream
                    // configuration map
                    ASSERT_TRUE(supported && isMaxSize);
                    ASSERT_FALSE(supportedUltraHighRes);
                }

                // Increment the counter for the configuration's format.
                auto& formatCounterMap = multiResInput ? multiResInputFormatCounterMap
                                                       : multiResOutputFormatCounterMap;
                if (formatCounterMap.count(multiResFormat) == 0) {
                    formatCounterMap[multiResFormat] = 1;
                } else {
                    formatCounterMap[multiResFormat]++;
                }
            }

            // There must be no duplicates
            for (size_t i = 0; i < physicalMultiResStreamConfigs.count / 4 - 1; i++) {
                for (size_t j = i + 1; j < physicalMultiResStreamConfigs.count / 4; j++) {
                    // Input/output doesn't match
                    if (physicalMultiResStreamConfigs.data.i32[i * 4 + 3] !=
                        physicalMultiResStreamConfigs.data.i32[j * 4 + 3]) {
                        continue;
                    }
                    // Format doesn't match
                    if (physicalMultiResStreamConfigs.data.i32[i * 4] !=
                        physicalMultiResStreamConfigs.data.i32[j * 4]) {
                        continue;
                    }
                    // Width doesn't match
                    if (physicalMultiResStreamConfigs.data.i32[i * 4 + 1] !=
                        physicalMultiResStreamConfigs.data.i32[j * 4 + 1]) {
                        continue;
                    }
                    // Height doesn't match
                    if (physicalMultiResStreamConfigs.data.i32[i * 4 + 2] !=
                        physicalMultiResStreamConfigs.data.i32[j * 4 + 2]) {
                        continue;
                    }
                    // input/output, format, width, and height all match
                    ADD_FAILURE();
                }
            }
        }
    }

    // If a multi-resolution stream is supported, there must be at least one
    // format with more than one resolutions
    if (multiResolutionStreamSupported) {
        size_t numMultiResFormats = 0;
        for (const auto& [format, sizeCount] : multiResOutputFormatCounterMap) {
            if (sizeCount >= 2) {
                numMultiResFormats++;
            }
        }
        for (const auto& [format, sizeCount] : multiResInputFormatCounterMap) {
            if (sizeCount >= 2) {
                numMultiResFormats++;

                // If multi-resolution reprocessing is supported, the logical
                // camera or ultra-high resolution sensor camera must support
                // the corresponding reprocessing capability.
                if (format == static_cast<uint32_t>(PixelFormat::IMPLEMENTATION_DEFINED)) {
                    ASSERT_EQ(isZSLModeAvailable(metadata, PRIV_REPROCESS), Status::OK);
                } else if (format == static_cast<int32_t>(PixelFormat::YCBCR_420_888)) {
                    ASSERT_EQ(isZSLModeAvailable(metadata, YUV_REPROCESS), Status::OK);
                }
            }
        }
        ASSERT_GT(numMultiResFormats, 0);
    }

    // Make sure ANDROID_LOGICAL_MULTI_CAMERA_ACTIVE_PHYSICAL_ID is available in
    // result keys.
    if (isMultiCamera) {
        retcode = find_camera_metadata_ro_entry(metadata, ANDROID_REQUEST_AVAILABLE_RESULT_KEYS,
                                                &entry);
        if ((0 == retcode) && (entry.count > 0)) {
            ASSERT_NE(std::find(entry.data.i32, entry.data.i32 + entry.count,
                                static_cast<int32_t>(
                                        CameraMetadataTag::
                                                ANDROID_LOGICAL_MULTI_CAMERA_ACTIVE_PHYSICAL_ID)),
                      entry.data.i32 + entry.count);
        } else {
            ADD_FAILURE() << "Get camera availableResultKeys failed!";
        }
    }
}

bool CameraAidlTest::isUltraHighResolution(const camera_metadata_t* staticMeta) {
    camera_metadata_ro_entry scalerEntry;
    int rc = find_camera_metadata_ro_entry(staticMeta, ANDROID_REQUEST_AVAILABLE_CAPABILITIES,
                                           &scalerEntry);
    if (rc == 0) {
        for (uint32_t i = 0; i < scalerEntry.count; i++) {
            if (scalerEntry.data.u8[i] ==
                ANDROID_REQUEST_AVAILABLE_CAPABILITIES_ULTRA_HIGH_RESOLUTION_SENSOR) {
                return true;
            }
        }
    }
    return false;
}

Status CameraAidlTest::getSupportedKeys(camera_metadata_t* staticMeta, uint32_t tagId,
                                        std::unordered_set<int32_t>* requestIDs) {
    if ((nullptr == staticMeta) || (nullptr == requestIDs)) {
        return Status::ILLEGAL_ARGUMENT;
    }

    camera_metadata_ro_entry entry;
    int rc = find_camera_metadata_ro_entry(staticMeta, tagId, &entry);
    if ((0 != rc) || (entry.count == 0)) {
        return Status::OK;
    }

    requestIDs->insert(entry.data.i32, entry.data.i32 + entry.count);

    return Status::OK;
}

void CameraAidlTest::getPrivacyTestPatternModes(
        const camera_metadata_t* staticMetadata,
        std::unordered_set<int32_t>* privacyTestPatternModes) {
    ASSERT_NE(staticMetadata, nullptr);
    ASSERT_NE(privacyTestPatternModes, nullptr);

    camera_metadata_ro_entry entry;
    int retcode = find_camera_metadata_ro_entry(
            staticMetadata, ANDROID_SENSOR_AVAILABLE_TEST_PATTERN_MODES, &entry);
    ASSERT_TRUE(0 == retcode);

    for (auto i = 0; i < entry.count; i++) {
        if (entry.data.i32[i] == ANDROID_SENSOR_TEST_PATTERN_MODE_SOLID_COLOR ||
            entry.data.i32[i] == ANDROID_SENSOR_TEST_PATTERN_MODE_BLACK) {
            privacyTestPatternModes->insert(entry.data.i32[i]);
        }
    }
}

void CameraAidlTest::getMultiResolutionStreamConfigurations(
        camera_metadata_ro_entry* multiResStreamConfigs, camera_metadata_ro_entry* streamConfigs,
        camera_metadata_ro_entry* maxResolutionStreamConfigs,
        const camera_metadata_t* staticMetadata) {
    ASSERT_NE(multiResStreamConfigs, nullptr);
    ASSERT_NE(streamConfigs, nullptr);
    ASSERT_NE(maxResolutionStreamConfigs, nullptr);
    ASSERT_NE(staticMetadata, nullptr);

    int retcode = find_camera_metadata_ro_entry(
            staticMetadata, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, streamConfigs);
    ASSERT_TRUE(0 == retcode);
    retcode = find_camera_metadata_ro_entry(
            staticMetadata, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_MAXIMUM_RESOLUTION,
            maxResolutionStreamConfigs);
    ASSERT_TRUE(-ENOENT == retcode || 0 == retcode);
    retcode = find_camera_metadata_ro_entry(
            staticMetadata, ANDROID_SCALER_PHYSICAL_CAMERA_MULTI_RESOLUTION_STREAM_CONFIGURATIONS,
            multiResStreamConfigs);
    ASSERT_TRUE(-ENOENT == retcode || 0 == retcode);
}

bool CameraAidlTest::isTorchSupported(const camera_metadata_t* staticMeta) {
    camera_metadata_ro_entry torchEntry;
    int rc = find_camera_metadata_ro_entry(staticMeta, ANDROID_FLASH_INFO_AVAILABLE, &torchEntry);
    if (rc != 0) {
        ALOGI("isTorchSupported: Failed to find entry for ANDROID_FLASH_INFO_AVAILABLE");
        return false;
    }
    if (torchEntry.count == 1 && !torchEntry.data.u8[0]) {
        ALOGI("isTorchSupported: Torch not supported");
        return false;
    }
    ALOGI("isTorchSupported: Torch supported");
    return true;
}

bool CameraAidlTest::isTorchStrengthControlSupported(const camera_metadata_t* staticMeta) {
    int32_t maxLevel = 0;
    camera_metadata_ro_entry maxEntry;
    int rc = find_camera_metadata_ro_entry(staticMeta, ANDROID_FLASH_INFO_STRENGTH_MAXIMUM_LEVEL,
                                           &maxEntry);
    if (rc != 0) {
        ALOGI("isTorchStrengthControlSupported: Failed to find entry for "
              "ANDROID_FLASH_INFO_STRENGTH_MAXIMUM_LEVEL");
        return false;
    }

    maxLevel = *maxEntry.data.i32;
    if (maxLevel > 1) {
        ALOGI("isTorchStrengthControlSupported: Torch strength control supported.");
        return true;
    }
    ALOGI("isTorchStrengthControlSupported: Torch strength control not supported.");
    return false;
}

void CameraAidlTest::verifyRequestTemplate(const camera_metadata_t* metadata,
                                           RequestTemplate requestTemplate) {
    ASSERT_NE(nullptr, metadata);
    size_t entryCount = get_camera_metadata_entry_count(metadata);
    ALOGI("template %u metadata entry count is %zu", (int32_t)requestTemplate, entryCount);
    // TODO: we can do better than 0 here. Need to check how many required
    // request keys we've defined for each template
    ASSERT_GT(entryCount, 0u);

    // Check zoomRatio
    camera_metadata_ro_entry zoomRatioEntry;
    int foundZoomRatio =
            find_camera_metadata_ro_entry(metadata, ANDROID_CONTROL_ZOOM_RATIO, &zoomRatioEntry);
    if (foundZoomRatio == 0) {
        ASSERT_EQ(zoomRatioEntry.count, 1);
        ASSERT_EQ(zoomRatioEntry.data.f[0], 1.0f);
    }

    // Check settings override
    camera_metadata_ro_entry settingsOverrideEntry;
    int foundSettingsOverride = find_camera_metadata_ro_entry(metadata,
           ANDROID_CONTROL_SETTINGS_OVERRIDE, &settingsOverrideEntry);
    if (foundSettingsOverride == 0) {
        ASSERT_EQ(settingsOverrideEntry.count, 1);
        ASSERT_EQ(settingsOverrideEntry.data.u8[0], ANDROID_CONTROL_SETTINGS_OVERRIDE_OFF);
    }
}

void CameraAidlTest::openEmptyDeviceSession(const std::string& name,
                                            const std::shared_ptr<ICameraProvider>& provider,
                                            std::shared_ptr<ICameraDeviceSession>* session,
                                            CameraMetadata* staticMeta,
                                            std::shared_ptr<ICameraDevice>* device) {
    ASSERT_NE(nullptr, session);
    ASSERT_NE(nullptr, staticMeta);
    ASSERT_NE(nullptr, device);

    ALOGI("configureStreams: Testing camera device %s", name.c_str());
    ndk::ScopedAStatus ret = provider->getCameraDeviceInterface(name, device);
    ALOGI("getCameraDeviceInterface returns status:%d:%d", ret.getExceptionCode(),
          ret.getServiceSpecificError());
    ASSERT_TRUE(ret.isOk());
    ASSERT_NE(*device, nullptr);

    std::shared_ptr<EmptyDeviceCb> cb = ndk::SharedRefBase::make<EmptyDeviceCb>();
    ret = (*device)->open(cb, session);
    ALOGI("device::open returns status:%d:%d", ret.getExceptionCode(),
          ret.getServiceSpecificError());
    ASSERT_TRUE(ret.isOk());
    ASSERT_NE(*session, nullptr);

    ret = (*device)->getCameraCharacteristics(staticMeta);
    ASSERT_TRUE(ret.isOk());
}

void CameraAidlTest::openEmptyInjectionSession(const std::string& name,
                                               const std::shared_ptr<ICameraProvider>& provider,
                                               std::shared_ptr<ICameraInjectionSession>* session,
                                               CameraMetadata* metadata,
                                               std::shared_ptr<ICameraDevice>* device) {
    ASSERT_NE(nullptr, session);
    ASSERT_NE(nullptr, metadata);
    ASSERT_NE(nullptr, device);

    ALOGI("openEmptyInjectionSession: Testing camera device %s", name.c_str());
    ndk::ScopedAStatus ret = provider->getCameraDeviceInterface(name, device);
    ALOGI("openEmptyInjectionSession: getCameraDeviceInterface returns status:%d:%d",
          ret.getExceptionCode(), ret.getServiceSpecificError());
    ASSERT_TRUE(ret.isOk());
    ASSERT_NE(*device, nullptr);

    std::shared_ptr<EmptyDeviceCb> cb = ndk::SharedRefBase::make<EmptyDeviceCb>();
    ret = (*device)->openInjectionSession(cb, session);
    ALOGI("device::openInjectionSession returns status:%d:%d", ret.getExceptionCode(),
          ret.getServiceSpecificError());

    if (static_cast<Status>(ret.getServiceSpecificError()) == Status::OPERATION_NOT_SUPPORTED &&
        *session == nullptr) {
        return;  // Injection Session not supported. Callee will receive nullptr in *session
    }

    ASSERT_TRUE(ret.isOk());
    ASSERT_NE(*session, nullptr);

    ret = (*device)->getCameraCharacteristics(metadata);
    ASSERT_TRUE(ret.isOk());
}

Status CameraAidlTest::getJpegBufferSize(camera_metadata_t* staticMeta, int32_t* outBufSize) {
    if (nullptr == staticMeta || nullptr == outBufSize) {
        return Status::ILLEGAL_ARGUMENT;
    }

    camera_metadata_ro_entry entry;
    int rc = find_camera_metadata_ro_entry(staticMeta, ANDROID_JPEG_MAX_SIZE, &entry);
    if ((0 != rc) || (1 != entry.count)) {
        return Status::ILLEGAL_ARGUMENT;
    }

    *outBufSize = entry.data.i32[0];
    return Status::OK;
}

Dataspace CameraAidlTest::getDataspace(PixelFormat format) {
    switch (format) {
        case PixelFormat::BLOB:
            return Dataspace::JFIF;
        case PixelFormat::Y16:
            return Dataspace::DEPTH;
        case PixelFormat::RAW16:
        case PixelFormat::RAW_OPAQUE:
        case PixelFormat::RAW10:
        case PixelFormat::RAW12:
            return Dataspace::ARBITRARY;
        default:
            return Dataspace::UNKNOWN;
    }
}

void CameraAidlTest::createStreamConfiguration(std::vector<Stream>& streams,
                                               StreamConfigurationMode configMode,
                                               StreamConfiguration* config,
                                               int32_t jpegBufferSize) {
    ASSERT_NE(nullptr, config);

    for (auto& stream : streams) {
        stream.bufferSize =
                (stream.format == PixelFormat::BLOB && stream.dataSpace == Dataspace::JFIF)
                        ? jpegBufferSize
                        : 0;
    }

    // Caller is responsible to fill in non-zero config->streamConfigCounter after this returns
    config->streams = streams;
    config->operationMode = configMode;
    config->multiResolutionInputImage = false;
}

void CameraAidlTest::verifyStreamCombination(const std::shared_ptr<ICameraDevice>& device,
                                             const StreamConfiguration& config,
                                             bool expectedStatus) {
    if (device != nullptr) {
        bool streamCombinationSupported;
        ScopedAStatus ret =
                device->isStreamCombinationSupported(config, &streamCombinationSupported);
        ASSERT_TRUE(ret.isOk());
        ASSERT_EQ(expectedStatus, streamCombinationSupported);

        int32_t interfaceVersion;
        ret = device->getInterfaceVersion(&interfaceVersion);
        ASSERT_TRUE(ret.isOk());
        bool supportFeatureCombinationQuery =
                (interfaceVersion >= CAMERA_DEVICE_API_MINOR_VERSION_3);
        if (supportFeatureCombinationQuery) {
            ret = device->isStreamCombinationWithSettingsSupported(config,
                                                                   &streamCombinationSupported);
            ASSERT_TRUE(ret.isOk());
            ASSERT_EQ(expectedStatus, streamCombinationSupported);
        }
    }
}

void CameraAidlTest::verifySessionCharacteristics(const CameraMetadata& session_chars,
                                                  const CameraMetadata& camera_chars) {
    const camera_metadata_t* session_metadata =
            reinterpret_cast<const camera_metadata_t*>(session_chars.metadata.data());

    const camera_metadata_t* camera_metadata =
            reinterpret_cast<const camera_metadata_t*>(camera_chars.metadata.data());

    size_t expectedSize = session_chars.metadata.size();
    int result = validate_camera_metadata_structure(session_metadata, &expectedSize);
    ASSERT_TRUE((result == 0) || (result == CAMERA_METADATA_VALIDATION_SHIFTED));
    size_t entryCount = get_camera_metadata_entry_count(session_metadata);
    // There should be at least 1 characteristic present:
    // SCALER_MAX_DIGITAL_ZOOM must always be available.
    // ZOOM_RATIO_RANGE must be available if ZOOM_RATIO is supported.
    ASSERT_TRUE(entryCount >= 1);

    camera_metadata_ro_entry entry;
    int retcode = 0;
    float maxDigitalZoom = 1.0;

    for (size_t i = 0; i < entryCount; i++) {
        retcode = get_camera_metadata_ro_entry(session_metadata, i, &entry);
        ASSERT_TRUE(retcode == 0);

        std::set<uint32_t> allowed_tags = {ANDROID_SCALER_AVAILABLE_MAX_DIGITAL_ZOOM,
                                           ANDROID_CONTROL_ZOOM_RATIO_RANGE};

        if (contains(allowed_tags, entry.tag)) {
            continue;
        }

        // Other than the ones above, no tags should be allowed apart from vendor tags.
        ASSERT_TRUE(entry.tag >= VENDOR_SECTION_START);
    }

    retcode = find_camera_metadata_ro_entry(session_metadata,
                                            ANDROID_SCALER_AVAILABLE_MAX_DIGITAL_ZOOM, &entry);
    if ((0 == retcode) && (entry.count == 1)) {
        maxDigitalZoom = entry.data.f[0];
    } else {
        ADD_FAILURE() << "Get camera scalerAvailableMaxDigitalZoom failed!";
    }

    retcode = find_camera_metadata_ro_entry(camera_metadata, ANDROID_CONTROL_ZOOM_RATIO_RANGE,
                                            &entry);
    bool hasZoomRatioRange = (0 == retcode && entry.count == 2);
    if (!hasZoomRatioRange) {
        ALOGI("Skipping the rest of the test as ZOOM_RATIO_RANGE is not in camera characteristics");
        return;
    }

    // Session characteristics must contain zoom_ratio_range if camera characteristics has it.
    retcode = find_camera_metadata_ro_entry(session_metadata, ANDROID_CONTROL_ZOOM_RATIO_RANGE,
                                            &entry);
    ASSERT_TRUE(0 == retcode && entry.count == 2);

    float minZoomRatio = entry.data.f[0];
    float maxZoomRatio = entry.data.f[1];
    constexpr float FLOATING_POINT_THRESHOLD = 0.00001f;
    if (abs(maxDigitalZoom - maxZoomRatio) > FLOATING_POINT_THRESHOLD) {
        ADD_FAILURE() << "Difference between maximum digital zoom " << maxDigitalZoom
                      << " and maximum zoom ratio " << maxZoomRatio
                      << " is greater than the threshold " << FLOATING_POINT_THRESHOLD << "!";
    }
    if (minZoomRatio > maxZoomRatio) {
        ADD_FAILURE() << "Maximum zoom ratio is less than minimum zoom ratio!";
    }
    if (minZoomRatio > 1.0f) {
        ADD_FAILURE() << "Minimum zoom ratio is more than 1.0!";
    }
    if (maxZoomRatio < 1.0f) {
        ADD_FAILURE() << "Maximum zoom ratio is less than 1.0!";
    }
}

std::vector<ConcurrentCameraIdCombination> CameraAidlTest::getConcurrentDeviceCombinations(
        std::shared_ptr<ICameraProvider>& provider) {
    std::vector<ConcurrentCameraIdCombination> combinations;
    ndk::ScopedAStatus ret = provider->getConcurrentCameraIds(&combinations);
    if (!ret.isOk()) {
        ADD_FAILURE();
    }

    return combinations;
}

Status CameraAidlTest::getMandatoryConcurrentStreams(const camera_metadata_t* staticMeta,
                                                     std::vector<AvailableStream>* outputStreams) {
    if (nullptr == staticMeta || nullptr == outputStreams) {
        return Status::ILLEGAL_ARGUMENT;
    }

    if (isDepthOnly(staticMeta)) {
        Size y16MaxSize(640, 480);
        Size maxAvailableY16Size;
        getMaxOutputSizeForFormat(staticMeta, PixelFormat::Y16, &maxAvailableY16Size);
        Size y16ChosenSize = getMinSize(y16MaxSize, maxAvailableY16Size);
        AvailableStream y16Stream = {.width = y16ChosenSize.width,
                                     .height = y16ChosenSize.height,
                                     .format = static_cast<int32_t>(PixelFormat::Y16)};
        outputStreams->push_back(y16Stream);
        return Status::OK;
    }

    Size yuvMaxSize(1280, 720);
    Size jpegMaxSize(1920, 1440);
    Size maxAvailableYuvSize;
    Size maxAvailableJpegSize;
    getMaxOutputSizeForFormat(staticMeta, PixelFormat::YCBCR_420_888, &maxAvailableYuvSize);
    getMaxOutputSizeForFormat(staticMeta, PixelFormat::BLOB, &maxAvailableJpegSize);
    Size yuvChosenSize = getMinSize(yuvMaxSize, maxAvailableYuvSize);
    Size jpegChosenSize = getMinSize(jpegMaxSize, maxAvailableJpegSize);

    AvailableStream yuvStream = {.width = yuvChosenSize.width,
                                 .height = yuvChosenSize.height,
                                 .format = static_cast<int32_t>(PixelFormat::YCBCR_420_888)};

    AvailableStream jpegStream = {.width = jpegChosenSize.width,
                                  .height = jpegChosenSize.height,
                                  .format = static_cast<int32_t>(PixelFormat::BLOB)};
    outputStreams->push_back(yuvStream);
    outputStreams->push_back(jpegStream);

    return Status::OK;
}

bool CameraAidlTest::isDepthOnly(const camera_metadata_t* staticMeta) {
    camera_metadata_ro_entry scalerEntry;
    camera_metadata_ro_entry depthEntry;

    int rc = find_camera_metadata_ro_entry(staticMeta, ANDROID_REQUEST_AVAILABLE_CAPABILITIES,
                                           &scalerEntry);
    if (rc == 0) {
        for (uint32_t i = 0; i < scalerEntry.count; i++) {
            if (scalerEntry.data.u8[i] ==
                ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE) {
                return false;
            }
        }
    }

    for (uint32_t i = 0; i < scalerEntry.count; i++) {
        if (scalerEntry.data.u8[i] == ANDROID_REQUEST_AVAILABLE_CAPABILITIES_DEPTH_OUTPUT) {
            rc = find_camera_metadata_ro_entry(
                    staticMeta, ANDROID_DEPTH_AVAILABLE_DEPTH_STREAM_CONFIGURATIONS, &depthEntry);
            size_t idx = 0;
            if (rc == 0 && depthEntry.data.i32[idx] == static_cast<int32_t>(PixelFormat::Y16)) {
                // only Depth16 format is supported now
                return true;
            }
            break;
        }
    }

    return false;
}

Status CameraAidlTest::getMaxOutputSizeForFormat(const camera_metadata_t* staticMeta,
                                                 PixelFormat format, Size* size,
                                                 bool maxResolution) {
    std::vector<AvailableStream> outputStreams;
    if (size == nullptr ||
        getAvailableOutputStreams(staticMeta, outputStreams,
                                  /*threshold*/ nullptr, maxResolution) != Status::OK) {
        return Status::ILLEGAL_ARGUMENT;
    }
    Size maxSize;
    bool found = false;
    for (auto& outputStream : outputStreams) {
        if (static_cast<int32_t>(format) == outputStream.format &&
            (outputStream.width * outputStream.height > maxSize.width * maxSize.height)) {
            maxSize.width = outputStream.width;
            maxSize.height = outputStream.height;
            found = true;
        }
    }
    if (!found) {
        ALOGE("%s :chosen format %d not found", __FUNCTION__, static_cast<int32_t>(format));
        return Status::ILLEGAL_ARGUMENT;
    }
    *size = maxSize;
    return Status::OK;
}

Size CameraAidlTest::getMinSize(Size a, Size b) {
    if (a.width * a.height < b.width * b.height) {
        return a;
    }
    return b;
}

Status CameraAidlTest::getZSLInputOutputMap(camera_metadata_t* staticMeta,
                                            std::vector<AvailableZSLInputOutput>& inputOutputMap) {
    if (nullptr == staticMeta) {
        return Status::ILLEGAL_ARGUMENT;
    }

    camera_metadata_ro_entry entry;
    int rc = find_camera_metadata_ro_entry(
            staticMeta, ANDROID_SCALER_AVAILABLE_INPUT_OUTPUT_FORMATS_MAP, &entry);
    if ((0 != rc) || (0 >= entry.count)) {
        return Status::ILLEGAL_ARGUMENT;
    }

    const int32_t* contents = &entry.data.i32[0];
    for (size_t i = 0; i < entry.count;) {
        int32_t inputFormat = contents[i++];
        int32_t length = contents[i++];
        for (int32_t j = 0; j < length; j++) {
            int32_t outputFormat = contents[i + j];
            AvailableZSLInputOutput zslEntry = {inputFormat, outputFormat};
            inputOutputMap.push_back(zslEntry);
        }
        i += length;
    }

    return Status::OK;
}

Status CameraAidlTest::findLargestSize(const std::vector<AvailableStream>& streamSizes,
                                       int32_t format, AvailableStream& result) {
    result = {0, 0, 0};
    for (auto& iter : streamSizes) {
        if (format == iter.format) {
            if ((result.width * result.height) < (iter.width * iter.height)) {
                result = iter;
            }
        }
    }

    return (result.format == format) ? Status::OK : Status::ILLEGAL_ARGUMENT;
}

void CameraAidlTest::constructFilteredSettings(
        const std::shared_ptr<ICameraDeviceSession>& session,
        const std::unordered_set<int32_t>& availableKeys, RequestTemplate reqTemplate,
        android::hardware::camera::common::V1_0::helper::CameraMetadata* defaultSettings,
        android::hardware::camera::common::V1_0::helper::CameraMetadata* filteredSettings) {
    ASSERT_NE(defaultSettings, nullptr);
    ASSERT_NE(filteredSettings, nullptr);

    CameraMetadata req;
    auto ret = session->constructDefaultRequestSettings(reqTemplate, &req);
    ASSERT_TRUE(ret.isOk());

    const camera_metadata_t* metadata =
            clone_camera_metadata(reinterpret_cast<const camera_metadata_t*>(req.metadata.data()));
    size_t expectedSize = req.metadata.size();
    int result = validate_camera_metadata_structure(metadata, &expectedSize);
    ASSERT_TRUE((result == 0) || (result == CAMERA_METADATA_VALIDATION_SHIFTED));

    size_t entryCount = get_camera_metadata_entry_count(metadata);
    ASSERT_GT(entryCount, 0u);
    *defaultSettings = metadata;

    const android::hardware::camera::common::V1_0::helper::CameraMetadata& constSettings =
            *defaultSettings;
    for (const auto& keyIt : availableKeys) {
        camera_metadata_ro_entry entry = constSettings.find(keyIt);
        if (entry.count > 0) {
            filteredSettings->update(entry);
        }
    }
}

void CameraAidlTest::verifySessionReconfigurationQuery(
        const std::shared_ptr<ICameraDeviceSession>& session, camera_metadata* oldSessionParams,
        camera_metadata* newSessionParams) {
    ASSERT_NE(nullptr, session);
    ASSERT_NE(nullptr, oldSessionParams);
    ASSERT_NE(nullptr, newSessionParams);

    std::vector<uint8_t> oldParams =
            std::vector(reinterpret_cast<uint8_t*>(oldSessionParams),
                        reinterpret_cast<uint8_t*>(oldSessionParams) +
                                get_camera_metadata_size(oldSessionParams));
    CameraMetadata oldMetadata = {oldParams};

    std::vector<uint8_t> newParams =
            std::vector(reinterpret_cast<uint8_t*>(newSessionParams),
                        reinterpret_cast<uint8_t*>(newSessionParams) +
                                get_camera_metadata_size(newSessionParams));
    CameraMetadata newMetadata = {newParams};

    bool reconfigReq;
    ndk::ScopedAStatus ret =
            session->isReconfigurationRequired(oldMetadata, newMetadata, &reconfigReq);
    ASSERT_TRUE(ret.isOk() || static_cast<Status>(ret.getServiceSpecificError()) ==
                                      Status::OPERATION_NOT_SUPPORTED);
}

Status CameraAidlTest::isConstrainedModeAvailable(camera_metadata_t* staticMeta) {
    Status ret = Status::OPERATION_NOT_SUPPORTED;
    if (nullptr == staticMeta) {
        return Status::ILLEGAL_ARGUMENT;
    }

    camera_metadata_ro_entry entry;
    int rc = find_camera_metadata_ro_entry(staticMeta, ANDROID_REQUEST_AVAILABLE_CAPABILITIES,
                                           &entry);
    if (0 != rc) {
        return Status::ILLEGAL_ARGUMENT;
    }

    for (size_t i = 0; i < entry.count; i++) {
        if (ANDROID_REQUEST_AVAILABLE_CAPABILITIES_CONSTRAINED_HIGH_SPEED_VIDEO ==
            entry.data.u8[i]) {
            ret = Status::OK;
            break;
        }
    }

    return ret;
}

Status CameraAidlTest::pickConstrainedModeSize(camera_metadata_t* staticMeta,
                                               AvailableStream& hfrStream) {
    if (nullptr == staticMeta) {
        return Status::ILLEGAL_ARGUMENT;
    }

    camera_metadata_ro_entry entry;
    int rc = find_camera_metadata_ro_entry(
            staticMeta, ANDROID_CONTROL_AVAILABLE_HIGH_SPEED_VIDEO_CONFIGURATIONS, &entry);
    if (0 != rc) {
        return Status::OPERATION_NOT_SUPPORTED;
    } else if (0 != (entry.count % 5)) {
        return Status::ILLEGAL_ARGUMENT;
    }

    hfrStream = {0, 0, static_cast<uint32_t>(PixelFormat::IMPLEMENTATION_DEFINED)};
    for (size_t i = 0; i < entry.count; i += 5) {
        int32_t w = entry.data.i32[i];
        int32_t h = entry.data.i32[i + 1];
        if ((hfrStream.width * hfrStream.height) < (w * h)) {
            hfrStream.width = w;
            hfrStream.height = h;
        }
    }

    return Status::OK;
}

void CameraAidlTest::processCaptureRequestInternal(uint64_t bufferUsage,
                                                   RequestTemplate reqTemplate,
                                                   bool useSecureOnlyCameras) {
    std::vector<std::string> cameraDeviceNames =
            getCameraDeviceNames(mProvider, useSecureOnlyCameras);
    AvailableStream streamThreshold = {kMaxPreviewWidth, kMaxPreviewHeight,
                                       static_cast<int32_t>(PixelFormat::IMPLEMENTATION_DEFINED)};
    int64_t bufferId = 1;
    int32_t frameNumber = 1;
    CameraMetadata settings;
    for (const auto& name : cameraDeviceNames) {
        Stream testStream;
        std::vector<HalStream> halStreams;
        std::shared_ptr<ICameraDeviceSession> session;
        std::shared_ptr<DeviceCb> cb;
        bool supportsPartialResults = false;
        bool useHalBufManager = false;
        int32_t partialResultCount = 0;
        ASSERT_NO_FATAL_FAILURE(configureSingleStream(
                name, mProvider, &streamThreshold, bufferUsage, reqTemplate, &session /*out*/,
                &testStream /*out*/, &halStreams /*out*/, &supportsPartialResults /*out*/,
                &partialResultCount /*out*/, &useHalBufManager /*out*/, &cb /*out*/));

        std::shared_ptr<ResultMetadataQueue> resultQueue;
        ::aidl::android::hardware::common::fmq::MQDescriptor<
                int8_t, aidl::android::hardware::common::fmq::SynchronizedReadWrite>
                descriptor;
        ndk::ScopedAStatus ret = session->getCaptureResultMetadataQueue(&descriptor);
        ASSERT_TRUE(ret.isOk());

        resultQueue = std::make_shared<ResultMetadataQueue>(descriptor);
        if (!resultQueue->isValid() || resultQueue->availableToWrite() <= 0) {
            ALOGE("%s: HAL returns empty result metadata fmq,"
                  " not use it",
                  __func__);
            resultQueue = nullptr;
            // Don't use the queue onwards.
        }

        std::shared_ptr<InFlightRequest> inflightReq = std::make_shared<InFlightRequest>(
                1, false, supportsPartialResults, partialResultCount, resultQueue);

        CameraMetadata req;
        ret = session->constructDefaultRequestSettings(reqTemplate, &req);
        ASSERT_TRUE(ret.isOk());
        settings = req;

        overrideRotateAndCrop(&settings);

        std::vector<CaptureRequest> requests(1);
        CaptureRequest& request = requests[0];
        request.frameNumber = frameNumber;
        request.fmqSettingsSize = 0;
        request.settings = settings;

        std::vector<StreamBuffer>& outputBuffers = request.outputBuffers;
        outputBuffers.resize(1);
        StreamBuffer& outputBuffer = outputBuffers[0];
        if (useHalBufManager) {
            outputBuffer = {halStreams[0].id,
                            /*bufferId*/ 0,   NativeHandle(), BufferStatus::OK,
                            NativeHandle(),   NativeHandle()};
        } else {
            buffer_handle_t handle;
            allocateGraphicBuffer(
                    testStream.width, testStream.height,
                    /* We don't look at halStreamConfig.streams[0].consumerUsage
                     * since that is 0 for output streams
                     */
                    ANDROID_NATIVE_UNSIGNED_CAST(android_convertGralloc1To0Usage(
                            static_cast<uint64_t>(halStreams[0].producerUsage), bufferUsage)),
                    halStreams[0].overrideFormat, &handle);

            outputBuffer = {halStreams[0].id, bufferId,       ::android::makeToAidl(handle),
                            BufferStatus::OK, NativeHandle(), NativeHandle()};
        }
        request.inputBuffer = {-1,
                               0,
                               NativeHandle(),
                               BufferStatus::ERROR,
                               NativeHandle(),
                               NativeHandle()};  // Empty Input Buffer

        {
            std::unique_lock<std::mutex> l(mLock);
            mInflightMap.clear();
            mInflightMap.insert(std::make_pair(frameNumber, inflightReq));
        }

        int32_t numRequestProcessed = 0;
        std::vector<BufferCache> cachesToRemove;
        ret = session->processCaptureRequest(requests, cachesToRemove, &numRequestProcessed);
        ALOGI("processCaptureRequestInternal: processCaptureRequest returns status: %d:%d",
              ret.getExceptionCode(), ret.getServiceSpecificError());

        ASSERT_TRUE(ret.isOk());
        ASSERT_EQ(numRequestProcessed, 1u);

        {
            std::unique_lock<std::mutex> l(mLock);
            while (!inflightReq->errorCodeValid &&
                   ((0 < inflightReq->numBuffersLeft) || (!inflightReq->haveResultMetadata))) {
                auto timeout = std::chrono::system_clock::now() +
                               std::chrono::seconds(kStreamBufferTimeoutSec);
                ASSERT_NE(std::cv_status::timeout, mResultCondition.wait_until(l, timeout));
            }

            ASSERT_FALSE(inflightReq->errorCodeValid);
            ASSERT_NE(inflightReq->resultOutputBuffers.size(), 0u);
            ASSERT_EQ(testStream.id, inflightReq->resultOutputBuffers[0].buffer.streamId);

            // shutterReadoutTimestamp, if supported, must
            // be >= shutterTimestamp + exposureTime,
            // and < shutterTimestamp + exposureTime + rollingShutterSkew / 2.
            ASSERT_FALSE(inflightReq->collectedResult.isEmpty());

            if (mSupportReadoutTimestamp &&
                inflightReq->collectedResult.exists(ANDROID_SENSOR_EXPOSURE_TIME)) {
                camera_metadata_entry_t exposureTimeResult =
                        inflightReq->collectedResult.find(ANDROID_SENSOR_EXPOSURE_TIME);
                nsecs_t exposureToReadout =
                        inflightReq->shutterReadoutTimestamp - inflightReq->shutterTimestamp;
                ASSERT_GE(exposureToReadout, exposureTimeResult.data.i64[0]);
                if (inflightReq->collectedResult.exists(ANDROID_SENSOR_ROLLING_SHUTTER_SKEW)) {
                    camera_metadata_entry_t rollingShutterSkew =
                            inflightReq->collectedResult.find(ANDROID_SENSOR_ROLLING_SHUTTER_SKEW);
                    ASSERT_LT(exposureToReadout,
                              exposureTimeResult.data.i64[0] + rollingShutterSkew.data.i64[0] / 2);
                }
            }

            request.frameNumber++;
            // Empty settings should be supported after the first call
            // for repeating requests.
            request.settings.metadata.clear();
            // The buffer has been registered to HAL by bufferId, so per
            // API contract we should send a null handle for this buffer
            request.outputBuffers[0].buffer = NativeHandle();
            mInflightMap.clear();
            inflightReq = std::make_shared<InFlightRequest>(1, false, supportsPartialResults,
                                                            partialResultCount, resultQueue);
            mInflightMap.insert(std::make_pair(request.frameNumber, inflightReq));
        }

        ret = session->processCaptureRequest(requests, cachesToRemove, &numRequestProcessed);
        ALOGI("processCaptureRequestInternal: processCaptureRequest returns status: %d:%d",
              ret.getExceptionCode(), ret.getServiceSpecificError());
        ASSERT_TRUE(ret.isOk());
        ASSERT_EQ(numRequestProcessed, 1u);

        {
            std::unique_lock<std::mutex> l(mLock);
            while (!inflightReq->errorCodeValid &&
                   ((0 < inflightReq->numBuffersLeft) || (!inflightReq->haveResultMetadata))) {
                auto timeout = std::chrono::system_clock::now() +
                               std::chrono::seconds(kStreamBufferTimeoutSec);
                ASSERT_NE(std::cv_status::timeout, mResultCondition.wait_until(l, timeout));
            }

            ASSERT_FALSE(inflightReq->errorCodeValid);
            ASSERT_NE(inflightReq->resultOutputBuffers.size(), 0u);
            ASSERT_EQ(testStream.id, inflightReq->resultOutputBuffers[0].buffer.streamId);
        }

        if (useHalBufManager) {
            verifyBuffersReturned(session, testStream.id, cb);
        }

        ret = session->close();
        ASSERT_TRUE(ret.isOk());
    }
}

void CameraAidlTest::configureStreamUseCaseInternal(const AvailableStream &threshold) {
    std::vector<std::string> cameraDeviceNames = getCameraDeviceNames(mProvider);

    for (const auto& name : cameraDeviceNames) {
        CameraMetadata meta;
        std::shared_ptr<ICameraDevice> cameraDevice;

        ASSERT_NO_FATAL_FAILURE(openEmptyDeviceSession(name, mProvider, &mSession /*out*/,
                                                       &meta /*out*/, &cameraDevice /*out*/));

        camera_metadata_t* staticMeta = reinterpret_cast<camera_metadata_t*>(meta.metadata.data());
        // Check if camera support depth only or doesn't support stream use case capability
        if (isDepthOnly(staticMeta) || !supportsStreamUseCaseCap(staticMeta) ||
            (threshold.format == static_cast<int32_t>(PixelFormat::RAW16) &&
             !supportsCroppedRawUseCase(staticMeta))) {
            ndk::ScopedAStatus ret = mSession->close();
            mSession = nullptr;
            ASSERT_TRUE(ret.isOk());
            continue;
        }

        std::vector<AvailableStream> outputPreviewStreams;

        ASSERT_EQ(Status::OK,
                  getAvailableOutputStreams(staticMeta, outputPreviewStreams, &threshold));
        ASSERT_NE(0u, outputPreviewStreams.size());

        // Combine valid and invalid stream use cases
        std::vector<int64_t> testedUseCases;
        testedUseCases.push_back(ANDROID_SCALER_AVAILABLE_STREAM_USE_CASES_CROPPED_RAW + 1);

        std::vector<int64_t> supportedUseCases;
        if (threshold.format == static_cast<int32_t>(PixelFormat::RAW16)) {
            // If the format is RAW16, supported use case is only CROPPED_RAW.
            // All others are unsupported for this format.
            testedUseCases.push_back(ANDROID_SCALER_AVAILABLE_STREAM_USE_CASES_CROPPED_RAW);
            supportedUseCases.push_back(ANDROID_SCALER_AVAILABLE_STREAM_USE_CASES_CROPPED_RAW);
            supportedUseCases.push_back(ANDROID_SCALER_AVAILABLE_STREAM_USE_CASES_DEFAULT);
        } else {
            camera_metadata_ro_entry entry;
            testedUseCases.insert(testedUseCases.end(), kMandatoryUseCases.begin(),
                                  kMandatoryUseCases.end());
            auto retcode = find_camera_metadata_ro_entry(
                    staticMeta, ANDROID_SCALER_AVAILABLE_STREAM_USE_CASES, &entry);
            if ((0 == retcode) && (entry.count > 0)) {
                supportedUseCases.insert(supportedUseCases.end(), entry.data.i64,
                                         entry.data.i64 + entry.count);
            } else {
                supportedUseCases.push_back(ANDROID_SCALER_AVAILABLE_STREAM_USE_CASES_DEFAULT);
            }
        }

        std::vector<Stream> streams(1);
        streams[0] = {
                0,
                StreamType::OUTPUT,
                outputPreviewStreams[0].width,
                outputPreviewStreams[0].height,
                static_cast<PixelFormat>(outputPreviewStreams[0].format),
                static_cast<::aidl::android::hardware::graphics::common::BufferUsage>(
                        GRALLOC1_CONSUMER_USAGE_CPU_READ),
                Dataspace::UNKNOWN,
                StreamRotation::ROTATION_0,
                std::string(),
                0,
                -1,
                {SensorPixelMode::ANDROID_SENSOR_PIXEL_MODE_DEFAULT},
                RequestAvailableDynamicRangeProfilesMap::
                        ANDROID_REQUEST_AVAILABLE_DYNAMIC_RANGE_PROFILES_MAP_STANDARD,
                ScalerAvailableStreamUseCases::ANDROID_SCALER_AVAILABLE_STREAM_USE_CASES_DEFAULT,
                static_cast<int>(
                        RequestAvailableColorSpaceProfilesMap::
                                ANDROID_REQUEST_AVAILABLE_COLOR_SPACE_PROFILES_MAP_UNSPECIFIED)};

        int32_t streamConfigCounter = 0;
        CameraMetadata req;
        StreamConfiguration config;
        RequestTemplate reqTemplate = RequestTemplate::STILL_CAPTURE;
        ndk::ScopedAStatus ret = mSession->constructDefaultRequestSettings(reqTemplate, &req);
        ASSERT_TRUE(ret.isOk());
        config.sessionParams = req;

        for (int64_t useCase : testedUseCases) {
            bool useCaseSupported = std::find(supportedUseCases.begin(), supportedUseCases.end(),
                                              useCase) != supportedUseCases.end();

            streams[0].useCase = static_cast<
                    aidl::android::hardware::camera::metadata::ScalerAvailableStreamUseCases>(
                    useCase);
            config.streams = streams;
            config.operationMode = StreamConfigurationMode::NORMAL_MODE;
            config.streamConfigCounter = streamConfigCounter;
            config.multiResolutionInputImage = false;

            bool combSupported;
            ret = cameraDevice->isStreamCombinationSupported(config, &combSupported);
            if (static_cast<int32_t>(Status::OPERATION_NOT_SUPPORTED) ==
                ret.getServiceSpecificError()) {
                continue;
            }

            ASSERT_TRUE(ret.isOk());
            ASSERT_EQ(combSupported, useCaseSupported);

            std::vector<HalStream> halStreams;
            ret = mSession->configureStreams(config, &halStreams);
            ALOGI("configureStreams returns status: %d", ret.getServiceSpecificError());
            if (useCaseSupported) {
                ASSERT_TRUE(ret.isOk());
                ASSERT_EQ(1u, halStreams.size());
            } else {
                ASSERT_EQ(static_cast<int32_t>(Status::ILLEGAL_ARGUMENT),
                          ret.getServiceSpecificError());
            }
        }
        ret = mSession->close();
        mSession = nullptr;
        ASSERT_TRUE(ret.isOk());
    }

}

ndk::ScopedAStatus CameraAidlTest::configureStreams(std::shared_ptr<ICameraDeviceSession>& session,
                                                    const StreamConfiguration& config,
                                                    BufferManagerType bufferManagerType,
                                                    std::set<int32_t>* halBufManagedStreamIds,
                                                    std::vector<HalStream>* halStreams) {
    auto ret = ndk::ScopedAStatus::ok();
    ConfigureStreamsRet aidl_return;
    int32_t interfaceVersion = -1;
    ret = session->getInterfaceVersion(&interfaceVersion);
    if (!ret.isOk()) {
        return ret;
    }

    if (bufferManagerType == BufferManagerType::SESSION && interfaceVersion >= 3) {
        ret = session->configureStreamsV2(config, &aidl_return);
    } else {
        ret = session->configureStreams(config, halStreams);
    }
    if (!ret.isOk()) {
        return ret;
    }
    if (bufferManagerType == BufferManagerType::SESSION) {
        *halStreams = std::move(aidl_return.halStreams);
    }
    for (const auto& halStream : *halStreams) {
        if ((bufferManagerType == BufferManagerType::SESSION && halStream.enableHalBufferManager) ||
            bufferManagerType == BufferManagerType::HAL) {
            halBufManagedStreamIds->insert(halStream.id);
        }
    }
    return ndk::ScopedAStatus::ok();
}

void CameraAidlTest::configureSingleStream(
        const std::string& name, const std::shared_ptr<ICameraProvider>& provider,
        const AvailableStream* previewThreshold, uint64_t bufferUsage, RequestTemplate reqTemplate,
        std::shared_ptr<ICameraDeviceSession>* session, Stream* previewStream,
        std::vector<HalStream>* halStreams, bool* supportsPartialResults,
        int32_t* partialResultCount, bool* useHalBufManager, std::shared_ptr<DeviceCb>* cb,
        uint32_t streamConfigCounter) {
    ASSERT_NE(nullptr, session);
    ASSERT_NE(nullptr, previewStream);
    ASSERT_NE(nullptr, halStreams);
    ASSERT_NE(nullptr, supportsPartialResults);
    ASSERT_NE(nullptr, partialResultCount);
    ASSERT_NE(nullptr, useHalBufManager);
    ASSERT_NE(nullptr, cb);

    std::vector<AvailableStream> outputPreviewStreams;
    std::shared_ptr<ICameraDevice> device;
    ALOGI("configureStreams: Testing camera device %s", name.c_str());

    ndk::ScopedAStatus ret = provider->getCameraDeviceInterface(name, &device);
    ALOGI("getCameraDeviceInterface returns status:%d:%d", ret.getExceptionCode(),
          ret.getServiceSpecificError());
    ASSERT_TRUE(ret.isOk());
    ASSERT_NE(device, nullptr);

    camera_metadata_t* staticMeta;
    CameraMetadata chars;
    ret = device->getCameraCharacteristics(&chars);
    ASSERT_TRUE(ret.isOk());
    staticMeta = clone_camera_metadata(
            reinterpret_cast<const camera_metadata_t*>(chars.metadata.data()));
    ASSERT_NE(nullptr, staticMeta);

    size_t expectedSize = chars.metadata.size();
    ALOGE("validate_camera_metadata_structure: %d",
          validate_camera_metadata_structure(staticMeta, &expectedSize));

    camera_metadata_ro_entry entry;
    auto status =
            find_camera_metadata_ro_entry(staticMeta, ANDROID_REQUEST_PARTIAL_RESULT_COUNT, &entry);
    if ((0 == status) && (entry.count > 0)) {
        *partialResultCount = entry.data.i32[0];
        *supportsPartialResults = (*partialResultCount > 1);
    }

    *cb = ndk::SharedRefBase::make<DeviceCb>(this, staticMeta);

    device->open(*cb, session);
    ALOGI("device::open returns status:%d:%d", ret.getExceptionCode(),
          ret.getServiceSpecificError());
    ASSERT_TRUE(ret.isOk());
    ASSERT_NE(*session, nullptr);

    BufferManagerType bufferManagerType = BufferManagerType::FRAMEWORK;
    status = find_camera_metadata_ro_entry(
            staticMeta, ANDROID_INFO_SUPPORTED_BUFFER_MANAGEMENT_VERSION, &entry);
    if ((0 == status) && (entry.count == 1)) {
        if (entry.data.u8[0] == ANDROID_INFO_SUPPORTED_BUFFER_MANAGEMENT_VERSION_HIDL_DEVICE_3_5) {
            bufferManagerType = BufferManagerType::HAL;
        } else if (entry.data.u8[0] ==
                   ANDROID_INFO_SUPPORTED_BUFFER_MANAGEMENT_VERSION_SESSION_CONFIGURABLE) {
            bufferManagerType = BufferManagerType::SESSION;
        }
    }

    outputPreviewStreams.clear();
    auto rc = getAvailableOutputStreams(staticMeta, outputPreviewStreams, previewThreshold);

    int32_t jpegBufferSize = 0;
    ASSERT_EQ(Status::OK, getJpegBufferSize(staticMeta, &jpegBufferSize));
    ASSERT_NE(0u, jpegBufferSize);

    ASSERT_EQ(Status::OK, rc);
    ASSERT_FALSE(outputPreviewStreams.empty());

    Dataspace dataspace = Dataspace::UNKNOWN;
    switch (static_cast<PixelFormat>(outputPreviewStreams[0].format)) {
        case PixelFormat::Y16:
            dataspace = Dataspace::DEPTH;
            break;
        default:
            dataspace = Dataspace::UNKNOWN;
    }

    std::vector<Stream> streams(1);
    streams[0] = {0,
                  StreamType::OUTPUT,
                  outputPreviewStreams[0].width,
                  outputPreviewStreams[0].height,
                  static_cast<PixelFormat>(outputPreviewStreams[0].format),
                  static_cast<aidl::android::hardware::graphics::common::BufferUsage>(bufferUsage),
                  dataspace,
                  StreamRotation::ROTATION_0,
                  "",
                  0,
                  /*groupId*/ -1,
                  {SensorPixelMode::ANDROID_SENSOR_PIXEL_MODE_DEFAULT},
                  RequestAvailableDynamicRangeProfilesMap::
                          ANDROID_REQUEST_AVAILABLE_DYNAMIC_RANGE_PROFILES_MAP_STANDARD,
                  ScalerAvailableStreamUseCases::ANDROID_SCALER_AVAILABLE_STREAM_USE_CASES_DEFAULT,
                  static_cast<int>(
                          RequestAvailableColorSpaceProfilesMap::
                                  ANDROID_REQUEST_AVAILABLE_COLOR_SPACE_PROFILES_MAP_UNSPECIFIED)};

    StreamConfiguration config;
    config.streams = streams;
    createStreamConfiguration(streams, StreamConfigurationMode::NORMAL_MODE, &config,
                              jpegBufferSize);

    CameraMetadata sessionParams;
    ret = (*session)->constructDefaultRequestSettings(reqTemplate, &sessionParams);
    ASSERT_TRUE(ret.isOk());
    config.sessionParams = sessionParams;
    config.streamConfigCounter = (int32_t)streamConfigCounter;

    bool supported = false;
    ret = device->isStreamCombinationSupported(config, &supported);
    ASSERT_TRUE(ret.isOk());
    ASSERT_EQ(supported, true);

    std::vector<HalStream> halConfigs;
    std::set<int32_t> halBufManagedStreamIds;
    ret = configureStreams(*session, config, bufferManagerType, &halBufManagedStreamIds,
                           &halConfigs);
    ALOGI("configureStreams returns status: %d:%d", ret.getExceptionCode(),
          ret.getServiceSpecificError());
    ASSERT_TRUE(ret.isOk());
    ASSERT_EQ(1u, halConfigs.size());
    halStreams->clear();
    halStreams->push_back(halConfigs[0]);
    *useHalBufManager = halBufManagedStreamIds.size() != 0;
    if (*useHalBufManager) {
        std::vector<Stream> ss(1);
        std::vector<HalStream> hs(1);
        ss[0] = config.streams[0];
        hs[0] = halConfigs[0];
        (*cb)->setCurrentStreamConfig(ss, hs);
    }

    *previewStream = config.streams[0];
    ASSERT_TRUE(ret.isOk());
}

void CameraAidlTest::overrideRotateAndCrop(CameraMetadata* settings) {
    if (settings == nullptr) {
        return;
    }

    ::android::hardware::camera::common::V1_0::helper::CameraMetadata requestMeta =
            clone_camera_metadata(reinterpret_cast<camera_metadata_t*>(settings->metadata.data()));
    auto entry = requestMeta.find(ANDROID_SCALER_ROTATE_AND_CROP);
    if ((entry.count > 0) && (entry.data.u8[0] == ANDROID_SCALER_ROTATE_AND_CROP_AUTO)) {
        uint8_t disableRotateAndCrop = ANDROID_SCALER_ROTATE_AND_CROP_NONE;
        requestMeta.update(ANDROID_SCALER_ROTATE_AND_CROP, &disableRotateAndCrop, 1);
        settings->metadata.clear();
        camera_metadata_t* metaBuffer = requestMeta.release();
        uint8_t* rawMetaBuffer = reinterpret_cast<uint8_t*>(metaBuffer);
        settings->metadata =
                std::vector(rawMetaBuffer, rawMetaBuffer + get_camera_metadata_size(metaBuffer));
    }
}

void CameraAidlTest::verifyBuffersReturned(const std::shared_ptr<ICameraDeviceSession>& session,
                                           int32_t streamId, const std::shared_ptr<DeviceCb>& cb,
                                           uint32_t streamConfigCounter) {
    ASSERT_NE(nullptr, session);

    std::vector<int32_t> streamIds(1);
    streamIds[0] = streamId;
    session->signalStreamFlush(streamIds, /*streamConfigCounter*/ streamConfigCounter);
    cb->waitForBuffersReturned();
}

void CameraAidlTest::processPreviewStabilizationCaptureRequestInternal(
        bool previewStabilizationOn,
        // Used as output when preview stabilization is off, as output when its on.
        std::unordered_map<std::string, nsecs_t>& cameraDeviceToTimeLag) {
    std::vector<std::string> cameraDeviceNames = getCameraDeviceNames(mProvider);
    AvailableStream streamThreshold = {kMaxPreviewWidth, kMaxPreviewHeight,
                                       static_cast<int32_t>(PixelFormat::IMPLEMENTATION_DEFINED)};
    int64_t bufferId = 1;
    int32_t frameNumber = 1;
    std::vector<uint8_t> settings;

    for (const auto& name : cameraDeviceNames) {
        if (!supportsPreviewStabilization(name, mProvider)) {
            ALOGI(" %s Camera device %s doesn't support preview stabilization, skipping", __func__,
                  name.c_str());
            continue;
        }

        Stream testStream;
        std::vector<HalStream> halStreams;
        std::shared_ptr<ICameraDeviceSession> session;
        std::shared_ptr<DeviceCb> cb;
        bool supportsPartialResults = false;
        bool useHalBufManager = false;
        int32_t partialResultCount = 0;
        ASSERT_NO_FATAL_FAILURE(configureSingleStream(
                name, mProvider, &streamThreshold, GRALLOC1_CONSUMER_USAGE_HWCOMPOSER,
                RequestTemplate::PREVIEW, &session /*out*/, &testStream /*out*/,
                &halStreams /*out*/, &supportsPartialResults /*out*/, &partialResultCount /*out*/,
                &useHalBufManager /*out*/, &cb /*out*/));

        ::aidl::android::hardware::common::fmq::MQDescriptor<
                int8_t, aidl::android::hardware::common::fmq::SynchronizedReadWrite>
                descriptor;
        ndk::ScopedAStatus resultQueueRet = session->getCaptureResultMetadataQueue(&descriptor);
        ASSERT_TRUE(resultQueueRet.isOk());

        std::shared_ptr<ResultMetadataQueue> resultQueue =
                std::make_shared<ResultMetadataQueue>(descriptor);
        if (!resultQueue->isValid() || resultQueue->availableToWrite() <= 0) {
            ALOGE("%s: HAL returns empty result metadata fmq,"
                  " not use it",
                  __func__);
            resultQueue = nullptr;
            // Don't use the queue onwards.
        }

        std::shared_ptr<InFlightRequest> inflightReq = std::make_shared<InFlightRequest>(
                1, false, supportsPartialResults, partialResultCount, resultQueue);

        CameraMetadata defaultMetadata;
        android::hardware::camera::common::V1_0::helper::CameraMetadata defaultSettings;
        ndk::ScopedAStatus ret = session->constructDefaultRequestSettings(RequestTemplate::PREVIEW,
                                                                          &defaultMetadata);
        ASSERT_TRUE(ret.isOk());

        const camera_metadata_t* metadata =
                reinterpret_cast<const camera_metadata_t*>(defaultMetadata.metadata.data());
        defaultSettings = metadata;
        android::status_t metadataRet = ::android::OK;
        uint8_t videoStabilizationMode = ANDROID_CONTROL_VIDEO_STABILIZATION_MODE_OFF;
        if (previewStabilizationOn) {
            videoStabilizationMode = ANDROID_CONTROL_VIDEO_STABILIZATION_MODE_PREVIEW_STABILIZATION;
            metadataRet = defaultSettings.update(ANDROID_CONTROL_VIDEO_STABILIZATION_MODE,
                                                 &videoStabilizationMode, 1);
        } else {
            metadataRet = defaultSettings.update(ANDROID_CONTROL_VIDEO_STABILIZATION_MODE,
                                                 &videoStabilizationMode, 1);
        }
        ASSERT_EQ(metadataRet, ::android::OK);

        camera_metadata_t* releasedMetadata = defaultSettings.release();
        uint8_t* rawMetadata = reinterpret_cast<uint8_t*>(releasedMetadata);

        buffer_handle_t buffer_handle;

        std::vector<CaptureRequest> requests(1);
        CaptureRequest& request = requests[0];
        request.frameNumber = frameNumber;
        request.fmqSettingsSize = 0;
        request.settings.metadata =
                std::vector(rawMetadata, rawMetadata + get_camera_metadata_size(releasedMetadata));
        overrideRotateAndCrop(&request.settings);
        request.outputBuffers = std::vector<StreamBuffer>(1);
        StreamBuffer& outputBuffer = request.outputBuffers[0];

        if (useHalBufManager) {
            outputBuffer = {halStreams[0].id,
                            /*bufferId*/ 0,   NativeHandle(), BufferStatus::OK,
                            NativeHandle(),   NativeHandle()};
        } else {
            allocateGraphicBuffer(testStream.width, testStream.height,
                                  /* We don't look at halStreamConfig.streams[0].consumerUsage
                                   * since that is 0 for output streams
                                   */
                                  ANDROID_NATIVE_UNSIGNED_CAST(android_convertGralloc1To0Usage(
                                          static_cast<uint64_t>(halStreams[0].producerUsage),
                                          GRALLOC1_CONSUMER_USAGE_HWCOMPOSER)),
                                  halStreams[0].overrideFormat, &buffer_handle);
            outputBuffer = {halStreams[0].id, bufferId,       ::android::makeToAidl(buffer_handle),
                            BufferStatus::OK, NativeHandle(), NativeHandle()};
        }
        request.inputBuffer = {
                -1, 0, NativeHandle(), BufferStatus::ERROR, NativeHandle(), NativeHandle()};

        {
            std::unique_lock<std::mutex> l(mLock);
            mInflightMap.clear();
            mInflightMap.insert(std::make_pair(frameNumber, inflightReq));
        }

        int32_t numRequestProcessed = 0;
        std::vector<BufferCache> cachesToRemove;
        ret = session->processCaptureRequest(requests, cachesToRemove, &numRequestProcessed);
        ASSERT_TRUE(ret.isOk());
        ASSERT_EQ(numRequestProcessed, 1u);

        {
            std::unique_lock<std::mutex> l(mLock);
            while (!inflightReq->errorCodeValid &&
                   ((0 < inflightReq->numBuffersLeft) || (!inflightReq->haveResultMetadata))) {
                auto timeout = std::chrono::system_clock::now() +
                               std::chrono::seconds(kStreamBufferTimeoutSec);
                ASSERT_NE(std::cv_status::timeout, mResultCondition.wait_until(l, timeout));
            }
            waitForReleaseFence(inflightReq->resultOutputBuffers);

            ASSERT_FALSE(inflightReq->errorCodeValid);
            ASSERT_NE(inflightReq->resultOutputBuffers.size(), 0u);
            ASSERT_EQ(testStream.id, inflightReq->resultOutputBuffers[0].buffer.streamId);
            nsecs_t captureTimestamp = mSupportReadoutTimestamp
                                               ? inflightReq->shutterReadoutTimestamp
                                               : inflightReq->shutterTimestamp;

            if (previewStabilizationOn) {
                // Here we collect the time difference between the buffer ready
                // timestamp - notify timestamp.
                // timeLag = buffer ready timestamp - notify timestamp.
                // timeLag(previewStabilization) must be <=
                //        timeLag(stabilization off) + 1 frame duration.
                auto it = cameraDeviceToTimeLag.find(name);
                camera_metadata_entry e;
                e = inflightReq->collectedResult.find(ANDROID_SENSOR_FRAME_DURATION);
                ASSERT_TRUE(e.count > 0);
                nsecs_t frameDuration = e.data.i64[0];
                ASSERT_TRUE(it != cameraDeviceToTimeLag.end());

                nsecs_t previewStabOnLagTime =
                        inflightReq->resultOutputBuffers[0].timeStamp - captureTimestamp;
                ASSERT_TRUE(previewStabOnLagTime <= (it->second + frameDuration));
            } else {
                // Fill in the buffer ready timestamp - notify timestamp;
                cameraDeviceToTimeLag[std::string(name)] =
                        inflightReq->resultOutputBuffers[0].timeStamp - captureTimestamp;
            }
        }

        if (useHalBufManager) {
            verifyBuffersReturned(session, testStream.id, cb);
        }

        ret = session->close();
        ASSERT_TRUE(ret.isOk());
    }
}

bool CameraAidlTest::supportsPreviewStabilization(
        const std::string& name, const std::shared_ptr<ICameraProvider>& provider) {
    std::shared_ptr<ICameraDevice> device;
    ndk::ScopedAStatus ret = provider->getCameraDeviceInterface(name, &device);
    ALOGI("getCameraDeviceInterface returns status:%d:%d", ret.getExceptionCode(),
          ret.getServiceSpecificError());
    if (!ret.isOk() || device == nullptr) {
        ADD_FAILURE() << "Failed to get camera device interface for " << name;
    }

    CameraMetadata metadata;
    ret = device->getCameraCharacteristics(&metadata);
    camera_metadata_t* staticMeta = clone_camera_metadata(
            reinterpret_cast<const camera_metadata_t*>(metadata.metadata.data()));
    if (!(ret.isOk())) {
        ADD_FAILURE() << "Failed to get camera characteristics for " << name;
    }
    // Go through the characteristics and see if video stabilization modes have
    // preview stabilization
    camera_metadata_ro_entry entry;

    int retcode = find_camera_metadata_ro_entry(
            staticMeta, ANDROID_CONTROL_AVAILABLE_VIDEO_STABILIZATION_MODES, &entry);
    if ((0 == retcode) && (entry.count > 0)) {
        for (auto i = 0; i < entry.count; i++) {
            if (entry.data.u8[i] ==
                ANDROID_CONTROL_VIDEO_STABILIZATION_MODE_PREVIEW_STABILIZATION) {
                return true;
            }
        }
    }
    return false;
}

void CameraAidlTest::configurePreviewStreams(
        const std::string& name, const std::shared_ptr<ICameraProvider>& provider,
        const AvailableStream* previewThreshold, const std::unordered_set<std::string>& physicalIds,
        std::shared_ptr<ICameraDeviceSession>* session, Stream* previewStream,
        std::vector<HalStream>* halStreams, bool* supportsPartialResults,
        int32_t* partialResultCount, std::set<int32_t>* halBufManagedStreamIds,
        std::shared_ptr<DeviceCb>* cb, int32_t streamConfigCounter, bool allowUnsupport) {
    ASSERT_NE(nullptr, session);
    ASSERT_NE(nullptr, halStreams);
    ASSERT_NE(nullptr, previewStream);
    ASSERT_NE(nullptr, supportsPartialResults);
    ASSERT_NE(nullptr, partialResultCount);
    ASSERT_NE(nullptr, halBufManagedStreamIds);
    ASSERT_NE(nullptr, cb);

    ASSERT_FALSE(physicalIds.empty());

    std::vector<AvailableStream> outputPreviewStreams;
    std::shared_ptr<ICameraDevice> device;
    ALOGI("configureStreams: Testing camera device %s", name.c_str());

    ndk::ScopedAStatus ret = provider->getCameraDeviceInterface(name, &device);
    ALOGI("getCameraDeviceInterface returns status:%d:%d", ret.getExceptionCode(),
          ret.getServiceSpecificError());
    ASSERT_TRUE(ret.isOk());
    ASSERT_NE(device, nullptr);

    CameraMetadata meta;
    ret = device->getCameraCharacteristics(&meta);
    ASSERT_TRUE(ret.isOk());
    camera_metadata_t* staticMeta =
            clone_camera_metadata(reinterpret_cast<const camera_metadata_t*>(meta.metadata.data()));
    ASSERT_NE(nullptr, staticMeta);

    camera_metadata_ro_entry entry;
    auto status =
            find_camera_metadata_ro_entry(staticMeta, ANDROID_REQUEST_PARTIAL_RESULT_COUNT, &entry);
    if ((0 == status) && (entry.count > 0)) {
        *partialResultCount = entry.data.i32[0];
        *supportsPartialResults = (*partialResultCount > 1);
    }

    *cb = ndk::SharedRefBase::make<DeviceCb>(this, staticMeta);
    ret = device->open(*cb, session);
    ALOGI("device::open returns status:%d:%d", ret.getExceptionCode(),
          ret.getServiceSpecificError());
    ASSERT_TRUE(ret.isOk());
    ASSERT_NE(*session, nullptr);

    BufferManagerType bufferManagerType = BufferManagerType::FRAMEWORK;
    status = find_camera_metadata_ro_entry(
            staticMeta, ANDROID_INFO_SUPPORTED_BUFFER_MANAGEMENT_VERSION, &entry);
    if ((0 == status) && (entry.count == 1)) {
        if (entry.data.u8[0] == ANDROID_INFO_SUPPORTED_BUFFER_MANAGEMENT_VERSION_HIDL_DEVICE_3_5) {
            bufferManagerType = BufferManagerType::HAL;
        } else if (entry.data.u8[0] ==
                   ANDROID_INFO_SUPPORTED_BUFFER_MANAGEMENT_VERSION_SESSION_CONFIGURABLE) {
            bufferManagerType = BufferManagerType::SESSION;
        }
    }

    outputPreviewStreams.clear();
    Status rc = getAvailableOutputStreams(staticMeta, outputPreviewStreams, previewThreshold);

    ASSERT_EQ(Status::OK, rc);
    ASSERT_FALSE(outputPreviewStreams.empty());

    std::vector<Stream> streams(physicalIds.size());
    int32_t streamId = 0;
    for (auto const& physicalId : physicalIds) {
        streams[streamId] = {
                streamId,
                StreamType::OUTPUT,
                outputPreviewStreams[0].width,
                outputPreviewStreams[0].height,
                static_cast<PixelFormat>(outputPreviewStreams[0].format),
                static_cast<aidl::android::hardware::graphics::common::BufferUsage>(
                        GRALLOC1_CONSUMER_USAGE_HWCOMPOSER),
                Dataspace::UNKNOWN,
                StreamRotation::ROTATION_0,
                physicalId,
                0,
                -1,
                {SensorPixelMode::ANDROID_SENSOR_PIXEL_MODE_DEFAULT},
                RequestAvailableDynamicRangeProfilesMap::
                        ANDROID_REQUEST_AVAILABLE_DYNAMIC_RANGE_PROFILES_MAP_STANDARD,
                ScalerAvailableStreamUseCases::ANDROID_SCALER_AVAILABLE_STREAM_USE_CASES_DEFAULT,
                static_cast<int>(
                        RequestAvailableColorSpaceProfilesMap::
                                ANDROID_REQUEST_AVAILABLE_COLOR_SPACE_PROFILES_MAP_UNSPECIFIED)};
        streamId++;
    }

    StreamConfiguration config = {streams, StreamConfigurationMode::NORMAL_MODE, CameraMetadata()};

    RequestTemplate reqTemplate = RequestTemplate::PREVIEW;
    ret = (*session)->constructDefaultRequestSettings(reqTemplate, &config.sessionParams);
    ASSERT_TRUE(ret.isOk());

    bool supported = false;
    ret = device->isStreamCombinationSupported(config, &supported);
    ASSERT_TRUE(ret.isOk());
    if (allowUnsupport && !supported) {
        // stream combination not supported. return null session
        ret = (*session)->close();
        ASSERT_TRUE(ret.isOk());
        *session = nullptr;
        return;
    }
    ASSERT_TRUE(supported) << "Stream combination must be supported.";

    config.streamConfigCounter = streamConfigCounter;
    std::vector<HalStream> halConfigs;
    ret = configureStreams(*session, config, bufferManagerType, halBufManagedStreamIds,
                           &halConfigs);

    ASSERT_TRUE(ret.isOk());
    ASSERT_EQ(physicalIds.size(), halConfigs.size());
    *halStreams = halConfigs;
    if (halBufManagedStreamIds->size() != 0) {
        // Only include the streams that are HAL buffer managed
        std::vector<Stream> ss;
        std::vector<HalStream> hs;
        for (size_t i = 0; i < physicalIds.size(); i++) {
            if (contains(*halBufManagedStreamIds, halConfigs[i].id)) {
                ss.emplace_back(streams[i]);
                hs.emplace_back(halConfigs[i]);
            }
        }
        (*cb)->setCurrentStreamConfig(ss, hs);
    }
    *previewStream = streams[0];
    ASSERT_TRUE(ret.isOk());
}

void CameraAidlTest::verifyBuffersReturned(const std::shared_ptr<ICameraDeviceSession>& session,
                                           const std::vector<int32_t>& streamIds,
                                           const std::shared_ptr<DeviceCb>& cb,
                                           uint32_t streamConfigCounter) {
    ndk::ScopedAStatus ret =
            session->signalStreamFlush(streamIds, /*streamConfigCounter*/ streamConfigCounter);
    ASSERT_TRUE(ret.isOk());
    cb->waitForBuffersReturned();
}

void CameraAidlTest::configureStreams(
        const std::string& name, const std::shared_ptr<ICameraProvider>& provider,
        PixelFormat format, std::shared_ptr<ICameraDeviceSession>* session, Stream* previewStream,
        std::vector<HalStream>* halStreams, bool* supportsPartialResults,
        int32_t* partialResultCount, std::set<int32_t>* halBufManagedStreamIds,
        std::shared_ptr<DeviceCb>* outCb, uint32_t streamConfigCounter, bool maxResolution,
        RequestAvailableDynamicRangeProfilesMap dynamicRangeProf,
        RequestAvailableColorSpaceProfilesMap colorSpaceProf) {
    ASSERT_NE(nullptr, session);
    ASSERT_NE(nullptr, halStreams);
    ASSERT_NE(nullptr, previewStream);
    ASSERT_NE(nullptr, supportsPartialResults);
    ASSERT_NE(nullptr, partialResultCount);
    ASSERT_NE(nullptr, halBufManagedStreamIds);
    ASSERT_NE(nullptr, outCb);

    ALOGI("configureStreams: Testing camera device %s", name.c_str());

    std::vector<AvailableStream> outputStreams;
    std::shared_ptr<ICameraDevice> device;

    ndk::ScopedAStatus ret = provider->getCameraDeviceInterface(name, &device);
    ALOGI("getCameraDeviceInterface returns status:%d:%d", ret.getExceptionCode(),
          ret.getServiceSpecificError());
    ASSERT_TRUE(ret.isOk());
    ASSERT_NE(device, nullptr);

    CameraMetadata metadata;
    camera_metadata_t* staticMeta;
    ret = device->getCameraCharacteristics(&metadata);
    ASSERT_TRUE(ret.isOk());
    staticMeta = clone_camera_metadata(
            reinterpret_cast<const camera_metadata_t*>(metadata.metadata.data()));
    ASSERT_NE(staticMeta, nullptr);

    camera_metadata_ro_entry entry;
    auto status =
            find_camera_metadata_ro_entry(staticMeta, ANDROID_REQUEST_PARTIAL_RESULT_COUNT, &entry);
    if ((0 == status) && (entry.count > 0)) {
        *partialResultCount = entry.data.i32[0];
        *supportsPartialResults = (*partialResultCount > 1);
    }

    *outCb = ndk::SharedRefBase::make<DeviceCb>(this, staticMeta);
    ret = device->open(*outCb, session);
    ALOGI("device::open returns status:%d:%d", ret.getExceptionCode(),
          ret.getServiceSpecificError());
    ASSERT_TRUE(ret.isOk());
    ASSERT_NE(*session, nullptr);

    BufferManagerType bufferManagerType = BufferManagerType::FRAMEWORK;
    status = find_camera_metadata_ro_entry(
            staticMeta, ANDROID_INFO_SUPPORTED_BUFFER_MANAGEMENT_VERSION, &entry);
    if ((0 == status) && (entry.count == 1)) {
        if (entry.data.u8[0] == ANDROID_INFO_SUPPORTED_BUFFER_MANAGEMENT_VERSION_HIDL_DEVICE_3_5) {
            bufferManagerType = BufferManagerType::HAL;
        } else if (entry.data.u8[0] ==
                   ANDROID_INFO_SUPPORTED_BUFFER_MANAGEMENT_VERSION_SESSION_CONFIGURABLE) {
            bufferManagerType = BufferManagerType::SESSION;
        }
    }

    outputStreams.clear();
    Size maxSize;
    if (maxResolution) {
        auto rc = getMaxOutputSizeForFormat(staticMeta, format, &maxSize, maxResolution);
        ASSERT_EQ(Status::OK, rc);
    } else {
        AvailableStream previewThreshold = {kMaxPreviewWidth, kMaxPreviewHeight,
            static_cast<int32_t>(format)};
        auto rc = getAvailableOutputStreams(staticMeta, outputStreams, &previewThreshold);

        ASSERT_EQ(Status::OK, rc);
        ASSERT_FALSE(outputStreams.empty());
        maxSize.width = outputStreams[0].width;
        maxSize.height = outputStreams[0].height;
    }


    std::vector<Stream> streams(1);
    streams[0] = {0,
                  StreamType::OUTPUT,
                  maxSize.width,
                  maxSize.height,
                  format,
                  previewStream->usage,
                  previewStream->dataSpace,
                  StreamRotation::ROTATION_0,
                  "",
                  0,
                  -1,
                  {maxResolution ? SensorPixelMode::ANDROID_SENSOR_PIXEL_MODE_MAXIMUM_RESOLUTION
                                 : SensorPixelMode::ANDROID_SENSOR_PIXEL_MODE_DEFAULT},
                  dynamicRangeProf,
                  ScalerAvailableStreamUseCases::ANDROID_SCALER_AVAILABLE_STREAM_USE_CASES_DEFAULT,
                  static_cast<int>(colorSpaceProf)};

    StreamConfiguration config;
    config.streams = streams;
    config.operationMode = StreamConfigurationMode::NORMAL_MODE;
    config.streamConfigCounter = streamConfigCounter;
    config.multiResolutionInputImage = false;
    CameraMetadata req;
    RequestTemplate reqTemplate = RequestTemplate::STILL_CAPTURE;
    ret = (*session)->constructDefaultRequestSettings(reqTemplate, &req);
    ASSERT_TRUE(ret.isOk());
    config.sessionParams = req;

    bool supported = false;
    ret = device->isStreamCombinationSupported(config, &supported);
    ASSERT_TRUE(ret.isOk());
    ASSERT_EQ(supported, true);

    ret = configureStreams(*session, config, bufferManagerType, halBufManagedStreamIds, halStreams);

    ASSERT_TRUE(ret.isOk());

    if (halBufManagedStreamIds->size() != 0) {
        std::vector<Stream> ss(1);
        std::vector<HalStream> hs(1);
        ss[0] = streams[0];
        hs[0] = (*halStreams)[0];
        (*outCb)->setCurrentStreamConfig(ss, hs);
    }

    *previewStream = streams[0];
    ASSERT_TRUE(ret.isOk());
}

bool CameraAidlTest::is10BitDynamicRangeCapable(const camera_metadata_t* staticMeta) {
    camera_metadata_ro_entry scalerEntry;
    int rc = find_camera_metadata_ro_entry(staticMeta, ANDROID_REQUEST_AVAILABLE_CAPABILITIES,
                                           &scalerEntry);
    if (rc == 0) {
        for (uint32_t i = 0; i < scalerEntry.count; i++) {
            if (scalerEntry.data.u8[i] ==
                ANDROID_REQUEST_AVAILABLE_CAPABILITIES_DYNAMIC_RANGE_TEN_BIT) {
                return true;
            }
        }
    }
    return false;
}

void CameraAidlTest::get10BitDynamicRangeProfiles(
        const camera_metadata_t* staticMeta,
        std::vector<RequestAvailableDynamicRangeProfilesMap>* profiles) {
    ASSERT_NE(nullptr, staticMeta);
    ASSERT_NE(nullptr, profiles);
    camera_metadata_ro_entry entry;
    std::unordered_set<int64_t> entries;
    int rc = find_camera_metadata_ro_entry(
            staticMeta, ANDROID_REQUEST_AVAILABLE_DYNAMIC_RANGE_PROFILES_MAP, &entry);
    ASSERT_EQ(rc, 0);
    ASSERT_TRUE(entry.count > 0);
    ASSERT_EQ(entry.count % 3, 0);

    for (uint32_t i = 0; i < entry.count; i += 3) {
        ASSERT_NE(entry.data.i64[i], ANDROID_REQUEST_AVAILABLE_DYNAMIC_RANGE_PROFILES_MAP_STANDARD);
        ASSERT_EQ(entries.find(entry.data.i64[i]), entries.end());
        entries.insert(static_cast<int64_t>(entry.data.i64[i]));
        profiles->emplace_back(
                static_cast<RequestAvailableDynamicRangeProfilesMap>(entry.data.i64[i]));
    }

    if (!entries.empty()) {
        ASSERT_NE(entries.find(ANDROID_REQUEST_AVAILABLE_DYNAMIC_RANGE_PROFILES_MAP_HLG10),
                  entries.end());
    }
}

void CameraAidlTest::verify10BitMetadata(
        HandleImporter& importer, const InFlightRequest& request,
        aidl::android::hardware::camera::metadata::RequestAvailableDynamicRangeProfilesMap
                profile) {
    for (auto b : request.resultOutputBuffers) {
        importer.importBuffer(b.buffer.buffer);
        bool smpte2086Present = importer.isSmpte2086Present(b.buffer.buffer);
        bool smpte2094_10Present = importer.isSmpte2094_10Present(b.buffer.buffer);
        bool smpte2094_40Present = importer.isSmpte2094_40Present(b.buffer.buffer);

        switch (static_cast<int64_t>(profile)) {
            case ANDROID_REQUEST_AVAILABLE_DYNAMIC_RANGE_PROFILES_MAP_HLG10:
                ASSERT_FALSE(smpte2086Present);
                ASSERT_FALSE(smpte2094_10Present);
                ASSERT_FALSE(smpte2094_40Present);
                break;
            case ANDROID_REQUEST_AVAILABLE_DYNAMIC_RANGE_PROFILES_MAP_HDR10:
                ASSERT_TRUE(smpte2086Present);
                ASSERT_FALSE(smpte2094_10Present);
                ASSERT_FALSE(smpte2094_40Present);
                break;
            case ANDROID_REQUEST_AVAILABLE_DYNAMIC_RANGE_PROFILES_MAP_HDR10_PLUS:
                ASSERT_FALSE(smpte2094_10Present);
                ASSERT_TRUE(smpte2094_40Present);
                break;
            case ANDROID_REQUEST_AVAILABLE_DYNAMIC_RANGE_PROFILES_MAP_DOLBY_VISION_10B_HDR_REF:
            case ANDROID_REQUEST_AVAILABLE_DYNAMIC_RANGE_PROFILES_MAP_DOLBY_VISION_10B_HDR_REF_PO:
            case ANDROID_REQUEST_AVAILABLE_DYNAMIC_RANGE_PROFILES_MAP_DOLBY_VISION_10B_HDR_OEM:
            case ANDROID_REQUEST_AVAILABLE_DYNAMIC_RANGE_PROFILES_MAP_DOLBY_VISION_10B_HDR_OEM_PO:
            case ANDROID_REQUEST_AVAILABLE_DYNAMIC_RANGE_PROFILES_MAP_DOLBY_VISION_8B_HDR_REF:
            case ANDROID_REQUEST_AVAILABLE_DYNAMIC_RANGE_PROFILES_MAP_DOLBY_VISION_8B_HDR_REF_PO:
            case ANDROID_REQUEST_AVAILABLE_DYNAMIC_RANGE_PROFILES_MAP_DOLBY_VISION_8B_HDR_OEM:
            case ANDROID_REQUEST_AVAILABLE_DYNAMIC_RANGE_PROFILES_MAP_DOLBY_VISION_8B_HDR_OEM_PO:
                ASSERT_FALSE(smpte2086Present);
                ASSERT_TRUE(smpte2094_10Present);
                ASSERT_FALSE(smpte2094_40Present);
                break;
            default:
                ALOGE("%s: Unexpected 10-bit dynamic range profile: %" PRId64, __FUNCTION__,
                      profile);
                ADD_FAILURE();
        }
        importer.freeBuffer(b.buffer.buffer);
    }
}

bool CameraAidlTest::reportsColorSpaces(const camera_metadata_t* staticMeta) {
    camera_metadata_ro_entry capabilityEntry;
    int rc = find_camera_metadata_ro_entry(staticMeta, ANDROID_REQUEST_AVAILABLE_CAPABILITIES,
                                           &capabilityEntry);
    if (rc == 0) {
        for (uint32_t i = 0; i < capabilityEntry.count; i++) {
            if (capabilityEntry.data.u8[i] ==
                ANDROID_REQUEST_AVAILABLE_CAPABILITIES_COLOR_SPACE_PROFILES) {
                return true;
            }
        }
    }
    return false;
}

void CameraAidlTest::getColorSpaceProfiles(
        const camera_metadata_t* staticMeta,
        std::vector<RequestAvailableColorSpaceProfilesMap>* profiles) {
    ASSERT_NE(nullptr, staticMeta);
    ASSERT_NE(nullptr, profiles);
    camera_metadata_ro_entry entry;
    int rc = find_camera_metadata_ro_entry(
            staticMeta, ANDROID_REQUEST_AVAILABLE_COLOR_SPACE_PROFILES_MAP, &entry);
    ASSERT_EQ(rc, 0);
    ASSERT_TRUE(entry.count > 0);
    ASSERT_EQ(entry.count % 3, 0);

    for (uint32_t i = 0; i < entry.count; i += 3) {
        ASSERT_NE(entry.data.i64[i],
                ANDROID_REQUEST_AVAILABLE_COLOR_SPACE_PROFILES_MAP_UNSPECIFIED);
        if (std::find(profiles->begin(), profiles->end(),
                static_cast<RequestAvailableColorSpaceProfilesMap>(entry.data.i64[i]))
                == profiles->end()) {
            profiles->emplace_back(
                    static_cast<RequestAvailableColorSpaceProfilesMap>(entry.data.i64[i]));
        }
    }
}

bool CameraAidlTest::isColorSpaceCompatibleWithDynamicRangeAndPixelFormat(
        const camera_metadata_t* staticMeta,
        RequestAvailableColorSpaceProfilesMap colorSpace,
        RequestAvailableDynamicRangeProfilesMap dynamicRangeProfile,
        aidl::android::hardware::graphics::common::PixelFormat pixelFormat) {
    camera_metadata_ro_entry entry;
    int rc = find_camera_metadata_ro_entry(
            staticMeta, ANDROID_REQUEST_AVAILABLE_COLOR_SPACE_PROFILES_MAP, &entry);

    if (rc == 0) {
        for (uint32_t i = 0; i < entry.count; i += 3) {
            RequestAvailableColorSpaceProfilesMap entryColorSpace =
                    static_cast<RequestAvailableColorSpaceProfilesMap>(entry.data.i64[i]);
            int64_t dynamicRangeProfileI64 = static_cast<int64_t>(dynamicRangeProfile);
            int32_t entryImageFormat = static_cast<int32_t>(entry.data.i64[i + 1]);
            int32_t expectedImageFormat = halFormatToPublicFormat(pixelFormat);
            if (entryColorSpace == colorSpace
                    && (entry.data.i64[i + 2] & dynamicRangeProfileI64) != 0
                    && entryImageFormat == expectedImageFormat) {
                return true;
            }
        }
    }

    return false;
}

const char* CameraAidlTest::getColorSpaceProfileString(
        RequestAvailableColorSpaceProfilesMap colorSpace) {
    auto colorSpaceCast = static_cast<int>(colorSpace);
    switch (colorSpaceCast) {
        case ANDROID_REQUEST_AVAILABLE_COLOR_SPACE_PROFILES_MAP_UNSPECIFIED:
            return "UNSPECIFIED";
        case ColorSpaceNamed::SRGB:
            return "SRGB";
        case ColorSpaceNamed::LINEAR_SRGB:
            return "LINEAR_SRGB";
        case ColorSpaceNamed::EXTENDED_SRGB:
            return "EXTENDED_SRGB";
        case ColorSpaceNamed::LINEAR_EXTENDED_SRGB:
            return "LINEAR_EXTENDED_SRGB";
        case ColorSpaceNamed::BT709:
            return "BT709";
        case ColorSpaceNamed::BT2020:
            return "BT2020";
        case ColorSpaceNamed::DCI_P3:
            return "DCI_P3";
        case ColorSpaceNamed::DISPLAY_P3:
            return "DISPLAY_P3";
        case ColorSpaceNamed::NTSC_1953:
            return "NTSC_1953";
        case ColorSpaceNamed::SMPTE_C:
            return "SMPTE_C";
        case ColorSpaceNamed::ADOBE_RGB:
            return "ADOBE_RGB";
        case ColorSpaceNamed::PRO_PHOTO_RGB:
            return "PRO_PHOTO_RGB";
        case ColorSpaceNamed::ACES:
            return "ACES";
        case ColorSpaceNamed::ACESCG:
            return "ACESCG";
        case ColorSpaceNamed::CIE_XYZ:
            return "CIE_XYZ";
        case ColorSpaceNamed::CIE_LAB:
            return "CIE_LAB";
        case ColorSpaceNamed::BT2020_HLG:
            return "BT2020_HLG";
        case ColorSpaceNamed::BT2020_PQ:
            return "BT2020_PQ";
        default:
            return "INVALID";
    }

    return "INVALID";
}

const char* CameraAidlTest::getDynamicRangeProfileString(
        RequestAvailableDynamicRangeProfilesMap dynamicRangeProfile) {
    auto dynamicRangeProfileCast =
            static_cast<camera_metadata_enum_android_request_available_dynamic_range_profiles_map>
            (dynamicRangeProfile);
    switch (dynamicRangeProfileCast) {
        case ANDROID_REQUEST_AVAILABLE_DYNAMIC_RANGE_PROFILES_MAP_STANDARD:
            return "STANDARD";
        case ANDROID_REQUEST_AVAILABLE_DYNAMIC_RANGE_PROFILES_MAP_HLG10:
            return "HLG10";
        case ANDROID_REQUEST_AVAILABLE_DYNAMIC_RANGE_PROFILES_MAP_HDR10:
            return "HDR10";
        case ANDROID_REQUEST_AVAILABLE_DYNAMIC_RANGE_PROFILES_MAP_HDR10_PLUS:
            return "HDR10_PLUS";
        case ANDROID_REQUEST_AVAILABLE_DYNAMIC_RANGE_PROFILES_MAP_DOLBY_VISION_10B_HDR_REF:
            return "DOLBY_VISION_10B_HDR_REF";
        case ANDROID_REQUEST_AVAILABLE_DYNAMIC_RANGE_PROFILES_MAP_DOLBY_VISION_10B_HDR_REF_PO:
            return "DOLBY_VISION_10B_HDR_REF_P0";
        case ANDROID_REQUEST_AVAILABLE_DYNAMIC_RANGE_PROFILES_MAP_DOLBY_VISION_10B_HDR_OEM:
            return "DOLBY_VISION_10B_HDR_OEM";
        case ANDROID_REQUEST_AVAILABLE_DYNAMIC_RANGE_PROFILES_MAP_DOLBY_VISION_10B_HDR_OEM_PO:
            return "DOLBY_VISION_10B_HDR_OEM_P0";
        case ANDROID_REQUEST_AVAILABLE_DYNAMIC_RANGE_PROFILES_MAP_DOLBY_VISION_8B_HDR_REF:
            return "DOLBY_VISION_8B_HDR_REF";
        case ANDROID_REQUEST_AVAILABLE_DYNAMIC_RANGE_PROFILES_MAP_DOLBY_VISION_8B_HDR_REF_PO:
            return "DOLBY_VISION_8B_HDR_REF_P0";
        case ANDROID_REQUEST_AVAILABLE_DYNAMIC_RANGE_PROFILES_MAP_DOLBY_VISION_8B_HDR_OEM:
            return "DOLBY_VISION_8B_HDR_OEM";
        case ANDROID_REQUEST_AVAILABLE_DYNAMIC_RANGE_PROFILES_MAP_DOLBY_VISION_8B_HDR_OEM_PO:
            return "DOLBY_VISION_8B_HDR_OEM_P0";
        default:
            return "INVALID";
    }

    return "INVALID";
}

int32_t CameraAidlTest::halFormatToPublicFormat(
        aidl::android::hardware::graphics::common::PixelFormat pixelFormat) {
    // This is an incomplete mapping of pixel format to image format and assumes dataspaces
    // (see getDataspace)
    switch (pixelFormat) {
    case PixelFormat::BLOB:
        return 0x100; // ImageFormat.JPEG
    case PixelFormat::Y16:
        return 0x44363159; // ImageFormat.DEPTH16
    default:
        return static_cast<int32_t>(pixelFormat);
    }
}

bool CameraAidlTest::supportZoomSettingsOverride(const camera_metadata_t* staticMeta) {
    camera_metadata_ro_entry availableOverridesEntry;
    int rc = find_camera_metadata_ro_entry(staticMeta, ANDROID_CONTROL_AVAILABLE_SETTINGS_OVERRIDES,
                                           &availableOverridesEntry);
    if (rc == 0) {
        for (size_t i = 0; i < availableOverridesEntry.count; i++) {
            if (availableOverridesEntry.data.i32[i] == ANDROID_CONTROL_SETTINGS_OVERRIDE_ZOOM) {
                return true;
            }
        }
    }
    return false;
}

bool CameraAidlTest::supportsCroppedRawUseCase(const camera_metadata_t *staticMeta) {
    camera_metadata_ro_entry availableStreamUseCasesEntry;
    int rc = find_camera_metadata_ro_entry(staticMeta, ANDROID_SCALER_AVAILABLE_STREAM_USE_CASES,
                                           &availableStreamUseCasesEntry);
    if (rc == 0) {
        for (size_t i = 0; i < availableStreamUseCasesEntry.count; i++) {
            if (availableStreamUseCasesEntry.data.i64[i] ==
                    ANDROID_SCALER_AVAILABLE_STREAM_USE_CASES_CROPPED_RAW) {
                return true;
            }
        }
    }
    return false;
}

bool CameraAidlTest::supportsStreamUseCaseCap(const camera_metadata_t* staticMeta) {
    camera_metadata_ro_entry entry;
    int retcode = find_camera_metadata_ro_entry(staticMeta, ANDROID_REQUEST_AVAILABLE_CAPABILITIES,
                                                &entry);
    bool hasStreamUseCaseCap = false;
    if ((0 == retcode) && (entry.count > 0)) {
        if (std::find(entry.data.u8, entry.data.u8 + entry.count,
                      ANDROID_REQUEST_AVAILABLE_CAPABILITIES_STREAM_USE_CASE) !=
            entry.data.u8 + entry.count) {
            hasStreamUseCaseCap = true;
        }
    }
    return hasStreamUseCaseCap;
}

bool CameraAidlTest::isPerFrameControl(const camera_metadata_t* staticMeta) {
    camera_metadata_ro_entry syncLatencyEntry;
    int rc = find_camera_metadata_ro_entry(staticMeta, ANDROID_SYNC_MAX_LATENCY,
                                           &syncLatencyEntry);
    if (rc == 0 && syncLatencyEntry.data.i32[0] == ANDROID_SYNC_MAX_LATENCY_PER_FRAME_CONTROL) {
        return true;
    }
    return false;
}

void CameraAidlTest::configurePreviewStream(
        const std::string& name, const std::shared_ptr<ICameraProvider>& provider,
        const AvailableStream* previewThreshold, std::shared_ptr<ICameraDeviceSession>* session,
        Stream* previewStream, std::vector<HalStream>* halStreams, bool* supportsPartialResults,
        int32_t* partialResultCount, bool* useHalBufManager, std::shared_ptr<DeviceCb>* cb,
        uint32_t streamConfigCounter) {
    ASSERT_NO_FATAL_FAILURE(configureSingleStream(
            name, provider, previewThreshold, GRALLOC1_CONSUMER_USAGE_HWCOMPOSER,
            RequestTemplate::PREVIEW, session, previewStream, halStreams, supportsPartialResults,
            partialResultCount, useHalBufManager, cb, streamConfigCounter));
}

Status CameraAidlTest::isOfflineSessionSupported(const camera_metadata_t* staticMeta) {
    Status ret = Status::OPERATION_NOT_SUPPORTED;
    if (nullptr == staticMeta) {
        return Status::ILLEGAL_ARGUMENT;
    }

    camera_metadata_ro_entry entry;
    int rc = find_camera_metadata_ro_entry(staticMeta, ANDROID_REQUEST_AVAILABLE_CAPABILITIES,
                                           &entry);
    if (0 != rc) {
        return Status::ILLEGAL_ARGUMENT;
    }

    for (size_t i = 0; i < entry.count; i++) {
        if (ANDROID_REQUEST_AVAILABLE_CAPABILITIES_OFFLINE_PROCESSING == entry.data.u8[i]) {
            ret = Status::OK;
            break;
        }
    }

    return ret;
}

void CameraAidlTest::configureOfflineStillStream(
        const std::string& name, const std::shared_ptr<ICameraProvider>& provider,
        const AvailableStream* threshold, std::shared_ptr<ICameraDeviceSession>* session,
        Stream* stream, std::vector<HalStream>* halStreams, bool* supportsPartialResults,
        int32_t* partialResultCount, std::shared_ptr<DeviceCb>* outCb, int32_t* jpegBufferSize,
        std::set<int32_t>* halBufManagedStreamIds) {
    ASSERT_NE(nullptr, session);
    ASSERT_NE(nullptr, halStreams);
    ASSERT_NE(nullptr, stream);
    ASSERT_NE(nullptr, supportsPartialResults);
    ASSERT_NE(nullptr, partialResultCount);
    ASSERT_NE(nullptr, outCb);
    ASSERT_NE(nullptr, jpegBufferSize);
    ASSERT_NE(nullptr, halBufManagedStreamIds);

    std::vector<AvailableStream> outputStreams;
    std::shared_ptr<ICameraDevice> cameraDevice;
    ALOGI("configureStreams: Testing camera device %s", name.c_str());

    ndk::ScopedAStatus ret = provider->getCameraDeviceInterface(name, &cameraDevice);
    ASSERT_TRUE(ret.isOk());
    ALOGI("getCameraDeviceInterface returns status:%d:%d", ret.getExceptionCode(),
          ret.getServiceSpecificError());
    ASSERT_NE(cameraDevice, nullptr);

    CameraMetadata metadata;
    ret = cameraDevice->getCameraCharacteristics(&metadata);
    ASSERT_TRUE(ret.isOk());
    camera_metadata_t* staticMeta = clone_camera_metadata(
            reinterpret_cast<const camera_metadata_t*>(metadata.metadata.data()));
    ASSERT_NE(nullptr, staticMeta);

    camera_metadata_ro_entry entry;
    auto status =
            find_camera_metadata_ro_entry(staticMeta, ANDROID_REQUEST_PARTIAL_RESULT_COUNT, &entry);
    if ((0 == status) && (entry.count > 0)) {
        *partialResultCount = entry.data.i32[0];
        *supportsPartialResults = (*partialResultCount > 1);
    }

    BufferManagerType bufferManagerType = BufferManagerType::FRAMEWORK;
    status = find_camera_metadata_ro_entry(
            staticMeta, ANDROID_INFO_SUPPORTED_BUFFER_MANAGEMENT_VERSION, &entry);
    if ((0 == status) && (entry.count == 1)) {
        if (entry.data.u8[0] == ANDROID_INFO_SUPPORTED_BUFFER_MANAGEMENT_VERSION_HIDL_DEVICE_3_5) {
            bufferManagerType = BufferManagerType::HAL;
        } else if (entry.data.u8[0] ==
                   ANDROID_INFO_SUPPORTED_BUFFER_MANAGEMENT_VERSION_SESSION_CONFIGURABLE) {
            bufferManagerType = BufferManagerType::SESSION;
        }
    }

    auto st = getJpegBufferSize(staticMeta, jpegBufferSize);
    ASSERT_EQ(st, Status::OK);

    *outCb = ndk::SharedRefBase::make<DeviceCb>(this, staticMeta);
    ret = cameraDevice->open(*outCb, session);
    ASSERT_TRUE(ret.isOk());
    ALOGI("device::open returns status:%d:%d", ret.getExceptionCode(),
          ret.getServiceSpecificError());
    ASSERT_NE(session, nullptr);

    outputStreams.clear();
    auto rc = getAvailableOutputStreams(staticMeta, outputStreams, threshold);
    size_t idx = 0;
    int currLargest = outputStreams[0].width * outputStreams[0].height;
    for (size_t i = 0; i < outputStreams.size(); i++) {
        int area = outputStreams[i].width * outputStreams[i].height;
        if (area > currLargest) {
            idx = i;
            currLargest = area;
        }
    }

    ASSERT_EQ(Status::OK, rc);
    ASSERT_FALSE(outputStreams.empty());

    Dataspace dataspace = getDataspace(static_cast<PixelFormat>(outputStreams[idx].format));

    std::vector<Stream> streams(/*size*/ 1);
    streams[0] = {/*id*/ 0,
                  StreamType::OUTPUT,
                  outputStreams[idx].width,
                  outputStreams[idx].height,
                  static_cast<PixelFormat>(outputStreams[idx].format),
                  static_cast<::aidl::android::hardware::graphics::common::BufferUsage>(
                          GRALLOC1_CONSUMER_USAGE_CPU_READ),
                  dataspace,
                  StreamRotation::ROTATION_0,
                  /*physicalId*/ std::string(),
                  *jpegBufferSize,
                  /*groupId*/ 0,
                  {SensorPixelMode::ANDROID_SENSOR_PIXEL_MODE_DEFAULT},
                  RequestAvailableDynamicRangeProfilesMap::
                          ANDROID_REQUEST_AVAILABLE_DYNAMIC_RANGE_PROFILES_MAP_STANDARD,
                  ScalerAvailableStreamUseCases::ANDROID_SCALER_AVAILABLE_STREAM_USE_CASES_DEFAULT,
                  static_cast<int>(
                          RequestAvailableColorSpaceProfilesMap::
                                  ANDROID_REQUEST_AVAILABLE_COLOR_SPACE_PROFILES_MAP_UNSPECIFIED)};

    StreamConfiguration config = {streams, StreamConfigurationMode::NORMAL_MODE, CameraMetadata()};

    ret = configureStreams(*session, config, bufferManagerType, halBufManagedStreamIds, halStreams);

    ASSERT_TRUE(ret.isOk());

    if (halBufManagedStreamIds->size() != 0) {
        (*outCb)->setCurrentStreamConfig(streams, *halStreams);
    }

    *stream = streams[0];
}

void CameraAidlTest::updateInflightResultQueue(
        const std::shared_ptr<ResultMetadataQueue>& resultQueue) {
    std::unique_lock<std::mutex> l(mLock);
    for (auto& it : mInflightMap) {
        it.second->resultQueue = resultQueue;
    }
}

void CameraAidlTest::processColorSpaceRequest(
        RequestAvailableColorSpaceProfilesMap colorSpace,
        RequestAvailableDynamicRangeProfilesMap dynamicRangeProfile) {
    std::vector<std::string> cameraDeviceNames = getCameraDeviceNames(mProvider);
    CameraMetadata settings;

    for (const auto& name : cameraDeviceNames) {
        std::string version, deviceId;
        ASSERT_TRUE(matchDeviceName(name, mProviderType, &version, &deviceId));
        CameraMetadata meta;
        std::shared_ptr<ICameraDevice> device;
        ASSERT_NO_FATAL_FAILURE(openEmptyDeviceSession(name, mProvider, &mSession, &meta, &device));
        camera_metadata_t* staticMeta = reinterpret_cast<camera_metadata_t*>(meta.metadata.data());

        // Device does not report color spaces, skip.
        if (!reportsColorSpaces(staticMeta)) {
            ndk::ScopedAStatus ret = mSession->close();
            mSession = nullptr;
            ASSERT_TRUE(ret.isOk());
            ALOGV("Camera %s does not report color spaces", name.c_str());
            continue;
        }
        std::vector<RequestAvailableColorSpaceProfilesMap> profileList;
        getColorSpaceProfiles(staticMeta, &profileList);
        ASSERT_FALSE(profileList.empty());

        // Device does not support color space / dynamic range profile, skip
        if (std::find(profileList.begin(), profileList.end(), colorSpace)
                == profileList.end() || !isColorSpaceCompatibleWithDynamicRangeAndPixelFormat(
                        staticMeta, colorSpace, dynamicRangeProfile,
                        PixelFormat::IMPLEMENTATION_DEFINED)) {
            ndk::ScopedAStatus ret = mSession->close();
            mSession = nullptr;
            ASSERT_TRUE(ret.isOk());
            ALOGV("Camera %s does not support color space %s with dynamic range profile %s and "
                  "pixel format %d", name.c_str(), getColorSpaceProfileString(colorSpace),
                  getDynamicRangeProfileString(dynamicRangeProfile),
                  PixelFormat::IMPLEMENTATION_DEFINED);
            continue;
        }

        ALOGV("Camera %s supports color space %s with dynamic range profile %s and pixel format %d",
                name.c_str(), getColorSpaceProfileString(colorSpace),
                getDynamicRangeProfileString(dynamicRangeProfile),
                PixelFormat::IMPLEMENTATION_DEFINED);

        // If an HDR dynamic range profile is reported in the color space profile list,
        // the device must also have the dynamic range profiles map capability and contain
        // the dynamic range profile in the map.
        if (dynamicRangeProfile != static_cast<RequestAvailableDynamicRangeProfilesMap>(
                ANDROID_REQUEST_AVAILABLE_DYNAMIC_RANGE_PROFILES_MAP_STANDARD)) {
            ASSERT_TRUE(is10BitDynamicRangeCapable(staticMeta));

            std::vector<RequestAvailableDynamicRangeProfilesMap> dynamicRangeProfiles;
            get10BitDynamicRangeProfiles(staticMeta, &dynamicRangeProfiles);
            ASSERT_FALSE(dynamicRangeProfiles.empty());
            ASSERT_FALSE(std::find(dynamicRangeProfiles.begin(), dynamicRangeProfiles.end(),
                    dynamicRangeProfile) == dynamicRangeProfiles.end());
        }

        CameraMetadata req;
        android::hardware::camera::common::V1_0::helper::CameraMetadata defaultSettings;
        ndk::ScopedAStatus ret =
                mSession->constructDefaultRequestSettings(RequestTemplate::PREVIEW, &req);
        ASSERT_TRUE(ret.isOk());

        const camera_metadata_t* metadata =
                reinterpret_cast<const camera_metadata_t*>(req.metadata.data());
        size_t expectedSize = req.metadata.size();
        int result = validate_camera_metadata_structure(metadata, &expectedSize);
        ASSERT_TRUE((result == 0) || (result == CAMERA_METADATA_VALIDATION_SHIFTED));

        size_t entryCount = get_camera_metadata_entry_count(metadata);
        ASSERT_GT(entryCount, 0u);
        defaultSettings = metadata;

        const camera_metadata_t* settingsBuffer = defaultSettings.getAndLock();
        uint8_t* rawSettingsBuffer = (uint8_t*)settingsBuffer;
        settings.metadata = std::vector(
                rawSettingsBuffer, rawSettingsBuffer + get_camera_metadata_size(settingsBuffer));
        overrideRotateAndCrop(&settings);

        ret = mSession->close();
        mSession = nullptr;
        ASSERT_TRUE(ret.isOk());

        std::vector<HalStream> halStreams;
        bool supportsPartialResults = false;
        std::set<int32_t> halBufManagedStreamIds;
        int32_t partialResultCount = 0;
        Stream previewStream;
        std::shared_ptr<DeviceCb> cb;

        previewStream.usage = static_cast<aidl::android::hardware::graphics::common::BufferUsage>(
                GRALLOC1_CONSUMER_USAGE_HWCOMPOSER);
        configureStreams(name, mProvider, PixelFormat::IMPLEMENTATION_DEFINED, &mSession,
                         &previewStream, &halStreams, &supportsPartialResults, &partialResultCount,
                         &halBufManagedStreamIds, &cb, 0,
                         /*maxResolution*/ false, dynamicRangeProfile, colorSpace);
        ASSERT_NE(mSession, nullptr);

        ::aidl::android::hardware::common::fmq::MQDescriptor<
                int8_t, aidl::android::hardware::common::fmq::SynchronizedReadWrite>
                descriptor;
        auto resultQueueRet = mSession->getCaptureResultMetadataQueue(&descriptor);
        ASSERT_TRUE(resultQueueRet.isOk());

        std::shared_ptr<ResultMetadataQueue> resultQueue =
                std::make_shared<ResultMetadataQueue>(descriptor);
        if (!resultQueue->isValid() || resultQueue->availableToWrite() <= 0) {
            ALOGE("%s: HAL returns empty result metadata fmq, not use it", __func__);
            resultQueue = nullptr;
            // Don't use the queue onwards.
        }

        mInflightMap.clear();
        // Stream as long as needed to fill the Hal inflight queue
        std::vector<CaptureRequest> requests(halStreams[0].maxBuffers);

        for (int32_t requestId = 0; requestId < requests.size(); requestId++) {
            std::shared_ptr<InFlightRequest> inflightReq = std::make_shared<InFlightRequest>(
                    static_cast<ssize_t>(halStreams.size()), false, supportsPartialResults,
                    partialResultCount, std::unordered_set<std::string>(), resultQueue);

            CaptureRequest& request = requests[requestId];
            std::vector<StreamBuffer>& outputBuffers = request.outputBuffers;
            outputBuffers.resize(halStreams.size());

            size_t k = 0;
            inflightReq->mOutstandingBufferIds.resize(halStreams.size());
            std::vector<buffer_handle_t> graphicBuffers;
            graphicBuffers.reserve(halStreams.size());

            auto bufferId = requestId + 1;  // Buffer id value 0 is not valid
            for (const auto& halStream : halStreams) {
                buffer_handle_t buffer_handle;
                if (contains(halBufManagedStreamIds, halStream.id)) {
                    outputBuffers[k] = {halStream.id,   0,
                                        NativeHandle(), BufferStatus::OK,
                                        NativeHandle(), NativeHandle()};
                } else {
                    auto usage = ANDROID_NATIVE_UNSIGNED_CAST(android_convertGralloc1To0Usage(
                            static_cast<uint64_t>(halStream.producerUsage),
                            static_cast<uint64_t>(halStream.consumerUsage)));
                    allocateGraphicBuffer(previewStream.width, previewStream.height, usage,
                                            halStream.overrideFormat, &buffer_handle);

                    inflightReq->mOutstandingBufferIds[halStream.id][bufferId] = buffer_handle;
                    graphicBuffers.push_back(buffer_handle);
                    outputBuffers[k] = {
                            halStream.id,     bufferId,       android::makeToAidl(buffer_handle),
                            BufferStatus::OK, NativeHandle(), NativeHandle()};
                }
                k++;
            }

            request.inputBuffer = {
                    -1, 0, NativeHandle(), BufferStatus::ERROR, NativeHandle(), NativeHandle()};
            request.frameNumber = bufferId;
            request.fmqSettingsSize = 0;
            request.settings = settings;
            request.inputWidth = 0;
            request.inputHeight = 0;

            {
                std::unique_lock<std::mutex> l(mLock);
                mInflightMap[bufferId] = inflightReq;
            }
        }

        int32_t numRequestProcessed = 0;
        std::vector<BufferCache> cachesToRemove;
        ndk::ScopedAStatus returnStatus =
            mSession->processCaptureRequest(requests, cachesToRemove, &numRequestProcessed);
        ASSERT_TRUE(returnStatus.isOk());
        ASSERT_EQ(numRequestProcessed, requests.size());

        returnStatus = mSession->repeatingRequestEnd(requests.size() - 1,
                std::vector<int32_t> {halStreams[0].id});
        ASSERT_TRUE(returnStatus.isOk());

        // We are keeping frame numbers and buffer ids consistent. Buffer id value of 0
        // is used to indicate a buffer that is not present/available so buffer ids as well
        // as frame numbers begin with 1.
        for (int32_t frameNumber = 1; frameNumber <= requests.size(); frameNumber++) {
            const auto& inflightReq = mInflightMap[frameNumber];
            std::unique_lock<std::mutex> l(mLock);
            while (!inflightReq->errorCodeValid &&
                    ((0 < inflightReq->numBuffersLeft) || (!inflightReq->haveResultMetadata))) {
                auto timeout = std::chrono::system_clock::now() +
                                std::chrono::seconds(kStreamBufferTimeoutSec);
                ASSERT_NE(std::cv_status::timeout, mResultCondition.wait_until(l, timeout));
            }

            ASSERT_FALSE(inflightReq->errorCodeValid);
            ASSERT_NE(inflightReq->resultOutputBuffers.size(), 0u);

            if (dynamicRangeProfile != static_cast<RequestAvailableDynamicRangeProfilesMap>(
                    ANDROID_REQUEST_AVAILABLE_DYNAMIC_RANGE_PROFILES_MAP_STANDARD)) {
                verify10BitMetadata(mHandleImporter, *inflightReq, dynamicRangeProfile);
            }
        }

        if (halBufManagedStreamIds.size() != 0) {
            std::vector<int32_t> streamIds;
            for (size_t i = 0; i < streamIds.size(); i++) {
                if (contains(halBufManagedStreamIds, halStreams[i].id)) {
                    streamIds.emplace_back(halStreams[i].id);
                }
            }
            mSession->signalStreamFlush(streamIds, /*streamConfigCounter*/ 0);
            cb->waitForBuffersReturned();
        }

        ret = mSession->close();
        mSession = nullptr;
        ASSERT_TRUE(ret.isOk());
    }
}

void CameraAidlTest::processZoomSettingsOverrideRequests(
        int32_t frameCount, const bool *overrideSequence, const bool *expectedResults) {
    std::vector<std::string> cameraDeviceNames = getCameraDeviceNames(mProvider);
    AvailableStream previewThreshold = {kMaxPreviewWidth, kMaxPreviewHeight,
                                        static_cast<int32_t>(PixelFormat::IMPLEMENTATION_DEFINED)};
    int64_t bufferId = 1;
    int32_t frameNumber = 1;
    CameraMetadata settings;
    ndk::ScopedAStatus ret;
    for (const auto& name : cameraDeviceNames) {
        CameraMetadata meta;
        std::shared_ptr<ICameraDevice> device;
        ASSERT_NO_FATAL_FAILURE(openEmptyDeviceSession(name, mProvider, &mSession /*out*/,
                                                       &meta /*out*/, &device /*out*/));
        camera_metadata_t* staticMeta =
                clone_camera_metadata(reinterpret_cast<camera_metadata_t*>(meta.metadata.data()));

        ret = mSession->close();
        mSession = nullptr;
        ASSERT_TRUE(ret.isOk());

        // Device does not support zoom settnigs override
        if (!supportZoomSettingsOverride(staticMeta)) {
            continue;
        }

        if (!isPerFrameControl(staticMeta)) {
            continue;
        }

        bool supportsPartialResults = false;
        bool useHalBufManager = false;
        int32_t partialResultCount = 0;
        Stream previewStream;
        std::vector<HalStream> halStreams;
        std::shared_ptr<DeviceCb> cb;
        configurePreviewStream(name, mProvider, &previewThreshold, &mSession /*out*/,
                               &previewStream /*out*/, &halStreams /*out*/,
                               &supportsPartialResults /*out*/, &partialResultCount /*out*/,
                               &useHalBufManager /*out*/, &cb /*out*/);
        ASSERT_NE(mSession, nullptr);

        ::aidl::android::hardware::common::fmq::MQDescriptor<
                int8_t, aidl::android::hardware::common::fmq::SynchronizedReadWrite>
                descriptor;
        auto resultQueueRet = mSession->getCaptureResultMetadataQueue(&descriptor);
        ASSERT_TRUE(resultQueueRet.isOk());

        std::shared_ptr<ResultMetadataQueue> resultQueue =
                std::make_shared<ResultMetadataQueue>(descriptor);
        if (!resultQueue->isValid() || resultQueue->availableToWrite() <= 0) {
            ALOGE("%s: HAL returns empty result metadata fmq, not use it", __func__);
            resultQueue = nullptr;
            // Don't use the queue onwards.
        }

        ret = mSession->constructDefaultRequestSettings(RequestTemplate::PREVIEW, &settings);
        ASSERT_TRUE(ret.isOk());

        mInflightMap.clear();
        ::android::hardware::camera::common::V1_0::helper::CameraMetadata requestMeta;
        std::vector<CaptureRequest> requests(frameCount);
        std::vector<buffer_handle_t> buffers(frameCount);
        std::vector<std::shared_ptr<InFlightRequest>> inflightReqs(frameCount);
        std::vector<CameraMetadata> requestSettings(frameCount);

        for (int32_t i = 0; i < frameCount; i++) {
            std::unique_lock<std::mutex> l(mLock);
            CaptureRequest& request = requests[i];
            std::vector<StreamBuffer>& outputBuffers = request.outputBuffers;
            outputBuffers.resize(1);
            StreamBuffer& outputBuffer = outputBuffers[0];

            if (useHalBufManager) {
                outputBuffer = {halStreams[0].id, 0,
                                NativeHandle(),   BufferStatus::OK,
                                NativeHandle(),   NativeHandle()};
            } else {
                allocateGraphicBuffer(previewStream.width, previewStream.height,
                                      ANDROID_NATIVE_UNSIGNED_CAST(android_convertGralloc1To0Usage(
                                              static_cast<uint64_t>(halStreams[0].producerUsage),
                                              static_cast<uint64_t>(halStreams[0].consumerUsage))),
                                      halStreams[0].overrideFormat, &buffers[i]);
                outputBuffer = {halStreams[0].id, bufferId + i,   ::android::makeToAidl(buffers[i]),
                                BufferStatus::OK, NativeHandle(), NativeHandle()};
            }

            // Set appropriate settings override tag
            requestMeta.append(reinterpret_cast<camera_metadata_t*>(settings.metadata.data()));
            int32_t settingsOverride = overrideSequence[i] ?
                    ANDROID_CONTROL_SETTINGS_OVERRIDE_ZOOM : ANDROID_CONTROL_SETTINGS_OVERRIDE_OFF;
            ASSERT_EQ(::android::OK, requestMeta.update(ANDROID_CONTROL_SETTINGS_OVERRIDE,
                    &settingsOverride, 1));
            camera_metadata_t* metaBuffer = requestMeta.release();
            uint8_t* rawMetaBuffer = reinterpret_cast<uint8_t*>(metaBuffer);
            requestSettings[i].metadata = std::vector(
                    rawMetaBuffer, rawMetaBuffer + get_camera_metadata_size(metaBuffer));
            overrideRotateAndCrop(&(requestSettings[i]));
            request.frameNumber = frameNumber + i;
            request.fmqSettingsSize = 0;
            request.settings = requestSettings[i];
            request.inputBuffer = {
                    -1, 0, NativeHandle(), BufferStatus::ERROR, NativeHandle(), NativeHandle()};

            inflightReqs[i] = std::make_shared<InFlightRequest>(1, false, supportsPartialResults,
                                                                partialResultCount, resultQueue);
            mInflightMap[frameNumber + i] = inflightReqs[i];
        }

        int32_t numRequestProcessed = 0;
        std::vector<BufferCache> cachesToRemove;

        ndk::ScopedAStatus returnStatus =
                mSession->processCaptureRequest(requests, cachesToRemove, &numRequestProcessed);
        ASSERT_TRUE(returnStatus.isOk());
        ASSERT_EQ(numRequestProcessed, frameCount);

        for (size_t i = 0; i < frameCount; i++) {
            std::unique_lock<std::mutex> l(mLock);
            while (!inflightReqs[i]->errorCodeValid && ((0 < inflightReqs[i]->numBuffersLeft) ||
                                                        (!inflightReqs[i]->haveResultMetadata))) {
                auto timeout = std::chrono::system_clock::now() +
                               std::chrono::seconds(kStreamBufferTimeoutSec);
                ASSERT_NE(std::cv_status::timeout, mResultCondition.wait_until(l, timeout));
            }

            ASSERT_FALSE(inflightReqs[i]->errorCodeValid);
            ASSERT_NE(inflightReqs[i]->resultOutputBuffers.size(), 0u);
            ASSERT_EQ(previewStream.id, inflightReqs[i]->resultOutputBuffers[0].buffer.streamId);
            ASSERT_FALSE(inflightReqs[i]->collectedResult.isEmpty());
            ASSERT_TRUE(inflightReqs[i]->collectedResult.exists(ANDROID_CONTROL_SETTINGS_OVERRIDE));
            camera_metadata_entry_t overrideResult =
                    inflightReqs[i]->collectedResult.find(ANDROID_CONTROL_SETTINGS_OVERRIDE);
            ASSERT_EQ(overrideResult.data.i32[0] == ANDROID_CONTROL_SETTINGS_OVERRIDE_ZOOM,
                    expectedResults[i]);
            ASSERT_TRUE(inflightReqs[i]->collectedResult.exists(
                    ANDROID_CONTROL_SETTINGS_OVERRIDING_FRAME_NUMBER));
            camera_metadata_entry_t frameNumberEntry = inflightReqs[i]->collectedResult.find(
                    ANDROID_CONTROL_SETTINGS_OVERRIDING_FRAME_NUMBER);
            ALOGV("%s: i %zu, expcetedResults[i] %d, overrideResult is %d, frameNumber %d",
                  __FUNCTION__, i, expectedResults[i], overrideResult.data.i32[0],
                  frameNumberEntry.data.i32[0]);
            if (expectedResults[i]) {
                ASSERT_GT(frameNumberEntry.data.i32[0], inflightReqs[i]->frameNumber);
            } else {
                ASSERT_EQ(frameNumberEntry.data.i32[0], frameNumber + i);
            }
        }

        ret = mSession->close();
        mSession = nullptr;
        ASSERT_TRUE(ret.isOk());
    }
}

void CameraAidlTest::getSupportedSizes(const camera_metadata_t* ch, uint32_t tag, int32_t format,
                                       std::vector<std::tuple<size_t, size_t>>* sizes /*out*/) {
    if (sizes == nullptr) {
        return;
    }

    camera_metadata_ro_entry entry;
    int retcode = find_camera_metadata_ro_entry(ch, tag, &entry);
    if ((0 == retcode) && (entry.count > 0)) {
        // Scaler entry contains 4 elements (format, width, height, type)
        for (size_t i = 0; i < entry.count; i += 4) {
            if ((entry.data.i32[i] == format) &&
                (entry.data.i32[i + 3] == ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT)) {
                sizes->push_back(std::make_tuple(entry.data.i32[i + 1], entry.data.i32[i + 2]));
            }
        }
    }
}

void CameraAidlTest::getSupportedDurations(const camera_metadata_t* ch, uint32_t tag,
                                           int32_t format,
                                           const std::vector<std::tuple<size_t, size_t>>& sizes,
                                           std::vector<int64_t>* durations /*out*/) {
    if (durations == nullptr) {
        return;
    }

    camera_metadata_ro_entry entry;
    int retcode = find_camera_metadata_ro_entry(ch, tag, &entry);
    if ((0 == retcode) && (entry.count > 0)) {
        // Duration entry contains 4 elements (format, width, height, duration)
        for (const auto& size : sizes) {
            int64_t width = std::get<0>(size);
            int64_t height = std::get<1>(size);
            for (size_t i = 0; i < entry.count; i += 4) {
                if ((entry.data.i64[i] == format) && (entry.data.i64[i + 1] == width) &&
                    (entry.data.i64[i + 2] == height)) {
                    durations->push_back(entry.data.i64[i + 3]);
                    break;
                }
            }
        }
    }
}

void CameraAidlTest::validateDefaultRequestMetadata(RequestTemplate reqTemplate,
                                                    const CameraMetadata& rawMetadata) {
    const camera_metadata_t* metadata = (camera_metadata_t*)rawMetadata.metadata.data();
    size_t expectedSize = rawMetadata.metadata.size();
    int result = validate_camera_metadata_structure(metadata, &expectedSize);
    ASSERT_TRUE((result == 0) || (result == CAMERA_METADATA_VALIDATION_SHIFTED));
    verifyRequestTemplate(metadata, reqTemplate);
}
