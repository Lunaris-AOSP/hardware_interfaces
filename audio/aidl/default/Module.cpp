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

#include <algorithm>
#include <set>

#define LOG_TAG "AHAL_Module"
#include <aidl/android/media/audio/common/AudioInputFlags.h>
#include <aidl/android/media/audio/common/AudioOutputFlags.h>
#include <android-base/logging.h>
#include <android/binder_ibinder_platform.h>
#include <error/expected_utils.h>

#include "core-impl/Configuration.h"
#include "core-impl/Module.h"
#include "core-impl/ModuleBluetooth.h"
#include "core-impl/ModulePrimary.h"
#include "core-impl/ModuleRemoteSubmix.h"
#include "core-impl/ModuleStub.h"
#include "core-impl/ModuleUsb.h"
#include "core-impl/SoundDose.h"
#include "core-impl/utils.h"

using aidl::android::hardware::audio::common::frameCountFromDurationMs;
using aidl::android::hardware::audio::common::getFrameSizeInBytes;
using aidl::android::hardware::audio::common::hasMmapFlag;
using aidl::android::hardware::audio::common::isBitPositionFlagSet;
using aidl::android::hardware::audio::common::isValidAudioMode;
using aidl::android::hardware::audio::common::SinkMetadata;
using aidl::android::hardware::audio::common::SourceMetadata;
using aidl::android::hardware::audio::core::sounddose::ISoundDose;
using aidl::android::media::audio::common::AudioChannelLayout;
using aidl::android::media::audio::common::AudioDevice;
using aidl::android::media::audio::common::AudioDeviceType;
using aidl::android::media::audio::common::AudioFormatDescription;
using aidl::android::media::audio::common::AudioFormatType;
using aidl::android::media::audio::common::AudioGainConfig;
using aidl::android::media::audio::common::AudioInputFlags;
using aidl::android::media::audio::common::AudioIoFlags;
using aidl::android::media::audio::common::AudioMMapPolicy;
using aidl::android::media::audio::common::AudioMMapPolicyInfo;
using aidl::android::media::audio::common::AudioMMapPolicyType;
using aidl::android::media::audio::common::AudioMode;
using aidl::android::media::audio::common::AudioOffloadInfo;
using aidl::android::media::audio::common::AudioOutputFlags;
using aidl::android::media::audio::common::AudioPort;
using aidl::android::media::audio::common::AudioPortConfig;
using aidl::android::media::audio::common::AudioPortExt;
using aidl::android::media::audio::common::AudioProfile;
using aidl::android::media::audio::common::Boolean;
using aidl::android::media::audio::common::Int;
using aidl::android::media::audio::common::MicrophoneInfo;
using aidl::android::media::audio::common::PcmType;

namespace aidl::android::hardware::audio::core {

namespace {

inline bool hasDynamicChannelMasks(const std::vector<AudioChannelLayout>& channelMasks) {
    return channelMasks.empty() ||
           std::all_of(channelMasks.begin(), channelMasks.end(),
                       [](const auto& channelMask) { return channelMask == AudioChannelLayout{}; });
}

inline bool hasDynamicFormat(const AudioFormatDescription& format) {
    return format == AudioFormatDescription{};
}

inline bool hasDynamicSampleRates(const std::vector<int32_t>& sampleRates) {
    return sampleRates.empty() ||
           std::all_of(sampleRates.begin(), sampleRates.end(),
                       [](const auto& sampleRate) { return sampleRate == 0; });
}

inline bool isDynamicProfile(const AudioProfile& profile) {
    return hasDynamicFormat(profile.format) || hasDynamicChannelMasks(profile.channelMasks) ||
           hasDynamicSampleRates(profile.sampleRates);
}

bool hasDynamicProfilesOnly(const std::vector<AudioProfile>& profiles) {
    if (profiles.empty()) return true;
    return std::all_of(profiles.begin(), profiles.end(), isDynamicProfile);
}

bool findAudioProfile(const AudioPort& port, const AudioFormatDescription& format,
                      AudioProfile* profile) {
    if (auto profilesIt =
                find_if(port.profiles.begin(), port.profiles.end(),
                        [&format](const auto& profile) { return profile.format == format; });
        profilesIt != port.profiles.end()) {
        *profile = *profilesIt;
        return true;
    }
    return false;
}

}  // namespace

// static
std::shared_ptr<Module> Module::createInstance(Type type, std::unique_ptr<Configuration>&& config) {
    switch (type) {
        case Type::DEFAULT:
            return ndk::SharedRefBase::make<ModulePrimary>(std::move(config));
        case Type::R_SUBMIX:
            return ndk::SharedRefBase::make<ModuleRemoteSubmix>(std::move(config));
        case Type::STUB:
            return ndk::SharedRefBase::make<ModuleStub>(std::move(config));
        case Type::USB:
            return ndk::SharedRefBase::make<ModuleUsb>(std::move(config));
        case Type::BLUETOOTH:
            return ndk::SharedRefBase::make<ModuleBluetooth>(std::move(config));
    }
}

// static
std::optional<Module::Type> Module::typeFromString(const std::string& type) {
    if (type == "default")
        return Module::Type::DEFAULT;
    else if (type == "r_submix")
        return Module::Type::R_SUBMIX;
    else if (type == "stub")
        return Module::Type::STUB;
    else if (type == "usb")
        return Module::Type::USB;
    else if (type == "bluetooth")
        return Module::Type::BLUETOOTH;
    return {};
}

std::ostream& operator<<(std::ostream& os, Module::Type t) {
    switch (t) {
        case Module::Type::DEFAULT:
            os << "default";
            break;
        case Module::Type::R_SUBMIX:
            os << "r_submix";
            break;
        case Module::Type::STUB:
            os << "stub";
            break;
        case Module::Type::USB:
            os << "usb";
            break;
        case Module::Type::BLUETOOTH:
            os << "bluetooth";
            break;
    }
    return os;
}

Module::Module(Type type, std::unique_ptr<Configuration>&& config)
    : mType(type), mConfig(std::move(config)) {
    populateConnectedProfiles();
}

void Module::cleanUpPatch(int32_t patchId) {
    erase_all_values(mPatches, std::set<int32_t>{patchId});
}

ndk::ScopedAStatus Module::createStreamContext(
        int32_t in_portConfigId, int64_t in_bufferSizeFrames,
        std::shared_ptr<IStreamCallback> asyncCallback,
        std::shared_ptr<IStreamOutEventCallback> outEventCallback, StreamContext* out_context) {
    if (in_bufferSizeFrames <= 0) {
        LOG(ERROR) << __func__ << ": " << mType << ": non-positive buffer size "
                   << in_bufferSizeFrames;
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    auto& configs = getConfig().portConfigs;
    auto portConfigIt = findById<AudioPortConfig>(configs, in_portConfigId);
    const int32_t nominalLatencyMs = getNominalLatencyMs(*portConfigIt);
    // Since this is a private method, it is assumed that
    // validity of the portConfigId has already been checked.
    int32_t minimumStreamBufferSizeFrames = 0;
    if (!calculateBufferSizeFrames(
                portConfigIt->format.value(), nominalLatencyMs,
                portConfigIt->sampleRate.value().value, &minimumStreamBufferSizeFrames).isOk()) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    }
    if (in_bufferSizeFrames < minimumStreamBufferSizeFrames) {
        LOG(ERROR) << __func__ << ": " << mType << ": insufficient buffer size "
                   << in_bufferSizeFrames << ", must be at least " << minimumStreamBufferSizeFrames;
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    const size_t frameSize =
            getFrameSizeInBytes(portConfigIt->format.value(), portConfigIt->channelMask.value());
    if (frameSize == 0) {
        LOG(ERROR) << __func__ << ": " << mType
                   << ": could not calculate frame size for port config "
                   << portConfigIt->toString();
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    LOG(DEBUG) << __func__ << ": " << mType << ": frame size " << frameSize << " bytes";
    if (frameSize > static_cast<size_t>(kMaximumStreamBufferSizeBytes / in_bufferSizeFrames)) {
        LOG(ERROR) << __func__ << ": " << mType << ": buffer size " << in_bufferSizeFrames
                   << " frames is too large, maximum size is "
                   << kMaximumStreamBufferSizeBytes / frameSize;
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    const auto& flags = portConfigIt->flags.value();
    StreamContext::DebugParameters params{mDebug.streamTransientStateDelayMs,
                                          mVendorDebug.forceTransientBurst,
                                          mVendorDebug.forceSynchronousDrain};
    std::shared_ptr<ISoundDose> soundDose;
    if (!getSoundDose(&soundDose).isOk()) {
        LOG(ERROR) << __func__ << ": could not create sound dose instance";
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    }
    StreamContext temp;
    if (hasMmapFlag(flags)) {
        MmapBufferDescriptor mmapDesc;
        RETURN_STATUS_IF_ERROR(
                createMmapBuffer(*portConfigIt, in_bufferSizeFrames, frameSize, &mmapDesc));
        temp = StreamContext(
                std::make_unique<StreamContext::CommandMQ>(1, true /*configureEventFlagWord*/),
                std::make_unique<StreamContext::ReplyMQ>(1, true /*configureEventFlagWord*/),
                portConfigIt->format.value(), portConfigIt->channelMask.value(),
                portConfigIt->sampleRate.value().value, flags, nominalLatencyMs,
                portConfigIt->ext.get<AudioPortExt::mix>().handle, std::move(mmapDesc),
                outEventCallback, mSoundDose.getInstance(), params);
    } else {
        temp = StreamContext(
                std::make_unique<StreamContext::CommandMQ>(1, true /*configureEventFlagWord*/),
                std::make_unique<StreamContext::ReplyMQ>(1, true /*configureEventFlagWord*/),
                portConfigIt->format.value(), portConfigIt->channelMask.value(),
                portConfigIt->sampleRate.value().value, flags, nominalLatencyMs,
                portConfigIt->ext.get<AudioPortExt::mix>().handle,
                std::make_unique<StreamContext::DataMQ>(frameSize * in_bufferSizeFrames),
                asyncCallback, outEventCallback, mSoundDose.getInstance(), params);
    }
    if (temp.isValid()) {
        *out_context = std::move(temp);
    } else {
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    }
    return ndk::ScopedAStatus::ok();
}

std::vector<AudioDevice> Module::getDevicesFromDevicePortConfigIds(
        const std::set<int32_t>& devicePortConfigIds) {
    std::vector<AudioDevice> result;
    auto& configs = getConfig().portConfigs;
    for (const auto& id : devicePortConfigIds) {
        auto it = findById<AudioPortConfig>(configs, id);
        if (it != configs.end() && it->ext.getTag() == AudioPortExt::Tag::device) {
            result.push_back(it->ext.template get<AudioPortExt::Tag::device>().device);
        } else {
            LOG(FATAL) << __func__ << ": " << mType
                       << ": failed to find device for id" << id;
        }
    }
    return result;
}

std::vector<AudioDevice> Module::findConnectedDevices(int32_t portConfigId) {
    return getDevicesFromDevicePortConfigIds(findConnectedPortConfigIds(portConfigId));
}

std::set<int32_t> Module::findConnectedPortConfigIds(int32_t portConfigId) {
    std::set<int32_t> result;
    auto patchIdsRange = mPatches.equal_range(portConfigId);
    auto& patches = getConfig().patches;
    for (auto it = patchIdsRange.first; it != patchIdsRange.second; ++it) {
        auto patchIt = findById<AudioPatch>(patches, it->second);
        if (patchIt == patches.end()) {
            LOG(FATAL) << __func__ << ": " << mType << ": patch with id " << it->second
                       << " taken from mPatches "
                       << "not found in the configuration";
        }
        if (std::find(patchIt->sourcePortConfigIds.begin(), patchIt->sourcePortConfigIds.end(),
                      portConfigId) != patchIt->sourcePortConfigIds.end()) {
            result.insert(patchIt->sinkPortConfigIds.begin(), patchIt->sinkPortConfigIds.end());
        } else {
            result.insert(patchIt->sourcePortConfigIds.begin(), patchIt->sourcePortConfigIds.end());
        }
    }
    return result;
}

ndk::ScopedAStatus Module::findPortIdForNewStream(int32_t in_portConfigId, AudioPort** port) {
    auto& configs = getConfig().portConfigs;
    auto portConfigIt = findById<AudioPortConfig>(configs, in_portConfigId);
    if (portConfigIt == configs.end()) {
        LOG(ERROR) << __func__ << ": " << mType << ": existing port config id " << in_portConfigId
                   << " not found";
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    const int32_t portId = portConfigIt->portId;
    // In our implementation, configs of mix ports always have unique IDs.
    CHECK(portId != in_portConfigId);
    auto& ports = getConfig().ports;
    auto portIt = findById<AudioPort>(ports, portId);
    if (portIt == ports.end()) {
        LOG(ERROR) << __func__ << ": " << mType << ": port id " << portId
                   << " used by port config id " << in_portConfigId << " not found";
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    if (mStreams.count(in_portConfigId) != 0) {
        LOG(ERROR) << __func__ << ": " << mType << ": port config id " << in_portConfigId
                   << " already has a stream opened on it";
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    }
    if (portIt->ext.getTag() != AudioPortExt::Tag::mix) {
        LOG(ERROR) << __func__ << ": " << mType << ": port config id " << in_portConfigId
                   << " does not correspond to a mix port";
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    const size_t maxOpenStreamCount = portIt->ext.get<AudioPortExt::Tag::mix>().maxOpenStreamCount;
    if (maxOpenStreamCount != 0 && mStreams.count(portId) >= maxOpenStreamCount) {
        LOG(ERROR) << __func__ << ": " << mType << ": port id " << portId
                   << " has already reached maximum allowed opened stream count: "
                   << maxOpenStreamCount;
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    }
    *port = &(*portIt);
    return ndk::ScopedAStatus::ok();
}

bool Module::generateDefaultPortConfig(const AudioPort& port, AudioPortConfig* config) {
    const bool allowDynamicConfig = port.ext.getTag() == AudioPortExt::device;
    for (const auto& profile : port.profiles) {
        if (isDynamicProfile(profile)) continue;
        config->format = profile.format;
        config->channelMask = *profile.channelMasks.begin();
        config->sampleRate = Int{.value = *profile.sampleRates.begin()};
        config->flags = port.flags;
        config->ext = port.ext;
        return true;
    }
    if (allowDynamicConfig) {
        config->format = AudioFormatDescription{};
        config->channelMask = AudioChannelLayout{};
        config->sampleRate = Int{.value = 0};
        config->flags = port.flags;
        config->ext = port.ext;
        return true;
    }
    LOG(ERROR) << __func__ << ": " << mType << ": port " << port.id << " only has dynamic profiles";
    return false;
}

void Module::populateConnectedProfiles() {
    Configuration& config = getConfig();
    for (const AudioPort& port : config.ports) {
        if (port.ext.getTag() == AudioPortExt::device) {
            if (auto devicePort = port.ext.get<AudioPortExt::device>();
                !devicePort.device.type.connection.empty() && port.profiles.empty()) {
                if (auto connIt = config.connectedProfiles.find(port.id);
                    connIt == config.connectedProfiles.end()) {
                    config.connectedProfiles.emplace(
                            port.id, internal::getStandard16And24BitPcmAudioProfiles());
                }
            }
        }
    }
}

template <typename C>
std::set<int32_t> Module::portIdsFromPortConfigIds(C portConfigIds) {
    std::set<int32_t> result;
    auto& portConfigs = getConfig().portConfigs;
    for (auto it = portConfigIds.begin(); it != portConfigIds.end(); ++it) {
        auto portConfigIt = findById<AudioPortConfig>(portConfigs, *it);
        if (portConfigIt != portConfigs.end()) {
            result.insert(portConfigIt->portId);
        }
    }
    return result;
}

std::unique_ptr<Module::Configuration> Module::initializeConfig() {
    return internal::getConfiguration(getType());
}

int32_t Module::getNominalLatencyMs(const AudioPortConfig&) {
    // Arbitrary value. Implementations must override this method to provide their actual latency.
    static constexpr int32_t kLatencyMs = 5;
    return kLatencyMs;
}

ndk::ScopedAStatus Module::calculateBufferSizeFrames(
        const ::aidl::android::media::audio::common::AudioFormatDescription &format,
        int32_t latencyMs, int32_t sampleRateHz, int32_t *bufferSizeFrames) {
    if (format.type == AudioFormatType::PCM) {
        *bufferSizeFrames = calculateBufferSizeFramesForPcm(latencyMs, sampleRateHz);
        return ndk::ScopedAStatus::ok();
    }
    LOG(ERROR) << __func__ << ": " << mType << ": format " << format.toString()
        << " is not supported";
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Module::createMmapBuffer(const AudioPortConfig& portConfig __unused,
                                            int32_t bufferSizeFrames __unused,
                                            int32_t frameSizeBytes __unused,
                                            MmapBufferDescriptor* desc __unused) {
    LOG(ERROR) << __func__ << ": " << mType << ": is not implemented";
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

std::vector<AudioRoute*> Module::getAudioRoutesForAudioPortImpl(int32_t portId) {
    std::vector<AudioRoute*> result;
    auto& routes = getConfig().routes;
    for (auto& r : routes) {
        const auto& srcs = r.sourcePortIds;
        if (r.sinkPortId == portId || std::find(srcs.begin(), srcs.end(), portId) != srcs.end()) {
            result.push_back(&r);
        }
    }
    return result;
}

Module::Configuration& Module::getConfig() {
    if (!mConfig) {
        mConfig = initializeConfig();
    }
    return *mConfig;
}

std::set<int32_t> Module::getRoutableAudioPortIds(int32_t portId,
                                                  std::vector<AudioRoute*>* routes) {
    std::vector<AudioRoute*> routesStorage;
    if (routes == nullptr) {
        routesStorage = getAudioRoutesForAudioPortImpl(portId);
        routes = &routesStorage;
    }
    std::set<int32_t> result;
    for (AudioRoute* r : *routes) {
        if (r->sinkPortId == portId) {
            result.insert(r->sourcePortIds.begin(), r->sourcePortIds.end());
        } else {
            result.insert(r->sinkPortId);
        }
    }
    return result;
}

void Module::registerPatch(const AudioPatch& patch) {
    auto& configs = getConfig().portConfigs;
    auto do_insert = [&](const std::vector<int32_t>& portConfigIds) {
        for (auto portConfigId : portConfigIds) {
            auto configIt = findById<AudioPortConfig>(configs, portConfigId);
            if (configIt != configs.end()) {
                mPatches.insert(std::pair{portConfigId, patch.id});
                if (configIt->portId != portConfigId) {
                    mPatches.insert(std::pair{configIt->portId, patch.id});
                }
            }
        };
    };
    do_insert(patch.sourcePortConfigIds);
    do_insert(patch.sinkPortConfigIds);
}

ndk::ScopedAStatus Module::updateStreamsConnectedState(const AudioPatch& oldPatch,
                                                       const AudioPatch& newPatch) {
    // Notify streams about the new set of devices they are connected to.
    auto maybeFailure = ndk::ScopedAStatus::ok();
    using Connections =
            std::map<int32_t /*mixPortConfigId*/, std::set<int32_t /*devicePortConfigId*/>>;
    Connections oldConnections, newConnections;
    auto fillConnectionsHelper = [&](Connections& connections,
                                     const std::vector<int32_t>& mixPortCfgIds,
                                     const std::vector<int32_t>& devicePortCfgIds) {
        for (int32_t mixPortCfgId : mixPortCfgIds) {
            connections[mixPortCfgId].insert(devicePortCfgIds.begin(), devicePortCfgIds.end());
        }
    };
    auto fillConnections = [&](Connections& connections, const AudioPatch& patch) {
        if (std::find_if(patch.sourcePortConfigIds.begin(), patch.sourcePortConfigIds.end(),
                         [&](int32_t portConfigId) { return mStreams.count(portConfigId) > 0; }) !=
            patch.sourcePortConfigIds.end()) {
            // Sources are mix ports.
            fillConnectionsHelper(connections, patch.sourcePortConfigIds, patch.sinkPortConfigIds);
        } else if (std::find_if(patch.sinkPortConfigIds.begin(), patch.sinkPortConfigIds.end(),
                                [&](int32_t portConfigId) {
                                    return mStreams.count(portConfigId) > 0;
                                }) != patch.sinkPortConfigIds.end()) {
            // Sources are device ports.
            fillConnectionsHelper(connections, patch.sinkPortConfigIds, patch.sourcePortConfigIds);
        }  // Otherwise, there are no streams to notify.
    };
    auto restoreOldConnections = [&](const std::set<int32_t>& mixPortIds,
                                     const bool continueWithEmptyDevices) {
        for (const auto mixPort : mixPortIds) {
            if (auto it = oldConnections.find(mixPort);
                continueWithEmptyDevices || it != oldConnections.end()) {
                const std::vector<AudioDevice> d =
                        it != oldConnections.end() ? getDevicesFromDevicePortConfigIds(it->second)
                                                   : std::vector<AudioDevice>();
                if (auto status = mStreams.setStreamConnectedDevices(mixPort, d); status.isOk()) {
                    LOG(WARNING) << ":updateStreamsConnectedState: rollback: mix port config:"
                                 << mixPort
                                 << (d.empty() ? "; not connected"
                                               : std::string("; connected to ") +
                                                         ::android::internal::ToString(d));
                } else {
                    // can't do much about rollback failures
                    LOG(ERROR)
                            << ":updateStreamsConnectedState: rollback: failed for mix port config:"
                            << mixPort;
                }
            }
        }
    };
    fillConnections(oldConnections, oldPatch);
    fillConnections(newConnections, newPatch);
    /**
     * Illustration of oldConnections and newConnections
     *
     * oldConnections {
     * a : {A,B,C},
     * b : {D},
     * d : {H,I,J},
     * e : {N,O,P},
     * f : {Q,R},
     * g : {T,U,V},
     * }
     *
     * newConnections {
     * a : {A,B,C},
     * c : {E,F,G},
     * d : {K,L,M},
     * e : {N,P},
     * f : {Q,R,S},
     * g : {U,V,W},
     * }
     *
     * Expected routings:
     *      'a': is ignored both in disconnect step and connect step,
     *           due to same devices both in oldConnections and newConnections.
     *      'b': handled only in disconnect step with empty devices because 'b' is only present
     *           in oldConnections.
     *      'c': handled only in connect step with {E,F,G} devices because 'c' is only present
     *           in newConnections.
     *      'd': handled only in connect step with {K,L,M} devices because 'd' is also present
     *           in newConnections and it is ignored in disconnected step.
     *      'e': handled only in connect step with {N,P} devices because 'e' is also present
     *           in newConnections and it is ignored in disconnect step. please note that there
     *           is no exclusive disconnection for device {O}.
     *      'f': handled only in connect step with {Q,R,S} devices because 'f' is also present
     *           in newConnections and it is ignored in disconnect step. Even though stream is
     *           already connected with {Q,R} devices and connection happens with {Q,R,S}.
     *      'g': handled only in connect step with {U,V,W} devices because 'g' is also present
     *           in newConnections and it is ignored in disconnect step. There is no exclusive
     *           disconnection with devices {T,U,V}.
     *
     *       If, any failure, will lead to restoreOldConnections (rollback).
     *       The aim of the restoreOldConnections is to make connections back to oldConnections.
     *       Failures in restoreOldConnections aren't handled.
     */

    std::set<int32_t> idsToConnectBackOnFailure;
    // disconnection step
    for (const auto& [oldMixPortConfigId, oldDevicePortConfigIds] : oldConnections) {
        if (auto it = newConnections.find(oldMixPortConfigId); it == newConnections.end()) {
            idsToConnectBackOnFailure.insert(oldMixPortConfigId);
            if (auto status = mStreams.setStreamConnectedDevices(oldMixPortConfigId, {});
                status.isOk()) {
                LOG(DEBUG) << __func__ << ": The stream on port config id " << oldMixPortConfigId
                           << " has been disconnected";
            } else {
                maybeFailure = std::move(status);
                // proceed to rollback even on one failure
                break;
            }
        }
    }

    if (!maybeFailure.isOk()) {
        restoreOldConnections(idsToConnectBackOnFailure, false /*continueWithEmptyDevices*/);
        LOG(WARNING) << __func__ << ": failed to disconnect from old patch. attempted rollback";
        return maybeFailure;
    }

    std::set<int32_t> idsToRollbackOnFailure;
    // connection step
    for (const auto& [newMixPortConfigId, newDevicePortConfigIds] : newConnections) {
        if (auto it = oldConnections.find(newMixPortConfigId);
            it == oldConnections.end() || it->second != newDevicePortConfigIds) {
            const auto connectedDevices = getDevicesFromDevicePortConfigIds(newDevicePortConfigIds);
            idsToRollbackOnFailure.insert(newMixPortConfigId);
            if (connectedDevices.empty()) {
                // This is important as workers use the vector size to derive the connection status.
                LOG(FATAL) << __func__ << ": No connected devices found for port config id "
                           << newMixPortConfigId;
            }
            if (auto status =
                        mStreams.setStreamConnectedDevices(newMixPortConfigId, connectedDevices);
                status.isOk()) {
                LOG(DEBUG) << __func__ << ": The stream on port config id " << newMixPortConfigId
                           << " has been connected to: "
                           << ::android::internal::ToString(connectedDevices);
            } else {
                maybeFailure = std::move(status);
                // proceed to rollback even on one failure
                break;
            }
        }
    }

    if (!maybeFailure.isOk()) {
        restoreOldConnections(idsToConnectBackOnFailure, false /*continueWithEmptyDevices*/);
        restoreOldConnections(idsToRollbackOnFailure, true /*continueWithEmptyDevices*/);
        LOG(WARNING) << __func__ << ": failed to connect for new patch. attempted rollback";
        return maybeFailure;
    }

    return ndk::ScopedAStatus::ok();
}

binder_status_t Module::dump(int fd, const char** args, uint32_t numArgs) {
    for (const auto& portConfig : getConfig().portConfigs) {
        if (portConfig.ext.getTag() == AudioPortExt::Tag::mix) {
            getStreams().dump(portConfig.id, fd, args, numArgs);
        }
    }
    return STATUS_OK;
}

ndk::ScopedAStatus Module::setModuleDebug(
        const ::aidl::android::hardware::audio::core::ModuleDebug& in_debug) {
    LOG(DEBUG) << __func__ << ": " << mType << ": old flags:" << mDebug.toString()
               << ", new flags: " << in_debug.toString();
    if (mDebug.simulateDeviceConnections != in_debug.simulateDeviceConnections &&
        !mConnectedDevicePorts.empty()) {
        LOG(ERROR) << __func__ << ": " << mType
                   << ": attempting to change device connections simulation while "
                      "having external "
                   << "devices connected";
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    }
    if (in_debug.streamTransientStateDelayMs < 0) {
        LOG(ERROR) << __func__ << ": " << mType << ": streamTransientStateDelayMs is negative: "
                   << in_debug.streamTransientStateDelayMs;
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    mDebug = in_debug;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::getTelephony(std::shared_ptr<ITelephony>* _aidl_return) {
    *_aidl_return = nullptr;
    LOG(DEBUG) << __func__ << ": " << mType << ": returning null";
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::getBluetooth(std::shared_ptr<IBluetooth>* _aidl_return) {
    *_aidl_return = nullptr;
    LOG(DEBUG) << __func__ << ": " << mType << ": returning null";
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::getBluetoothA2dp(std::shared_ptr<IBluetoothA2dp>* _aidl_return) {
    *_aidl_return = nullptr;
    LOG(DEBUG) << __func__ << ": " << mType << ": returning null";
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::getBluetoothLe(std::shared_ptr<IBluetoothLe>* _aidl_return) {
    *_aidl_return = nullptr;
    LOG(DEBUG) << __func__ << ": " << mType << ": returning null";
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::connectExternalDevice(const AudioPort& in_templateIdAndAdditionalData,
                                                 AudioPort* _aidl_return) {
    const int32_t templateId = in_templateIdAndAdditionalData.id;
    auto& ports = getConfig().ports;
    AudioPort connectedPort;
    {  // Scope the template port so that we don't accidentally modify it.
        auto templateIt = findById<AudioPort>(ports, templateId);
        if (templateIt == ports.end()) {
            LOG(ERROR) << __func__ << ": " << mType << ": port id " << templateId << " not found";
            return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
        }
        if (templateIt->ext.getTag() != AudioPortExt::Tag::device) {
            LOG(ERROR) << __func__ << ": " << mType << ": port id " << templateId
                       << " is not a device port";
            return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
        }
        auto& templateDevicePort = templateIt->ext.get<AudioPortExt::Tag::device>();
        if (templateDevicePort.device.type.connection.empty()) {
            LOG(ERROR) << __func__ << ": " << mType << ": port id " << templateId
                       << " is permanently attached";
            return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
        }
        if (mConnectedDevicePorts.find(templateId) != mConnectedDevicePorts.end()) {
            LOG(ERROR) << __func__ << ": " << mType << ": port id " << templateId
                       << " is a connected device port";
            return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
        }
        // Postpone id allocation until we ensure that there are no client errors.
        connectedPort = *templateIt;
        connectedPort.extraAudioDescriptors = in_templateIdAndAdditionalData.extraAudioDescriptors;
        const auto& inputDevicePort =
                in_templateIdAndAdditionalData.ext.get<AudioPortExt::Tag::device>();
        auto& connectedDevicePort = connectedPort.ext.get<AudioPortExt::Tag::device>();
        connectedDevicePort.device.address = inputDevicePort.device.address;
        LOG(DEBUG) << __func__ << ": " << mType << ": device port " << connectedPort.id
                   << " device set to " << connectedDevicePort.device.toString();
        // Check if there is already a connected port with for the same external device.

        for (auto connectedPortPair : mConnectedDevicePorts) {
            auto connectedPortIt = findById<AudioPort>(ports, connectedPortPair.first);
            if (connectedPortIt->ext.get<AudioPortExt::Tag::device>().device ==
                connectedDevicePort.device) {
                LOG(ERROR) << __func__ << ": " << mType << ": device "
                           << connectedDevicePort.device.toString()
                           << " is already connected at the device port id "
                           << connectedPortPair.first;
                return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
            }
        }
    }

    // Two main cases are considered with regard to the profiles of the connected device port:
    //
    //  1. If the template device port has dynamic profiles, and at least one routable mix
    //     port also has dynamic profiles, it means that after connecting the device, the
    //     connected device port must have profiles populated with actual capabilities of
    //     the connected device, and dynamic of routable mix ports will be filled
    //     according to these capabilities. An example of this case is connection of an
    //     HDMI or USB device. For USB handled by ADSP, there can be mix ports with static
    //     profiles, and one dedicated mix port for "hi-fi" playback. The latter is left with
    //     dynamic profiles so that they can be populated with actual capabilities of
    //     the connected device.
    //
    //  2. If the template device port has dynamic profiles, while all routable mix ports
    //     have static profiles, it means that after connecting the device, the connected
    //     device port can be left with dynamic profiles, and profiles of mix ports are
    //     left untouched. An example of this case is connection of an analog wired
    //     headset, it should be treated in the same way as a speaker.
    //
    //  Yet another possible case is when both the template device port and all routable
    //  mix ports have static profiles. This is allowed and handled correctly, however, it
    //  is not very practical, since these profiles are likely duplicates of each other.

    std::vector<AudioRoute*> routesToMixPorts = getAudioRoutesForAudioPortImpl(templateId);
    std::set<int32_t> routableMixPortIds = getRoutableAudioPortIds(templateId, &routesToMixPorts);
    const int32_t nextPortId = getConfig().nextPortId++;
    if (!mDebug.simulateDeviceConnections) {
        // Even if the device port has static profiles, the HAL module might need to update
        // them, or abort the connection process.
        RETURN_STATUS_IF_ERROR(populateConnectedDevicePort(&connectedPort, nextPortId));
    } else if (hasDynamicProfilesOnly(connectedPort.profiles)) {
        auto& connectedProfiles = getConfig().connectedProfiles;
        if (auto connectedProfilesIt = connectedProfiles.find(templateId);
            connectedProfilesIt != connectedProfiles.end()) {
            connectedPort.profiles = connectedProfilesIt->second;
        }
    }
    if (hasDynamicProfilesOnly(connectedPort.profiles)) {
        // Possible case 2. Check if all routable mix ports have static profiles.
        if (auto dynamicMixPortIt = std::find_if(ports.begin(), ports.end(),
                                                 [&routableMixPortIds](const auto& p) {
                                                     return routableMixPortIds.count(p.id) > 0 &&
                                                            hasDynamicProfilesOnly(p.profiles);
                                                 });
            dynamicMixPortIt != ports.end()) {
            LOG(ERROR) << __func__ << ": " << mType
                       << ": connected port only has dynamic profiles after connecting "
                       << "external device " << connectedPort.toString() << ", and there exist "
                       << "a routable mix port with dynamic profiles: "
                       << dynamicMixPortIt->toString();
            return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
        }
    }

    connectedPort.id = nextPortId;
    auto [connectedPortsIt, _] =
            mConnectedDevicePorts.insert(std::pair(connectedPort.id, std::set<int32_t>()));
    LOG(DEBUG) << __func__ << ": " << mType << ": template port " << templateId
               << " external device connected, "
               << "connected port ID " << connectedPort.id;
    ports.push_back(connectedPort);
    onExternalDeviceConnectionChanged(connectedPort, true /*connected*/);

    // For routes where the template port is a source, add the connected port to sources,
    // otherwise, create a new route by copying from the route for the template port.
    std::vector<AudioRoute> newRoutes;
    for (AudioRoute* r : routesToMixPorts) {
        if (r->sinkPortId == templateId) {
            newRoutes.push_back(AudioRoute{.sourcePortIds = r->sourcePortIds,
                                           .sinkPortId = connectedPort.id,
                                           .isExclusive = r->isExclusive});
        } else {
            r->sourcePortIds.push_back(connectedPort.id);
        }
    }
    auto& routes = getConfig().routes;
    routes.insert(routes.end(), newRoutes.begin(), newRoutes.end());

    if (!hasDynamicProfilesOnly(connectedPort.profiles) && !routableMixPortIds.empty()) {
        // Note: this is a simplistic approach assuming that a mix port can only be populated
        // from a single device port. Implementing support for stuffing dynamic profiles with
        // a superset of all profiles from all routable dynamic device ports would be more involved.
        for (auto& port : ports) {
            if (routableMixPortIds.count(port.id) == 0) continue;
            if (hasDynamicProfilesOnly(port.profiles)) {
                port.profiles = connectedPort.profiles;
                connectedPortsIt->second.insert(port.id);
            } else {
                // Check if profiles are not all dynamic because they were populated by
                // a previous connection. Otherwise, it means that they are actually static.
                for (const auto& cp : mConnectedDevicePorts) {
                    if (cp.second.count(port.id) > 0) {
                        connectedPortsIt->second.insert(port.id);
                        break;
                    }
                }
            }
        }
    }
    *_aidl_return = std::move(connectedPort);

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::disconnectExternalDevice(int32_t in_portId) {
    auto& ports = getConfig().ports;
    auto portIt = findById<AudioPort>(ports, in_portId);
    if (portIt == ports.end()) {
        LOG(ERROR) << __func__ << ": " << mType << ": port id " << in_portId << " not found";
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    if (portIt->ext.getTag() != AudioPortExt::Tag::device) {
        LOG(ERROR) << __func__ << ": " << mType << ": port id " << in_portId
                   << " is not a device port";
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    auto connectedPortsIt = mConnectedDevicePorts.find(in_portId);
    if (connectedPortsIt == mConnectedDevicePorts.end()) {
        LOG(ERROR) << __func__ << ": " << mType << ": port id " << in_portId
                   << " is not a connected device port";
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    auto& configs = getConfig().portConfigs;
    auto& initials = getConfig().initialConfigs;
    auto configIt = std::find_if(configs.begin(), configs.end(), [&](const auto& config) {
        if (config.portId == in_portId) {
            // Check if the configuration was provided by the client.
            const auto& initialIt = findById<AudioPortConfig>(initials, config.id);
            return initialIt == initials.end() || config != *initialIt;
        }
        return false;
    });
    if (configIt != configs.end()) {
        LOG(ERROR) << __func__ << ": " << mType << ": port id " << in_portId
                   << " has a non-default config with id " << configIt->id;
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    }
    onExternalDeviceConnectionChanged(*portIt, false /*connected*/);
    ports.erase(portIt);
    LOG(DEBUG) << __func__ << ": " << mType << ": connected device port " << in_portId
               << " released";

    auto& routes = getConfig().routes;
    for (auto routesIt = routes.begin(); routesIt != routes.end();) {
        if (routesIt->sinkPortId == in_portId) {
            routesIt = routes.erase(routesIt);
        } else {
            // Note: the list of sourcePortIds can't become empty because there must
            // be the id of the template port in the route.
            erase_if(routesIt->sourcePortIds, [in_portId](auto src) { return src == in_portId; });
            ++routesIt;
        }
    }

    // Clear profiles for mix ports that are not connected to any other ports.
    std::set<int32_t> mixPortsToClear = std::move(connectedPortsIt->second);
    mConnectedDevicePorts.erase(connectedPortsIt);
    for (const auto& connectedPort : mConnectedDevicePorts) {
        for (int32_t mixPortId : connectedPort.second) {
            mixPortsToClear.erase(mixPortId);
        }
    }
    for (int32_t mixPortId : mixPortsToClear) {
        auto mixPortIt = findById<AudioPort>(ports, mixPortId);
        if (mixPortIt != ports.end()) {
            mixPortIt->profiles = {};
        }
    }

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::prepareToDisconnectExternalDevice(int32_t in_portId) {
    auto& ports = getConfig().ports;
    auto portIt = findById<AudioPort>(ports, in_portId);
    if (portIt == ports.end()) {
        LOG(ERROR) << __func__ << ": " << mType << ": port id " << in_portId << " not found";
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    if (portIt->ext.getTag() != AudioPortExt::Tag::device) {
        LOG(ERROR) << __func__ << ": " << mType << ": port id " << in_portId
                   << " is not a device port";
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    auto connectedPortsIt = mConnectedDevicePorts.find(in_portId);
    if (connectedPortsIt == mConnectedDevicePorts.end()) {
        LOG(ERROR) << __func__ << ": " << mType << ": port id " << in_portId
                   << " is not a connected device port";
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }

    onPrepareToDisconnectExternalDevice(*portIt);

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::getAudioPatches(std::vector<AudioPatch>* _aidl_return) {
    *_aidl_return = getConfig().patches;
    LOG(DEBUG) << __func__ << ": " << mType << ": returning " << _aidl_return->size() << " patches";
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::getAudioPort(int32_t in_portId, AudioPort* _aidl_return) {
    auto& ports = getConfig().ports;
    auto portIt = findById<AudioPort>(ports, in_portId);
    if (portIt != ports.end()) {
        *_aidl_return = *portIt;
        LOG(DEBUG) << __func__ << ": " << mType << ": returning port by id " << in_portId;
        return ndk::ScopedAStatus::ok();
    }
    LOG(ERROR) << __func__ << ": " << mType << ": port id " << in_portId << " not found";
    return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
}

ndk::ScopedAStatus Module::getAudioPortConfigs(std::vector<AudioPortConfig>* _aidl_return) {
    *_aidl_return = getConfig().portConfigs;
    LOG(DEBUG) << __func__ << ": " << mType << ": returning " << _aidl_return->size()
               << " port configs";
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::getAudioPorts(std::vector<AudioPort>* _aidl_return) {
    *_aidl_return = getConfig().ports;
    LOG(DEBUG) << __func__ << ": " << mType << ": returning " << _aidl_return->size() << " ports";
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::getAudioRoutes(std::vector<AudioRoute>* _aidl_return) {
    *_aidl_return = getConfig().routes;
    LOG(DEBUG) << __func__ << ": " << mType << ": returning " << _aidl_return->size() << " routes";
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::getAudioRoutesForAudioPort(int32_t in_portId,
                                                      std::vector<AudioRoute>* _aidl_return) {
    auto& ports = getConfig().ports;
    if (auto portIt = findById<AudioPort>(ports, in_portId); portIt == ports.end()) {
        LOG(ERROR) << __func__ << ": " << mType << ": port id " << in_portId << " not found";
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    std::vector<AudioRoute*> routes = getAudioRoutesForAudioPortImpl(in_portId);
    std::transform(routes.begin(), routes.end(), std::back_inserter(*_aidl_return),
                   [](auto rptr) { return *rptr; });
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::openInputStream(const OpenInputStreamArguments& in_args,
                                           OpenInputStreamReturn* _aidl_return) {
    LOG(DEBUG) << __func__ << ": " << mType << ": port config id " << in_args.portConfigId
               << ", buffer size " << in_args.bufferSizeFrames << " frames";
    AudioPort* port = nullptr;
    RETURN_STATUS_IF_ERROR(findPortIdForNewStream(in_args.portConfigId, &port));
    if (port->flags.getTag() != AudioIoFlags::Tag::input) {
        LOG(ERROR) << __func__ << ": " << mType << ": port config id " << in_args.portConfigId
                   << " does not correspond to an input mix port";
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    StreamContext context;
    RETURN_STATUS_IF_ERROR(createStreamContext(in_args.portConfigId, in_args.bufferSizeFrames,
                                               nullptr, nullptr, &context));
    context.fillDescriptor(&_aidl_return->desc);
    std::shared_ptr<StreamIn> stream;
    RETURN_STATUS_IF_ERROR(createInputStream(std::move(context), in_args.sinkMetadata,
                                             getMicrophoneInfos(), &stream));
    StreamWrapper streamWrapper(stream);
    if (auto patchIt = mPatches.find(in_args.portConfigId); patchIt != mPatches.end()) {
        RETURN_STATUS_IF_ERROR(
                streamWrapper.setConnectedDevices(findConnectedDevices(in_args.portConfigId)));
    }
    auto streamBinder = streamWrapper.getBinder();
    AIBinder_setMinSchedulerPolicy(streamBinder.get(), SCHED_NORMAL, ANDROID_PRIORITY_AUDIO);
    AIBinder_setInheritRt(streamBinder.get(), true);
    mStreams.insert(port->id, in_args.portConfigId, std::move(streamWrapper));
    _aidl_return->stream = std::move(stream);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::openOutputStream(const OpenOutputStreamArguments& in_args,
                                            OpenOutputStreamReturn* _aidl_return) {
    LOG(DEBUG) << __func__ << ": " << mType << ": port config id " << in_args.portConfigId
               << ", has offload info? " << (in_args.offloadInfo.has_value()) << ", buffer size "
               << in_args.bufferSizeFrames << " frames";
    AudioPort* port = nullptr;
    RETURN_STATUS_IF_ERROR(findPortIdForNewStream(in_args.portConfigId, &port));
    if (port->flags.getTag() != AudioIoFlags::Tag::output) {
        LOG(ERROR) << __func__ << ": " << mType << ": port config id " << in_args.portConfigId
                   << " does not correspond to an output mix port";
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    const bool isOffload = isBitPositionFlagSet(port->flags.get<AudioIoFlags::Tag::output>(),
                                                AudioOutputFlags::COMPRESS_OFFLOAD);
    if (isOffload && !in_args.offloadInfo.has_value()) {
        LOG(ERROR) << __func__ << ": " << mType << ": port id " << port->id
                   << " has COMPRESS_OFFLOAD flag set, requires offload info";
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    const bool isNonBlocking = isBitPositionFlagSet(port->flags.get<AudioIoFlags::Tag::output>(),
                                                    AudioOutputFlags::NON_BLOCKING);
    if (isNonBlocking && in_args.callback == nullptr) {
        LOG(ERROR) << __func__ << ": " << mType << ": port id " << port->id
                   << " has NON_BLOCKING flag set, requires async callback";
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    StreamContext context;
    RETURN_STATUS_IF_ERROR(createStreamContext(in_args.portConfigId, in_args.bufferSizeFrames,
                                               isNonBlocking ? in_args.callback : nullptr,
                                               in_args.eventCallback, &context));
    context.fillDescriptor(&_aidl_return->desc);
    std::shared_ptr<StreamOut> stream;
    RETURN_STATUS_IF_ERROR(createOutputStream(std::move(context), in_args.sourceMetadata,
                                              in_args.offloadInfo, &stream));
    StreamWrapper streamWrapper(stream);
    if (auto patchIt = mPatches.find(in_args.portConfigId); patchIt != mPatches.end()) {
        RETURN_STATUS_IF_ERROR(
                streamWrapper.setConnectedDevices(findConnectedDevices(in_args.portConfigId)));
    }
    auto streamBinder = streamWrapper.getBinder();
    AIBinder_setMinSchedulerPolicy(streamBinder.get(), SCHED_NORMAL, ANDROID_PRIORITY_AUDIO);
    AIBinder_setInheritRt(streamBinder.get(), true);
    mStreams.insert(port->id, in_args.portConfigId, std::move(streamWrapper));
    _aidl_return->stream = std::move(stream);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::getSupportedPlaybackRateFactors(
        SupportedPlaybackRateFactors* _aidl_return) {
    LOG(DEBUG) << __func__ << ": " << mType;
    (void)_aidl_return;
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Module::setAudioPatch(const AudioPatch& in_requested, AudioPatch* _aidl_return) {
    LOG(DEBUG) << __func__ << ": " << mType << ": requested patch " << in_requested.toString();
    if (in_requested.sourcePortConfigIds.empty()) {
        LOG(ERROR) << __func__ << ": " << mType << ": requested patch has empty sources list";
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    if (!all_unique<int32_t>(in_requested.sourcePortConfigIds)) {
        LOG(ERROR) << __func__ << ": " << mType
                   << ": requested patch has duplicate ids in the sources list";
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    if (in_requested.sinkPortConfigIds.empty()) {
        LOG(ERROR) << __func__ << ": " << mType << ": requested patch has empty sinks list";
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    if (!all_unique<int32_t>(in_requested.sinkPortConfigIds)) {
        LOG(ERROR) << __func__ << ": " << mType
                   << ": requested patch has duplicate ids in the sinks list";
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }

    auto& configs = getConfig().portConfigs;
    std::vector<int32_t> missingIds;
    auto sources =
            selectByIds<AudioPortConfig>(configs, in_requested.sourcePortConfigIds, &missingIds);
    if (!missingIds.empty()) {
        LOG(ERROR) << __func__ << ": " << mType << ": following source port config ids not found: "
                   << ::android::internal::ToString(missingIds);
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    auto sinks = selectByIds<AudioPortConfig>(configs, in_requested.sinkPortConfigIds, &missingIds);
    if (!missingIds.empty()) {
        LOG(ERROR) << __func__ << ": " << mType << ": following sink port config ids not found: "
                   << ::android::internal::ToString(missingIds);
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    // bool indicates whether a non-exclusive route is available.
    // If only an exclusive route is available, that means the patch can not be
    // established if there is any other patch which currently uses the sink port.
    std::map<int32_t, bool> allowedSinkPorts;
    auto& routes = getConfig().routes;
    for (auto src : sources) {
        for (const auto& r : routes) {
            const auto& srcs = r.sourcePortIds;
            if (std::find(srcs.begin(), srcs.end(), src->portId) != srcs.end()) {
                if (!allowedSinkPorts[r.sinkPortId]) {  // prefer non-exclusive
                    allowedSinkPorts[r.sinkPortId] = !r.isExclusive;
                }
            }
        }
    }
    for (auto sink : sinks) {
        if (allowedSinkPorts.count(sink->portId) == 0) {
            LOG(ERROR) << __func__ << ": " << mType << ": there is no route to the sink port id "
                       << sink->portId;
            return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
        }
    }
    RETURN_STATUS_IF_ERROR(checkAudioPatchEndpointsMatch(sources, sinks));

    auto& patches = getConfig().patches;
    auto existing = patches.end();
    std::optional<decltype(mPatches)> patchesBackup;
    if (in_requested.id != 0) {
        existing = findById<AudioPatch>(patches, in_requested.id);
        if (existing != patches.end()) {
            patchesBackup = mPatches;
            cleanUpPatch(existing->id);
        } else {
            LOG(ERROR) << __func__ << ": " << mType << ": not found existing patch id "
                       << in_requested.id;
            return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
        }
    }
    // Validate the requested patch.
    for (const auto& [sinkPortId, nonExclusive] : allowedSinkPorts) {
        if (!nonExclusive && mPatches.count(sinkPortId) != 0) {
            LOG(ERROR) << __func__ << ": " << mType << ": sink port id " << sinkPortId
                       << "is exclusive and is already used by some other patch";
            if (patchesBackup.has_value()) {
                mPatches = std::move(*patchesBackup);
            }
            return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
        }
    }
    // Find the highest sample rate among mix port configs.
    std::map<int32_t, AudioPortConfig*> sampleRates;
    std::vector<AudioPortConfig*>& mixPortConfigs =
            sources[0]->ext.getTag() == AudioPortExt::mix ? sources : sinks;
    for (auto mix : mixPortConfigs) {
        sampleRates.emplace(mix->sampleRate.value().value, mix);
    }
    *_aidl_return = in_requested;
    auto maxSampleRateIt = std::max_element(sampleRates.begin(), sampleRates.end());
    const int32_t latencyMs = getNominalLatencyMs(*(maxSampleRateIt->second));
    if (!calculateBufferSizeFrames(
                maxSampleRateIt->second->format.value(), latencyMs, maxSampleRateIt->first,
                &_aidl_return->minimumStreamBufferSizeFrames).isOk()) {
        if (patchesBackup.has_value()) {
            mPatches = std::move(*patchesBackup);
        }
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    }
    _aidl_return->latenciesMs.clear();
    _aidl_return->latenciesMs.insert(_aidl_return->latenciesMs.end(),
                                     _aidl_return->sinkPortConfigIds.size(), latencyMs);
    AudioPatch oldPatch{};
    if (existing == patches.end()) {
        _aidl_return->id = getConfig().nextPatchId++;
        patches.push_back(*_aidl_return);
    } else {
        oldPatch = *existing;
        *existing = *_aidl_return;
    }
    patchesBackup = mPatches;
    registerPatch(*_aidl_return);
    if (auto status = updateStreamsConnectedState(oldPatch, *_aidl_return); !status.isOk()) {
        mPatches = std::move(*patchesBackup);
        if (existing == patches.end()) {
            patches.pop_back();
        } else {
            *existing = oldPatch;
        }
        return status;
    }

    LOG(DEBUG) << __func__ << ": " << mType << ": " << (oldPatch.id == 0 ? "created" : "updated")
               << " patch " << _aidl_return->toString();
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::setAudioPortConfig(const AudioPortConfig& in_requested,
                                              AudioPortConfig* out_suggested, bool* _aidl_return) {
    auto generate = [this](const AudioPort& port, AudioPortConfig* config) {
        return generateDefaultPortConfig(port, config);
    };
    return setAudioPortConfigImpl(in_requested, generate, out_suggested, _aidl_return);
}

ndk::ScopedAStatus Module::setAudioPortConfigImpl(
        const AudioPortConfig& in_requested,
        const std::function<bool(const ::aidl::android::media::audio::common::AudioPort& port,
                                 ::aidl::android::media::audio::common::AudioPortConfig* config)>&
                fillPortConfig,
        AudioPortConfig* out_suggested, bool* applied) {
    LOG(DEBUG) << __func__ << ": " << mType << ": requested " << in_requested.toString();
    auto& configs = getConfig().portConfigs;
    auto existing = configs.end();
    if (in_requested.id != 0) {
        if (existing = findById<AudioPortConfig>(configs, in_requested.id);
            existing == configs.end()) {
            LOG(ERROR) << __func__ << ": " << mType << ": existing port config id "
                       << in_requested.id << " not found";
            return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
        }
    }

    const int portId = existing != configs.end() ? existing->portId : in_requested.portId;
    if (portId == 0) {
        LOG(ERROR) << __func__ << ": " << mType
                   << ": requested port config does not specify portId";
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    auto& ports = getConfig().ports;
    auto portIt = findById<AudioPort>(ports, portId);
    if (portIt == ports.end()) {
        LOG(ERROR) << __func__ << ": " << mType
                   << ": requested port config points to non-existent portId " << portId;
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    if (existing != configs.end()) {
        *out_suggested = *existing;
    } else {
        AudioPortConfig newConfig;
        newConfig.portId = portIt->id;
        if (fillPortConfig(*portIt, &newConfig)) {
            *out_suggested = newConfig;
        } else {
            LOG(ERROR) << __func__ << ": " << mType
                       << ": unable generate a default config for port " << portId;
            return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
        }
    }
    // From this moment, 'out_suggested' is either an existing port config,
    // or a new generated config. Now attempt to update it according to the specified
    // fields of 'in_requested'.

    // Device ports with only dynamic profiles are used for devices that are connected via ADSP,
    // which takes care of their actual configuration automatically.
    const bool allowDynamicConfig = portIt->ext.getTag() == AudioPortExt::device &&
                                    hasDynamicProfilesOnly(portIt->profiles);
    bool requestedIsValid = true, requestedIsFullySpecified = true;

    AudioIoFlags portFlags = portIt->flags;
    if (in_requested.flags.has_value()) {
        if (in_requested.flags.value() != portFlags) {
            LOG(WARNING) << __func__ << ": " << mType << ": requested flags "
                         << in_requested.flags.value().toString() << " do not match port's "
                         << portId << " flags " << portFlags.toString();
            requestedIsValid = false;
        }
    } else {
        requestedIsFullySpecified = false;
    }

    AudioProfile portProfile;
    if (in_requested.format.has_value()) {
        const auto& format = in_requested.format.value();
        if ((format == AudioFormatDescription{} && allowDynamicConfig) ||
            findAudioProfile(*portIt, format, &portProfile)) {
            out_suggested->format = format;
        } else {
            LOG(WARNING) << __func__ << ": " << mType << ": requested format " << format.toString()
                         << " is not found in the profiles of port " << portId;
            requestedIsValid = false;
        }
    } else {
        requestedIsFullySpecified = false;
    }
    if (!(out_suggested->format.value() == AudioFormatDescription{} && allowDynamicConfig) &&
        !findAudioProfile(*portIt, out_suggested->format.value(), &portProfile)) {
        LOG(ERROR) << __func__ << ": " << mType << ": port " << portId
                   << " does not support format " << out_suggested->format.value().toString()
                   << " anymore";
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }

    if (in_requested.channelMask.has_value()) {
        const auto& channelMask = in_requested.channelMask.value();
        if ((channelMask == AudioChannelLayout{} && allowDynamicConfig) ||
            find(portProfile.channelMasks.begin(), portProfile.channelMasks.end(), channelMask) !=
                    portProfile.channelMasks.end()) {
            out_suggested->channelMask = channelMask;
        } else {
            LOG(WARNING) << __func__ << ": " << mType << ": requested channel mask "
                         << channelMask.toString() << " is not supported for the format "
                         << portProfile.format.toString() << " by the port " << portId;
            requestedIsValid = false;
        }
    } else {
        requestedIsFullySpecified = false;
    }

    if (in_requested.sampleRate.has_value()) {
        const auto& sampleRate = in_requested.sampleRate.value();
        if ((sampleRate.value == 0 && allowDynamicConfig) ||
            find(portProfile.sampleRates.begin(), portProfile.sampleRates.end(),
                 sampleRate.value) != portProfile.sampleRates.end()) {
            out_suggested->sampleRate = sampleRate;
        } else {
            LOG(WARNING) << __func__ << ": " << mType << ": requested sample rate "
                         << sampleRate.value << " is not supported for the format "
                         << portProfile.format.toString() << " by the port " << portId;
            requestedIsValid = false;
        }
    } else {
        requestedIsFullySpecified = false;
    }

    if (in_requested.gain.has_value()) {
        if (!setAudioPortConfigGain(*portIt, in_requested.gain.value())) {
            return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
        }
        out_suggested->gain = in_requested.gain.value();
    }

    if (in_requested.ext.getTag() != AudioPortExt::Tag::unspecified) {
        if (in_requested.ext.getTag() == out_suggested->ext.getTag()) {
            if (out_suggested->ext.getTag() == AudioPortExt::Tag::mix) {
                // 'AudioMixPortExt.handle' and '.usecase' are set by the client,
                // copy from in_requested.
                const auto& src = in_requested.ext.get<AudioPortExt::Tag::mix>();
                auto& dst = out_suggested->ext.get<AudioPortExt::Tag::mix>();
                dst.handle = src.handle;
                dst.usecase = src.usecase;
            }
        } else {
            LOG(WARNING) << __func__ << ": " << mType << ": requested ext tag "
                         << toString(in_requested.ext.getTag()) << " do not match port's tag "
                         << toString(out_suggested->ext.getTag());
            requestedIsValid = false;
        }
    }

    if (existing == configs.end() && requestedIsValid && requestedIsFullySpecified) {
        out_suggested->id = getConfig().nextPortId++;
        configs.push_back(*out_suggested);
        *applied = true;
        LOG(DEBUG) << __func__ << ": " << mType << ": created new port config "
                   << out_suggested->toString();
    } else if (existing != configs.end() && requestedIsValid) {
        *existing = *out_suggested;
        *applied = true;
        LOG(DEBUG) << __func__ << ": " << mType << ": updated port config "
                   << out_suggested->toString();
    } else {
        LOG(DEBUG) << __func__ << ": " << mType << ": not applied; existing config ? "
                   << (existing != configs.end()) << "; requested is valid? " << requestedIsValid
                   << ", fully specified? " << requestedIsFullySpecified;
        *applied = false;
    }
    return ndk::ScopedAStatus::ok();
}

bool Module::setAudioPortConfigGain(const AudioPort& port, const AudioGainConfig& gainRequested) {
    auto& ports = getConfig().ports;
    if (gainRequested.index < 0 || gainRequested.index >= (int)port.gains.size()) {
        LOG(ERROR) << __func__ << ": gains for port " << port.id << " is undefined";
        return false;
    }
    int stepValue = port.gains[gainRequested.index].stepValue;
    if (stepValue == 0) {
        LOG(ERROR) << __func__ << ": port gain step value is 0";
        return false;
    }
    int minValue = port.gains[gainRequested.index].minValue;
    int maxValue = port.gains[gainRequested.index].maxValue;
    if (gainRequested.values[0] > maxValue || gainRequested.values[0] < minValue) {
        LOG(ERROR) << __func__ << ": gain value " << gainRequested.values[0]
                   << " out of range of min and max gain config";
        return false;
    }
    int gainIndex = (gainRequested.values[0] - minValue) / stepValue;
    int totalSteps = (maxValue - minValue) / stepValue;
    if (totalSteps == 0) {
        LOG(ERROR) << __func__ << ": difference between port gain min value " << minValue
                   << " and max value " << maxValue << " is less than step value " << stepValue;
        return false;
    }
    // Root-power quantities are used in curve:
    // 10^((minMb / 100 + (maxMb / 100 - minMb / 100) * gainIndex / totalSteps) / (10 * 2))
    // where 100 is the conversion from mB to dB, 10 comes from the log 10 conversion from power
    // ratios, and 2 means are the square of amplitude.
    float gain =
            pow(10, (minValue + (maxValue - minValue) * (gainIndex / (float)totalSteps)) / 2000);
    if (gain < 0) {
        LOG(ERROR) << __func__ << ": gain " << gain << " is less than 0";
        return false;
    }
    for (const auto& route : getConfig().routes) {
        if (route.sinkPortId != port.id) {
            continue;
        }
        for (const auto sourcePortId : route.sourcePortIds) {
            mStreams.setGain(sourcePortId, gain);
        }
    }
    return true;
}

ndk::ScopedAStatus Module::resetAudioPatch(int32_t in_patchId) {
    auto& patches = getConfig().patches;
    auto patchIt = findById<AudioPatch>(patches, in_patchId);
    if (patchIt != patches.end()) {
        auto patchesBackup = mPatches;
        cleanUpPatch(patchIt->id);
        if (auto status = updateStreamsConnectedState(*patchIt, AudioPatch{}); !status.isOk()) {
            mPatches = std::move(patchesBackup);
            return status;
        }
        patches.erase(patchIt);
        LOG(DEBUG) << __func__ << ": " << mType << ": erased patch " << in_patchId;
        return ndk::ScopedAStatus::ok();
    }
    LOG(ERROR) << __func__ << ": " << mType << ": patch id " << in_patchId << " not found";
    return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
}

ndk::ScopedAStatus Module::resetAudioPortConfig(int32_t in_portConfigId) {
    auto& configs = getConfig().portConfigs;
    auto configIt = findById<AudioPortConfig>(configs, in_portConfigId);
    if (configIt != configs.end()) {
        if (mStreams.count(in_portConfigId) != 0) {
            LOG(ERROR) << __func__ << ": " << mType << ": port config id " << in_portConfigId
                       << " has a stream opened on it";
            return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
        }
        auto patchIt = mPatches.find(in_portConfigId);
        if (patchIt != mPatches.end()) {
            LOG(ERROR) << __func__ << ": " << mType << ": port config id " << in_portConfigId
                       << " is used by the patch with id " << patchIt->second;
            return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
        }
        auto& initials = getConfig().initialConfigs;
        auto initialIt = findById<AudioPortConfig>(initials, in_portConfigId);
        if (initialIt == initials.end()) {
            configs.erase(configIt);
            LOG(DEBUG) << __func__ << ": " << mType << ": erased port config " << in_portConfigId;
        } else if (*configIt != *initialIt) {
            *configIt = *initialIt;
            LOG(DEBUG) << __func__ << ": " << mType << ": reset port config " << in_portConfigId;
        }
        return ndk::ScopedAStatus::ok();
    }
    LOG(ERROR) << __func__ << ": " << mType << ": port config id " << in_portConfigId
               << " not found";
    return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
}

ndk::ScopedAStatus Module::getMasterMute(bool* _aidl_return) {
    *_aidl_return = mMasterMute;
    LOG(DEBUG) << __func__ << ": " << mType << ": returning " << *_aidl_return;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::setMasterMute(bool in_mute) {
    LOG(DEBUG) << __func__ << ": " << mType << ": " << in_mute;
    auto result = mDebug.simulateDeviceConnections ? ndk::ScopedAStatus::ok()
                                                   : onMasterMuteChanged(in_mute);
    if (result.isOk()) {
        mMasterMute = in_mute;
    } else {
        LOG(ERROR) << __func__ << ": " << mType << ": failed calling onMasterMuteChanged("
                   << in_mute << "), error=" << result;
        // Reset master mute if it failed.
        onMasterMuteChanged(mMasterMute);
    }
    return result;
}

ndk::ScopedAStatus Module::getMasterVolume(float* _aidl_return) {
    *_aidl_return = mMasterVolume;
    LOG(DEBUG) << __func__ << ": " << mType << ": returning " << *_aidl_return;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::setMasterVolume(float in_volume) {
    LOG(DEBUG) << __func__ << ": " << mType << ": " << in_volume;
    if (in_volume >= 0.0f && in_volume <= 1.0f) {
        auto result = mDebug.simulateDeviceConnections ? ndk::ScopedAStatus::ok()
                                                       : onMasterVolumeChanged(in_volume);
        if (result.isOk()) {
            mMasterVolume = in_volume;
        } else {
            // Reset master volume if it failed.
            LOG(ERROR) << __func__ << ": " << mType << ": failed calling onMasterVolumeChanged("
                       << in_volume << "), error=" << result;
            onMasterVolumeChanged(mMasterVolume);
        }
        return result;
    }
    LOG(ERROR) << __func__ << ": " << mType << ": invalid master volume value: " << in_volume;
    return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
}

ndk::ScopedAStatus Module::getMicMute(bool* _aidl_return) {
    *_aidl_return = mMicMute;
    LOG(DEBUG) << __func__ << ": " << mType << ": returning " << *_aidl_return;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::setMicMute(bool in_mute) {
    LOG(DEBUG) << __func__ << ": " << mType << ": " << in_mute;
    mMicMute = in_mute;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::getMicrophones(std::vector<MicrophoneInfo>* _aidl_return) {
    *_aidl_return = getMicrophoneInfos();
    LOG(DEBUG) << __func__ << ": " << mType << ": returning "
               << ::android::internal::ToString(*_aidl_return);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::updateAudioMode(AudioMode in_mode) {
    if (!isValidAudioMode(in_mode)) {
        LOG(ERROR) << __func__ << ": " << mType << ": invalid mode " << toString(in_mode);
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    // No checks for supported audio modes here, it's an informative notification.
    LOG(DEBUG) << __func__ << ": " << mType << ": " << toString(in_mode);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::updateScreenRotation(ScreenRotation in_rotation) {
    LOG(DEBUG) << __func__ << ": " << mType << ": " << toString(in_rotation);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::updateScreenState(bool in_isTurnedOn) {
    LOG(DEBUG) << __func__ << ": " << mType << ": " << in_isTurnedOn;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::getSoundDose(std::shared_ptr<ISoundDose>* _aidl_return) {
    if (!mSoundDose) {
        mSoundDose = ndk::SharedRefBase::make<sounddose::SoundDose>();
    }
    *_aidl_return = mSoundDose.getInstance();
    LOG(DEBUG) << __func__ << ": " << mType
               << ": returning instance of ISoundDose: " << _aidl_return->get();
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::generateHwAvSyncId(int32_t* _aidl_return) {
    LOG(DEBUG) << __func__ << ": " << mType;
    (void)_aidl_return;
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

const std::string Module::VendorDebug::kForceTransientBurstName = "aosp.forceTransientBurst";
const std::string Module::VendorDebug::kForceSynchronousDrainName = "aosp.forceSynchronousDrain";
const std::string Module::kClipTransitionSupportName = "aosp.clipTransitionSupport";

ndk::ScopedAStatus Module::getVendorParameters(const std::vector<std::string>& in_ids,
                                               std::vector<VendorParameter>* _aidl_return) {
    LOG(VERBOSE) << __func__ << ": " << mType << ": id count: " << in_ids.size();
    bool allParametersKnown = true;
    for (const auto& id : in_ids) {
        if (id == VendorDebug::kForceTransientBurstName) {
            VendorParameter forceTransientBurst{.id = id};
            forceTransientBurst.ext.setParcelable(Boolean{mVendorDebug.forceTransientBurst});
            _aidl_return->push_back(std::move(forceTransientBurst));
        } else if (id == VendorDebug::kForceSynchronousDrainName) {
            VendorParameter forceSynchronousDrain{.id = id};
            forceSynchronousDrain.ext.setParcelable(Boolean{mVendorDebug.forceSynchronousDrain});
            _aidl_return->push_back(std::move(forceSynchronousDrain));
        } else if (id == kClipTransitionSupportName) {
            VendorParameter clipTransitionSupport{.id = id};
            clipTransitionSupport.ext.setParcelable(Boolean{true});
            _aidl_return->push_back(std::move(clipTransitionSupport));
        } else {
            allParametersKnown = false;
            LOG(VERBOSE) << __func__ << ": " << mType << ": unrecognized parameter \"" << id << "\"";
        }
    }
    if (allParametersKnown) return ndk::ScopedAStatus::ok();
    return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
}

namespace {

template <typename W>
bool extractParameter(const VendorParameter& p, decltype(W::value)* v) {
    std::optional<W> value;
    binder_status_t result = p.ext.getParcelable(&value);
    if (result == STATUS_OK && value.has_value()) {
        *v = value.value().value;
        return true;
    }
    LOG(ERROR) << __func__ << ": failed to read the value of the parameter \"" << p.id
               << "\": " << result;
    return false;
}

}  // namespace

ndk::ScopedAStatus Module::setVendorParameters(const std::vector<VendorParameter>& in_parameters,
                                               bool in_async) {
    LOG(VERBOSE) << __func__ << ": " << mType << ": parameter count " << in_parameters.size()
                 << ", async: " << in_async;
    bool allParametersKnown = true;
    for (const auto& p : in_parameters) {
        if (p.id == VendorDebug::kForceTransientBurstName) {
            if (!extractParameter<Boolean>(p, &mVendorDebug.forceTransientBurst)) {
                return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
            }
        } else if (p.id == VendorDebug::kForceSynchronousDrainName) {
            if (!extractParameter<Boolean>(p, &mVendorDebug.forceSynchronousDrain)) {
                return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
            }
        } else {
            allParametersKnown = false;
            LOG(VERBOSE) << __func__ << ": " << mType << ": unrecognized parameter \"" << p.id
                         << "\"";
        }
    }
    if (allParametersKnown) return ndk::ScopedAStatus::ok();
    return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
}

ndk::ScopedAStatus Module::addDeviceEffect(
        int32_t in_portConfigId,
        const std::shared_ptr<::aidl::android::hardware::audio::effect::IEffect>& in_effect) {
    if (in_effect == nullptr) {
        LOG(DEBUG) << __func__ << ": " << mType << ": port id " << in_portConfigId
                   << ", null effect";
    } else {
        LOG(DEBUG) << __func__ << ": " << mType << ": port id " << in_portConfigId
                   << ", effect Binder " << in_effect->asBinder().get();
    }
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Module::removeDeviceEffect(
        int32_t in_portConfigId,
        const std::shared_ptr<::aidl::android::hardware::audio::effect::IEffect>& in_effect) {
    if (in_effect == nullptr) {
        LOG(DEBUG) << __func__ << ": " << mType << ": port id " << in_portConfigId
                   << ", null effect";
    } else {
        LOG(DEBUG) << __func__ << ": " << mType << ": port id " << in_portConfigId
                   << ", effect Binder " << in_effect->asBinder().get();
    }
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Module::getMmapPolicyInfos(AudioMMapPolicyType mmapPolicyType,
                                              std::vector<AudioMMapPolicyInfo>* _aidl_return) {
    LOG(DEBUG) << __func__ << ": " << mType << ": mmap policy type " << toString(mmapPolicyType);
    std::set<int32_t> mmapSinks;
    std::set<int32_t> mmapSources;
    auto& ports = getConfig().ports;
    for (const auto& port : ports) {
        if (port.flags.getTag() == AudioIoFlags::Tag::input &&
            isBitPositionFlagSet(port.flags.get<AudioIoFlags::Tag::input>(),
                                 AudioInputFlags::MMAP_NOIRQ)) {
            mmapSinks.insert(port.id);
        } else if (port.flags.getTag() == AudioIoFlags::Tag::output &&
                   isBitPositionFlagSet(port.flags.get<AudioIoFlags::Tag::output>(),
                                        AudioOutputFlags::MMAP_NOIRQ)) {
            mmapSources.insert(port.id);
        }
    }
    if (mmapSources.empty() && mmapSinks.empty()) {
        AudioMMapPolicyInfo never;
        never.mmapPolicy = AudioMMapPolicy::NEVER;
        _aidl_return->push_back(never);
        return ndk::ScopedAStatus::ok();
    }
    for (const auto& route : getConfig().routes) {
        if (mmapSinks.count(route.sinkPortId) != 0) {
            // The sink is a mix port, add the sources if they are device ports.
            for (int sourcePortId : route.sourcePortIds) {
                auto sourcePortIt = findById<AudioPort>(ports, sourcePortId);
                if (sourcePortIt == ports.end()) {
                    // This must not happen
                    LOG(ERROR) << __func__ << ": " << mType << ": port id " << sourcePortId
                               << " cannot be found";
                    continue;
                }
                if (sourcePortIt->ext.getTag() != AudioPortExt::Tag::device) {
                    // The source is not a device port, skip
                    continue;
                }
                AudioMMapPolicyInfo policyInfo;
                policyInfo.device = sourcePortIt->ext.get<AudioPortExt::Tag::device>().device;
                // Always return AudioMMapPolicy.AUTO if the device supports mmap for
                // default implementation.
                policyInfo.mmapPolicy = AudioMMapPolicy::AUTO;
                _aidl_return->push_back(policyInfo);
            }
        } else {
            auto sinkPortIt = findById<AudioPort>(ports, route.sinkPortId);
            if (sinkPortIt == ports.end()) {
                // This must not happen
                LOG(ERROR) << __func__ << ": " << mType << ": port id " << route.sinkPortId
                           << " cannot be found";
                continue;
            }
            if (sinkPortIt->ext.getTag() != AudioPortExt::Tag::device) {
                // The sink is not a device port, skip
                continue;
            }
            if (count_any(mmapSources, route.sourcePortIds)) {
                AudioMMapPolicyInfo policyInfo;
                policyInfo.device = sinkPortIt->ext.get<AudioPortExt::Tag::device>().device;
                // Always return AudioMMapPolicy.AUTO if the device supports mmap for
                // default implementation.
                policyInfo.mmapPolicy = AudioMMapPolicy::AUTO;
                _aidl_return->push_back(policyInfo);
            }
        }
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::supportsVariableLatency(bool* _aidl_return) {
    LOG(DEBUG) << __func__ << ": " << mType;
    *_aidl_return = false;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::getAAudioMixerBurstCount(int32_t* _aidl_return) {
    if (!isMmapSupported()) {
        LOG(DEBUG) << __func__ << ": " << mType << ": mmap is not supported ";
        return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }
    *_aidl_return = DEFAULT_AAUDIO_MIXER_BURST_COUNT;
    LOG(DEBUG) << __func__ << ": " << mType << ": returning " << *_aidl_return;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::getAAudioHardwareBurstMinUsec(int32_t* _aidl_return) {
    if (!isMmapSupported()) {
        LOG(DEBUG) << __func__ << ": " << mType << ": mmap is not supported ";
        return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }
    *_aidl_return = DEFAULT_AAUDIO_HARDWARE_BURST_MIN_DURATION_US;
    LOG(DEBUG) << __func__ << ": " << mType << ": returning " << *_aidl_return;
    return ndk::ScopedAStatus::ok();
}

bool Module::isMmapSupported() {
    if (mIsMmapSupported.has_value()) {
        return mIsMmapSupported.value();
    }
    std::vector<AudioMMapPolicyInfo> mmapPolicyInfos;
    if (!getMmapPolicyInfos(AudioMMapPolicyType::DEFAULT, &mmapPolicyInfos).isOk()) {
        mIsMmapSupported = false;
    } else {
        mIsMmapSupported =
                std::find_if(mmapPolicyInfos.begin(), mmapPolicyInfos.end(), [](const auto& info) {
                    return info.mmapPolicy == AudioMMapPolicy::AUTO ||
                           info.mmapPolicy == AudioMMapPolicy::ALWAYS;
                }) != mmapPolicyInfos.end();
    }
    return mIsMmapSupported.value();
}

ndk::ScopedAStatus Module::populateConnectedDevicePort(AudioPort* audioPort, int32_t) {
    if (audioPort->ext.getTag() != AudioPortExt::device) {
        LOG(ERROR) << __func__ << ": " << mType << ": not a device port: " << audioPort->toString();
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    const auto& devicePort = audioPort->ext.get<AudioPortExt::device>();
    if (!devicePort.device.type.connection.empty()) {
        LOG(ERROR) << __func__ << ": " << mType << ": module implementation must override "
                                                  "'populateConnectedDevicePort' "
                   << "to handle connection of external devices.";
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    }
    LOG(VERBOSE) << __func__ << ": " << mType << ": do nothing and return ok";
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::checkAudioPatchEndpointsMatch(
        const std::vector<AudioPortConfig*>& sources __unused,
        const std::vector<AudioPortConfig*>& sinks __unused) {
    LOG(VERBOSE) << __func__ << ": " << mType << ": do nothing and return ok";
    return ndk::ScopedAStatus::ok();
}

void Module::onExternalDeviceConnectionChanged(
        const ::aidl::android::media::audio::common::AudioPort& audioPort __unused,
        bool connected __unused) {
    LOG(DEBUG) << __func__ << ": " << mType << ": do nothing and return";
}

void Module::onPrepareToDisconnectExternalDevice(
        const ::aidl::android::media::audio::common::AudioPort& audioPort __unused) {
    LOG(DEBUG) << __func__ << ": " << mType << ": do nothing and return";
}

ndk::ScopedAStatus Module::onMasterMuteChanged(bool mute __unused) {
    LOG(VERBOSE) << __func__ << ": " << mType << ": do nothing and return ok";
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::onMasterVolumeChanged(float volume __unused) {
    LOG(VERBOSE) << __func__ << ": " << mType << ": do nothing and return ok";
    return ndk::ScopedAStatus::ok();
}

std::vector<MicrophoneInfo> Module::getMicrophoneInfos() {
    std::vector<MicrophoneInfo> result;
    Configuration& config = getConfig();
    for (const AudioPort& port : config.ports) {
        if (port.ext.getTag() == AudioPortExt::Tag::device) {
            const AudioDeviceType deviceType =
                    port.ext.get<AudioPortExt::Tag::device>().device.type.type;
            if (deviceType == AudioDeviceType::IN_MICROPHONE ||
                deviceType == AudioDeviceType::IN_MICROPHONE_BACK) {
                // Placeholder values. Vendor implementations must populate MicrophoneInfo
                // accordingly based on their physical microphone parameters.
                result.push_back(MicrophoneInfo{
                        .id = port.name,
                        .device = port.ext.get<AudioPortExt::Tag::device>().device,
                        .group = 0,
                        .indexInTheGroup = 0,
                });
            }
        }
    }
    return result;
}

ndk::ScopedAStatus Module::bluetoothParametersUpdated() {
    return mStreams.bluetoothParametersUpdated();
}

}  // namespace aidl::android::hardware::audio::core
