/**
 * Copyright (c) 2022, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <aidl/android/hardware/graphics/common/BlendMode.h>
#include <aidl/android/hardware/graphics/common/BufferUsage.h>
#include <aidl/android/hardware/graphics/common/FRect.h>
#include <aidl/android/hardware/graphics/common/Rect.h>
#include <aidl/android/hardware/graphics/composer3/Composition.h>
#include <aidl/android/hardware/graphics/composer3/IComposer.h>
#include <android-base/properties.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <android/hardware/graphics/composer3/ComposerClientReader.h>
#include <android/hardware/graphics/composer3/ComposerClientWriter.h>
#include <binder/ProcessState.h>
#include <gtest/gtest.h>
#include <ui/Fence.h>
#include <ui/GraphicBuffer.h>
#include <ui/PixelFormat.h>
#include <algorithm>
#include <numeric>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include "GraphicsComposerCallback.h"

using aidl::android::hardware::graphics::common::Dataspace;
using aidl::android::hardware::graphics::common::DisplayDecorationSupport;
using aidl::android::hardware::graphics::common::FRect;
using aidl::android::hardware::graphics::common::PixelFormat;
using aidl::android::hardware::graphics::common::Rect;
using namespace ::ndk;

namespace aidl::android::hardware::graphics::composer3::libhwc_aidl_test {

class DisplayWrapper;
/**
 * A wrapper to IComposerClient.
 * This wrapper manages the IComposerClient instance and manages the resources for
 * the tests with respect to the IComposerClient calls.
 */
class ComposerClientWrapper {
  public:
    ComposerClientWrapper(const std::string& name);

    ScopedAStatus createClient();

    bool tearDown(std::unordered_map<int64_t, ComposerClientWriter*> displayWriters);

    std::pair<ScopedAStatus, int32_t> getInterfaceVersion() const;

    std::pair<ScopedAStatus, VirtualDisplay> createVirtualDisplay(int32_t width, int32_t height,
                                                                  PixelFormat pixelFormat,
                                                                  int32_t bufferSlotCount);

    ScopedAStatus destroyVirtualDisplay(int64_t display);

    std::pair<ScopedAStatus, int64_t> createLayer(int64_t display, int32_t bufferSlotCount,
                                                  ComposerClientWriter*);

    ScopedAStatus destroyLayer(int64_t display, int64_t layer, ComposerClientWriter*);

    std::pair<ScopedAStatus, int32_t> getActiveConfig(int64_t display);

    ScopedAStatus setActiveConfig(DisplayWrapper* display, int32_t config);

    ScopedAStatus setPeakRefreshRateConfig(DisplayWrapper* display);

    std::pair<ScopedAStatus, int32_t> getDisplayAttribute(int64_t display, int32_t config,
                                                          DisplayAttribute displayAttribute);

    ScopedAStatus setPowerMode(int64_t display, PowerMode powerMode);

    ScopedAStatus setVsync(int64_t display, bool enable);

    void setVsyncAllowed(bool isAllowed);

    std::pair<ScopedAStatus, std::vector<float>> getDataspaceSaturationMatrix(Dataspace dataspace);

    std::pair<ScopedAStatus, std::vector<CommandResultPayload>> executeCommands(
            const std::vector<DisplayCommand>& commands);

    std::optional<VsyncPeriodChangeTimeline> takeLastVsyncPeriodChangeTimeline();

    ScopedAStatus setContentType(int64_t display, ContentType contentType);

    std::pair<ScopedAStatus, VsyncPeriodChangeTimeline> setActiveConfigWithConstraints(
            DisplayWrapper* display, int32_t config,
            const VsyncPeriodChangeConstraints& constraints);

    std::pair<ScopedAStatus, std::vector<DisplayCapability>> getDisplayCapabilities(
            int64_t display);

    ScopedAStatus dumpDebugInfo();

    std::pair<ScopedAStatus, DisplayIdentification> getDisplayIdentificationData(int64_t display);

    std::pair<ScopedAStatus, HdrCapabilities> getHdrCapabilities(int64_t display);

    std::pair<ScopedAStatus, std::vector<PerFrameMetadataKey>> getPerFrameMetadataKeys(
            int64_t display);

    std::pair<ScopedAStatus, ReadbackBufferAttributes> getReadbackBufferAttributes(int64_t display);

    ScopedAStatus setReadbackBuffer(int64_t display, const native_handle_t* buffer,
                                    const ScopedFileDescriptor& releaseFence);

    std::pair<ScopedAStatus, ScopedFileDescriptor> getReadbackBufferFence(int64_t display);

    std::pair<ScopedAStatus, std::vector<ColorMode>> getColorModes(int64_t display);

    std::pair<ScopedAStatus, std::vector<RenderIntent>> getRenderIntents(int64_t display,
                                                                         ColorMode colorMode);

    ScopedAStatus setColorMode(int64_t display, ColorMode colorMode, RenderIntent renderIntent);

    std::pair<ScopedAStatus, DisplayContentSamplingAttributes>
    getDisplayedContentSamplingAttributes(int64_t display);

    ScopedAStatus setDisplayedContentSamplingEnabled(int64_t display, bool isEnabled,
                                                     FormatColorComponent formatColorComponent,
                                                     int64_t maxFrames);

    std::pair<ScopedAStatus, DisplayContentSample> getDisplayedContentSample(int64_t display,
                                                                             int64_t maxFrames,
                                                                             int64_t timestamp);

    std::pair<ScopedAStatus, DisplayConnectionType> getDisplayConnectionType(int64_t display);

    std::pair<ScopedAStatus, std::vector<int32_t>> getDisplayConfigs(int64_t display);

    std::pair<ScopedAStatus, std::vector<DisplayConfiguration>> getDisplayConfigurations(
            int64_t display);

    ScopedAStatus notifyExpectedPresent(int64_t display,
                                        ClockMonotonicTimestamp expectedPresentTime,
                                        int frameIntervalNs);

    std::pair<ScopedAStatus, int32_t> getDisplayVsyncPeriod(int64_t display);

    ScopedAStatus setAutoLowLatencyMode(int64_t display, bool isEnabled);

    std::pair<ScopedAStatus, std::vector<ContentType>> getSupportedContentTypes(int64_t display);

    std::pair<ScopedAStatus, std::optional<DisplayDecorationSupport>> getDisplayDecorationSupport(
            int64_t display);

    std::pair<ScopedAStatus, int32_t> getMaxVirtualDisplayCount();

    std::pair<ScopedAStatus, std::string> getDisplayName(int64_t display);

    ScopedAStatus setClientTargetSlotCount(int64_t display, int32_t bufferSlotCount);

    std::pair<ScopedAStatus, std::vector<Capability>> getCapabilities();

    ScopedAStatus setBootDisplayConfig(int64_t display, int32_t config);

    ScopedAStatus clearBootDisplayConfig(int64_t display);

    std::pair<ScopedAStatus, int32_t> getPreferredBootDisplayConfig(int64_t display);

    std::pair<ScopedAStatus, std::vector<common::HdrConversionCapability>>
    getHdrConversionCapabilities();

    std::pair<ScopedAStatus, common::Hdr> setHdrConversionStrategy(
            const common::HdrConversionStrategy& conversionStrategy);

    std::pair<ScopedAStatus, common::Transform> getDisplayPhysicalOrientation(int64_t display);

    ScopedAStatus setIdleTimerEnabled(int64_t display, int32_t timeoutMs);

    int32_t getVsyncIdleCount();

    int64_t getVsyncIdleTime();

    int64_t getInvalidDisplayId();

    std::pair<ScopedAStatus, std::vector<DisplayWrapper>> getDisplays();

    std::pair<ScopedAStatus, OverlayProperties> getOverlaySupport();

    ndk::ScopedAStatus setRefreshRateChangedCallbackDebugEnabled(int64_t display, bool enabled);

    std::vector<RefreshRateChangedDebugData> takeListOfRefreshRateChangedDebugData();

    std::pair<ScopedAStatus, int32_t> getMaxLayerPictureProfiles(int64_t display);

    std::pair<ScopedAStatus, std::vector<Luts>> getLuts(int64_t display,
                                                        const std::vector<Buffer>& buffers);

    std::vector<std::pair<int64_t, common::DisplayHotplugEvent>> getAndClearLatestHotplugs();

    static constexpr int32_t kMaxFrameIntervalNs = 50000000;  // 20fps
    static constexpr int32_t kNoFrameIntervalNs = 0;

  private:
    void addDisplayConfigs(DisplayWrapper*, const std::vector<DisplayConfiguration>&);
    ScopedAStatus addDisplayConfigLegacy(DisplayWrapper*, int32_t config);
    bool getDisplayConfigurationSupported() const;
    ScopedAStatus updateDisplayProperties(DisplayWrapper* display, int32_t config);

    ScopedAStatus addDisplayToDisplayResources(int64_t display, bool isVirtual);

    ScopedAStatus addLayerToDisplayResources(int64_t display, int64_t layer);

    void removeLayerFromDisplayResources(int64_t display, int64_t layer);

    bool destroyAllLayers(std::unordered_map<int64_t, ComposerClientWriter*> displayWriters);

    bool verifyComposerCallbackParams();

    // Keep track of displays and layers. When a test fails/ends,
    // the ComposerClientWrapper::tearDown should be called from the
    // test tearDown to clean up the resources for the test.
    struct DisplayResource {
        DisplayResource(bool isVirtual_) : isVirtual(isVirtual_) {}

        bool isVirtual;
        std::unordered_set<int64_t> layers;
    };

    std::shared_ptr<IComposer> mComposer;
    std::shared_ptr<IComposerClient> mComposerClient;
    std::shared_ptr<GraphicsComposerCallback> mComposerCallback;
    std::unordered_map<int64_t, DisplayResource> mDisplayResources;
    bool mSupportsBatchedCreateLayer = false;
    std::atomic<int64_t> mNextLayerHandle = 1;
};

class DisplayWrapper {
  public:
    explicit DisplayWrapper(int64_t displayId)
        : mDisplayId(displayId), mDisplayWidth(0), mDisplayHeight(0) {}

    int64_t getDisplayId() const { return mDisplayId; }

    FRect getCrop() const {
        return {0, 0, static_cast<float>(mDisplayWidth), static_cast<float>(mDisplayHeight)};
    }

    Rect getFrameRect() const { return {0, 0, mDisplayWidth, mDisplayHeight}; }

    void setDimensions(int32_t displayWidth, int32_t displayHeight) {
        mDisplayWidth = displayWidth;
        mDisplayHeight = displayHeight;
    }

    int32_t getDisplayWidth() const { return mDisplayWidth; }

    int32_t getDisplayHeight() const { return mDisplayHeight; }

    struct DisplayConfig {
        DisplayConfig(int32_t vsyncPeriod_, int32_t configGroup_,
                      std::optional<VrrConfig> vrrConfigOpt_ = {})
            : vsyncPeriod(vsyncPeriod_),
              configGroup(configGroup_),
              vrrConfigOpt(std::move(vrrConfigOpt_)) {}
        int32_t vsyncPeriod;
        int32_t configGroup;
        std::optional<VrrConfig> vrrConfigOpt;
    };

    void addDisplayConfig(int32_t config, DisplayConfig displayConfig) {
        mDisplayConfigs.insert({config, displayConfig});
    }

    DisplayConfig getDisplayConfig(int32_t config) { return mDisplayConfigs.find(config)->second; }

    bool isRateSameBetweenConfigs(int config1, int config2) {
        const auto displayConfig1 = getDisplayConfig(config1);
        const auto displayConfig2 = getDisplayConfig(config2);
        const auto vrrConfigOpt1 = displayConfig1.vrrConfigOpt;
        const auto vrrConfigOpt2 = displayConfig2.vrrConfigOpt;

        if (vrrConfigOpt1 && vrrConfigOpt2 &&
            vrrConfigOpt1->minFrameIntervalNs == vrrConfigOpt2->minFrameIntervalNs) {
            return true;
        } else if (displayConfig1.vsyncPeriod == displayConfig2.vsyncPeriod) {
            return true;
        }
        return false;
    }

    std::string printConfig(int config) {
        const auto displayConfig = getDisplayConfig(config);
        const auto vrrConfigOpt = displayConfig.vrrConfigOpt;
        std::stringstream ss;
        if (displayConfig.vrrConfigOpt) {
            ss << "{Config " << config << ": vsyncPeriod " << displayConfig.vsyncPeriod
               << ", minFrameIntervalNs " << vrrConfigOpt->minFrameIntervalNs << "}";
        } else {
            ss << "{Config " << config << ": vsyncPeriod " << displayConfig.vsyncPeriod << "}";
        }
        return ss.str();
    }

    std::unordered_map<int32_t, DisplayConfig> getDisplayConfigs() { return mDisplayConfigs; }

  private:
    int64_t mDisplayId;
    int32_t mDisplayWidth;
    int32_t mDisplayHeight;
    std::unordered_map<int32_t, DisplayConfig> mDisplayConfigs;
};
}  // namespace aidl::android::hardware::graphics::composer3::libhwc_aidl_test
