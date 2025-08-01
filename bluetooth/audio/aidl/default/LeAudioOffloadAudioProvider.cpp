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

#define LOG_TAG "BTAudioProviderLeAudioHW"

#include "LeAudioOffloadAudioProvider.h"

#include <BluetoothAudioCodecs.h>
#include <BluetoothAudioSessionReport.h>
#include <android-base/logging.h>

namespace aidl {
namespace android {
namespace hardware {
namespace bluetooth {
namespace audio {

constexpr uint8_t kLeAudioDirectionSink = 0x01;
constexpr uint8_t kLeAudioDirectionSource = 0x02;
constexpr uint8_t kIsoDataPathHci = 0x00;
constexpr uint8_t kIsoDataPathPlatformDefault = 0x01;

const std::map<CodecSpecificConfigurationLtv::SamplingFrequency, uint32_t>
    freq_to_support_bitmask_map = {
        {CodecSpecificConfigurationLtv::SamplingFrequency::HZ8000,
         CodecSpecificCapabilitiesLtv::SupportedSamplingFrequencies::HZ8000},
        {CodecSpecificConfigurationLtv::SamplingFrequency::HZ11025,
         CodecSpecificCapabilitiesLtv::SupportedSamplingFrequencies::HZ11025},
        {CodecSpecificConfigurationLtv::SamplingFrequency::HZ16000,
         CodecSpecificCapabilitiesLtv::SupportedSamplingFrequencies::HZ16000},
        {CodecSpecificConfigurationLtv::SamplingFrequency::HZ22050,
         CodecSpecificCapabilitiesLtv::SupportedSamplingFrequencies::HZ22050},
        {CodecSpecificConfigurationLtv::SamplingFrequency::HZ24000,
         CodecSpecificCapabilitiesLtv::SupportedSamplingFrequencies::HZ24000},
        {CodecSpecificConfigurationLtv::SamplingFrequency::HZ32000,
         CodecSpecificCapabilitiesLtv::SupportedSamplingFrequencies::HZ32000},
        {CodecSpecificConfigurationLtv::SamplingFrequency::HZ48000,
         CodecSpecificCapabilitiesLtv::SupportedSamplingFrequencies::HZ48000},
        {CodecSpecificConfigurationLtv::SamplingFrequency::HZ88200,
         CodecSpecificCapabilitiesLtv::SupportedSamplingFrequencies::HZ88200},
        {CodecSpecificConfigurationLtv::SamplingFrequency::HZ96000,
         CodecSpecificCapabilitiesLtv::SupportedSamplingFrequencies::HZ96000},
        {CodecSpecificConfigurationLtv::SamplingFrequency::HZ176400,
         CodecSpecificCapabilitiesLtv::SupportedSamplingFrequencies::HZ176400},
        {CodecSpecificConfigurationLtv::SamplingFrequency::HZ192000,
         CodecSpecificCapabilitiesLtv::SupportedSamplingFrequencies::HZ192000},
        {CodecSpecificConfigurationLtv::SamplingFrequency::HZ384000,
         CodecSpecificCapabilitiesLtv::SupportedSamplingFrequencies::HZ384000},
};

// Helper map from capability's tag to configuration's tag
std::map<CodecSpecificCapabilitiesLtv::Tag, CodecSpecificConfigurationLtv::Tag>
    cap_to_cfg_tag_map = {
        {CodecSpecificCapabilitiesLtv::Tag::supportedSamplingFrequencies,
         CodecSpecificConfigurationLtv::Tag::samplingFrequency},
        {CodecSpecificCapabilitiesLtv::Tag::supportedMaxCodecFramesPerSDU,
         CodecSpecificConfigurationLtv::Tag::codecFrameBlocksPerSDU},
        {CodecSpecificCapabilitiesLtv::Tag::supportedFrameDurations,
         CodecSpecificConfigurationLtv::Tag::frameDuration},
        {CodecSpecificCapabilitiesLtv::Tag::supportedAudioChannelCounts,
         CodecSpecificConfigurationLtv::Tag::audioChannelAllocation},
        {CodecSpecificCapabilitiesLtv::Tag::supportedOctetsPerCodecFrame,
         CodecSpecificConfigurationLtv::Tag::octetsPerCodecFrame},
};

const std::map<CodecSpecificConfigurationLtv::FrameDuration, uint32_t>
    fduration_to_support_fduration_map = {
        {CodecSpecificConfigurationLtv::FrameDuration::US7500,
         CodecSpecificCapabilitiesLtv::SupportedFrameDurations::US7500},
        {CodecSpecificConfigurationLtv::FrameDuration::US10000,
         CodecSpecificCapabilitiesLtv::SupportedFrameDurations::US10000},
        {CodecSpecificConfigurationLtv::FrameDuration::US20000,
         CodecSpecificCapabilitiesLtv::SupportedFrameDurations::US20000},
};

std::map<int32_t, CodecSpecificConfigurationLtv::SamplingFrequency>
    sampling_freq_map = {
        {16000, CodecSpecificConfigurationLtv::SamplingFrequency::HZ16000},
        {24000, CodecSpecificConfigurationLtv::SamplingFrequency::HZ24000},
        {48000, CodecSpecificConfigurationLtv::SamplingFrequency::HZ48000},
        {96000, CodecSpecificConfigurationLtv::SamplingFrequency::HZ96000},
};

std::map<int32_t, CodecSpecificConfigurationLtv::FrameDuration>
    frame_duration_map = {
        {7500, CodecSpecificConfigurationLtv::FrameDuration::US7500},
        {10000, CodecSpecificConfigurationLtv::FrameDuration::US10000},
};

LeAudioOffloadOutputAudioProvider::LeAudioOffloadOutputAudioProvider()
    : LeAudioOffloadAudioProvider() {
  session_type_ = SessionType::LE_AUDIO_HARDWARE_OFFLOAD_ENCODING_DATAPATH;
}

LeAudioOffloadInputAudioProvider::LeAudioOffloadInputAudioProvider()
    : LeAudioOffloadAudioProvider() {
  session_type_ = SessionType::LE_AUDIO_HARDWARE_OFFLOAD_DECODING_DATAPATH;
}

LeAudioOffloadBroadcastAudioProvider::LeAudioOffloadBroadcastAudioProvider()
    : LeAudioOffloadAudioProvider() {
  session_type_ =
      SessionType::LE_AUDIO_BROADCAST_HARDWARE_OFFLOAD_ENCODING_DATAPATH;
}

LeAudioOffloadAudioProvider::LeAudioOffloadAudioProvider()
    : BluetoothAudioProvider() {}

bool LeAudioOffloadAudioProvider::isValid(const SessionType& sessionType) {
  return (sessionType == session_type_);
}

ndk::ScopedAStatus LeAudioOffloadAudioProvider::startSession(
    const std::shared_ptr<IBluetoothAudioPort>& host_if,
    const AudioConfiguration& audio_config,
    const std::vector<LatencyMode>& latency_modes, DataMQDesc* _aidl_return) {
  if (session_type_ ==
      SessionType::LE_AUDIO_BROADCAST_HARDWARE_OFFLOAD_ENCODING_DATAPATH) {
    if (audio_config.getTag() != AudioConfiguration::leAudioBroadcastConfig) {
      LOG(WARNING) << __func__ << " - Invalid Audio Configuration="
                   << audio_config.toString();
      *_aidl_return = DataMQDesc();
      return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
  } else if (audio_config.getTag() != AudioConfiguration::leAudioConfig) {
    LOG(WARNING) << __func__ << " - Invalid Audio Configuration="
                 << audio_config.toString();
    *_aidl_return = DataMQDesc();
    return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
  }

  return BluetoothAudioProvider::startSession(host_if, audio_config,
                                              latency_modes, _aidl_return);
}

ndk::ScopedAStatus LeAudioOffloadAudioProvider::onSessionReady(
    DataMQDesc* _aidl_return) {
  BluetoothAudioSessionReport::OnSessionStarted(
      session_type_, stack_iface_, nullptr, *audio_config_, latency_modes_);
  *_aidl_return = DataMQDesc();
  return ndk::ScopedAStatus::ok();
}
ndk::ScopedAStatus LeAudioOffloadAudioProvider::setCodecPriority(
    const CodecId& in_codecId, int32_t in_priority) {
  codec_priority_map_[in_codecId] = in_priority;
  return ndk::ScopedAStatus::ok();
};

bool LeAudioOffloadAudioProvider::isMatchedValidCodec(CodecId cfg_codec,
                                                      CodecId req_codec) {
  auto priority = codec_priority_map_.find(cfg_codec);
  if (priority != codec_priority_map_.end() &&
      priority->second ==
          LeAudioOffloadAudioProvider::CODEC_PRIORITY_DISABLED) {
    return false;
  }
  return cfg_codec == req_codec;
}

bool LeAudioOffloadAudioProvider::filterCapabilitiesMatchedContext(
    AudioContext& setting_context,
    const IBluetoothAudioProvider::LeAudioDeviceCapabilities& capabilities) {
  // If has no metadata, assume match
  if (!capabilities.metadata.has_value()) return true;

  for (auto metadata : capabilities.metadata.value()) {
    if (!metadata.has_value()) continue;
    if (metadata.value().getTag() == MetadataLtv::Tag::preferredAudioContexts) {
      // Check all pref audio context to see if anything matched
      auto& prefer_context =
          metadata.value()
              .get<MetadataLtv::Tag::preferredAudioContexts>()
              .values;
      if (setting_context.bitmask & prefer_context.bitmask) {
        // New mask with matched capability
        setting_context.bitmask &= prefer_context.bitmask;
        return true;
      }
    }
  }

  return false;
}

bool LeAudioOffloadAudioProvider::isMatchedSamplingFreq(
    CodecSpecificConfigurationLtv::SamplingFrequency& cfg_freq,
    CodecSpecificCapabilitiesLtv::SupportedSamplingFrequencies&
        capability_freq) {
  auto p = freq_to_support_bitmask_map.find(cfg_freq);
  if (p != freq_to_support_bitmask_map.end()) {
    if (capability_freq.bitmask & p->second) {
      return true;
    }
  }
  return false;
}

bool LeAudioOffloadAudioProvider::isMatchedFrameDuration(
    CodecSpecificConfigurationLtv::FrameDuration& cfg_fduration,
    CodecSpecificCapabilitiesLtv::SupportedFrameDurations&
        capability_fduration) {
  auto p = fduration_to_support_fduration_map.find(cfg_fduration);
  if (p != fduration_to_support_fduration_map.end())
    if (capability_fduration.bitmask & p->second) {
      return true;
    }
  return false;
}

int getCountFromBitmask(int bitmask) {
  return std::bitset<32>(bitmask).count();
}

bool LeAudioOffloadAudioProvider::isMatchedAudioChannel(
    CodecSpecificConfigurationLtv::AudioChannelAllocation& cfg_channel,
    CodecSpecificCapabilitiesLtv::SupportedAudioChannelCounts&
        capability_channel) {
  int count = getCountFromBitmask(cfg_channel.bitmask);
  if (count == 1 &&
      !(capability_channel.bitmask &
        CodecSpecificCapabilitiesLtv::SupportedAudioChannelCounts::ONE))
    return false;
  if (count == 2 &&
      !(capability_channel.bitmask &
        CodecSpecificCapabilitiesLtv::SupportedAudioChannelCounts::TWO))
    return false;
  return true;
}

bool LeAudioOffloadAudioProvider::isMatchedCodecFramesPerSDU(
    CodecSpecificConfigurationLtv::CodecFrameBlocksPerSDU& cfg_frame_sdu,
    CodecSpecificCapabilitiesLtv::SupportedMaxCodecFramesPerSDU&
        capability_frame_sdu) {
  return cfg_frame_sdu.value <= capability_frame_sdu.value;
}

bool LeAudioOffloadAudioProvider::isMatchedOctetsPerCodecFrame(
    CodecSpecificConfigurationLtv::OctetsPerCodecFrame& cfg_octets,
    CodecSpecificCapabilitiesLtv::SupportedOctetsPerCodecFrame&
        capability_octets) {
  return cfg_octets.value >= capability_octets.min &&
         cfg_octets.value <= capability_octets.max;
}

bool LeAudioOffloadAudioProvider::isCapabilitiesMatchedCodecConfiguration(
    std::vector<CodecSpecificConfigurationLtv>& codec_cfg,
    std::vector<CodecSpecificCapabilitiesLtv> codec_capabilities) {
  // Convert all codec_cfg into a map of tags -> correct data
  std::map<CodecSpecificConfigurationLtv::Tag, CodecSpecificConfigurationLtv>
      cfg_tag_map;
  for (auto codec_cfg_data : codec_cfg)
    cfg_tag_map[codec_cfg_data.getTag()] = codec_cfg_data;

  for (auto& codec_capability : codec_capabilities) {
    auto cfg = cfg_tag_map.find(cap_to_cfg_tag_map[codec_capability.getTag()]);
    // If capability has this tag, but our configuration doesn't
    // Then we will assume it is matched
    if (cfg == cfg_tag_map.end()) {
      continue;
    }

    switch (codec_capability.getTag()) {
      case CodecSpecificCapabilitiesLtv::Tag::supportedSamplingFrequencies: {
        if (!isMatchedSamplingFreq(
                cfg->second.get<
                    CodecSpecificConfigurationLtv::Tag::samplingFrequency>(),
                codec_capability.get<CodecSpecificCapabilitiesLtv::Tag::
                                         supportedSamplingFrequencies>())) {
          return false;
        }
        break;
      }

      case CodecSpecificCapabilitiesLtv::Tag::supportedFrameDurations: {
        if (!isMatchedFrameDuration(
                cfg->second
                    .get<CodecSpecificConfigurationLtv::Tag::frameDuration>(),
                codec_capability.get<CodecSpecificCapabilitiesLtv::Tag::
                                         supportedFrameDurations>())) {
          return false;
        }
        break;
      }

      case CodecSpecificCapabilitiesLtv::Tag::supportedAudioChannelCounts: {
        if (!isMatchedAudioChannel(
                cfg->second.get<CodecSpecificConfigurationLtv::Tag::
                                    audioChannelAllocation>(),
                codec_capability.get<CodecSpecificCapabilitiesLtv::Tag::
                                         supportedAudioChannelCounts>())) {
          return false;
        }
        break;
      }

      case CodecSpecificCapabilitiesLtv::Tag::supportedMaxCodecFramesPerSDU: {
        if (!isMatchedCodecFramesPerSDU(
                cfg->second.get<CodecSpecificConfigurationLtv::Tag::
                                    codecFrameBlocksPerSDU>(),
                codec_capability.get<CodecSpecificCapabilitiesLtv::Tag::
                                         supportedMaxCodecFramesPerSDU>())) {
          return false;
        }
        break;
      }

      case CodecSpecificCapabilitiesLtv::Tag::supportedOctetsPerCodecFrame: {
        if (!isMatchedOctetsPerCodecFrame(
                cfg->second.get<
                    CodecSpecificConfigurationLtv::Tag::octetsPerCodecFrame>(),
                codec_capability.get<CodecSpecificCapabilitiesLtv::Tag::
                                         supportedOctetsPerCodecFrame>())) {
          return false;
        }
        break;
      }
    }
  }

  return true;
}

bool LeAudioOffloadAudioProvider::filterMatchedAseConfiguration(
    LeAudioAseConfiguration& setting_cfg,
    const LeAudioAseConfiguration& requirement_cfg) {
  // Check matching for codec configuration <=> requirement ASE codec
  // Also match if no CodecId requirement
  if (requirement_cfg.codecId.has_value()) {
    if (!setting_cfg.codecId.has_value()) return false;
    if (!isMatchedValidCodec(setting_cfg.codecId.value(),
                             requirement_cfg.codecId.value())) {
      return false;
    }
  }

  if (requirement_cfg.targetLatency !=
          LeAudioAseConfiguration::TargetLatency::UNDEFINED &&
      setting_cfg.targetLatency != requirement_cfg.targetLatency) {
    return false;
  }
  // Ignore PHY requirement

  // Check all codec configuration
  std::map<CodecSpecificConfigurationLtv::Tag, CodecSpecificConfigurationLtv>
      cfg_tag_map;
  for (auto cfg : setting_cfg.codecConfiguration)
    cfg_tag_map[cfg.getTag()] = cfg;

  for (auto requirement_cfg : requirement_cfg.codecConfiguration) {
    // Directly compare CodecSpecificConfigurationLtv
    auto cfg = cfg_tag_map.find(requirement_cfg.getTag());
    // Config not found for this requirement, cannot match
    if (cfg == cfg_tag_map.end()) {
      return false;
    }

    // Ignore matching for audio channel allocation
    // since the rule is complicated. Match outside instead
    if (requirement_cfg.getTag() ==
        CodecSpecificConfigurationLtv::Tag::audioChannelAllocation)
      continue;

    if (cfg->second != requirement_cfg) {
      return false;
    }
  }
  // Ignore vendor configuration and metadata requirement

  return true;
}

bool LeAudioOffloadAudioProvider::isMatchedBISConfiguration(
    LeAudioBisConfiguration bis_cfg,
    const IBluetoothAudioProvider::LeAudioDeviceCapabilities& capabilities) {
  if (!isMatchedValidCodec(bis_cfg.codecId, capabilities.codecId)) {
    return false;
  }
  if (!isCapabilitiesMatchedCodecConfiguration(
          bis_cfg.codecConfiguration, capabilities.codecSpecificCapabilities)) {
    return false;
  }
  return true;
}

void LeAudioOffloadAudioProvider::filterCapabilitiesAseDirectionConfiguration(
    std::vector<std::optional<AseDirectionConfiguration>>&
        direction_configurations,
    const IBluetoothAudioProvider::LeAudioDeviceCapabilities& capabilities,
    std::vector<std::optional<AseDirectionConfiguration>>&
        valid_direction_configurations) {
  for (auto direction_configuration : direction_configurations) {
    if (!direction_configuration.has_value()) continue;
    if (!direction_configuration.value().aseConfiguration.codecId.has_value())
      continue;
    if (!isMatchedValidCodec(
            direction_configuration.value().aseConfiguration.codecId.value(),
            capabilities.codecId))
      continue;
    // Check matching for codec configuration <=> codec capabilities
    if (!isCapabilitiesMatchedCodecConfiguration(
            direction_configuration.value().aseConfiguration.codecConfiguration,
            capabilities.codecSpecificCapabilities))
      continue;
    valid_direction_configurations.push_back(direction_configuration);
  }
}

int getLeAudioAseConfigurationAllocationBitmask(LeAudioAseConfiguration cfg) {
  for (auto cfg_ltv : cfg.codecConfiguration) {
    if (cfg_ltv.getTag() ==
        CodecSpecificConfigurationLtv::Tag::audioChannelAllocation) {
      return cfg_ltv
          .get<CodecSpecificConfigurationLtv::Tag::audioChannelAllocation>()
          .bitmask;
    }
  }
  return 0;
}

std::optional<AseDirectionConfiguration> findValidMonoConfig(
    std::vector<AseDirectionConfiguration>& valid_direction_configurations,
    int bitmask) {
  for (auto& cfg : valid_direction_configurations) {
    int cfg_bitmask =
        getLeAudioAseConfigurationAllocationBitmask(cfg.aseConfiguration);
    if (getCountFromBitmask(cfg_bitmask) <= 1) {
      // Modify the bitmask to be the same as the requirement
      for (auto& codec_cfg : cfg.aseConfiguration.codecConfiguration) {
        if (codec_cfg.getTag() ==
            CodecSpecificConfigurationLtv::Tag::audioChannelAllocation) {
          codec_cfg
              .get<CodecSpecificConfigurationLtv::Tag::audioChannelAllocation>()
              .bitmask = bitmask;
          return cfg;
        }
      }
    }
  }
  return std::nullopt;
}

std::vector<AseDirectionConfiguration> getValidConfigurationsFromAllocation(
    int req_allocation_bitmask,
    std::vector<AseDirectionConfiguration>& valid_direction_configurations,
    bool isExact) {
  // Prefer the same allocation_bitmask
  int channel_count = getCountFromBitmask(req_allocation_bitmask);

  if (isExact) {
    for (auto& cfg : valid_direction_configurations) {
      int cfg_bitmask =
          getLeAudioAseConfigurationAllocationBitmask(cfg.aseConfiguration);
      if (cfg_bitmask == req_allocation_bitmask) {
        LOG(DEBUG)
            << __func__
            << ": Found an exact match for the requirement allocation of "
            << cfg_bitmask;
        return {cfg};
      }
    }
    return {};
  }
  // Not using exact match strategy
  if (channel_count <= 1) {
    // Mono requirement matched if cfg is a mono config
    auto cfg = findValidMonoConfig(valid_direction_configurations,
                                   req_allocation_bitmask);
    if (cfg.has_value()) return {cfg.value()};
  } else {
    // Stereo requirement returns 2 mono configs
    // that has a combined bitmask equal to the stereo config
    std::vector<AseDirectionConfiguration> temp;
    for (int bit = 0; bit < 32; ++bit)
      if (req_allocation_bitmask & (1 << bit)) {
        auto cfg =
            findValidMonoConfig(valid_direction_configurations, (1 << bit));
        if (cfg.has_value()) temp.push_back(cfg.value());
      }
    if (temp.size() == channel_count) return temp;
  }
  return {};
}

// Check and filter each index to see if it's a match.
void LeAudioOffloadAudioProvider::filterRequirementAseDirectionConfiguration(
    std::optional<std::vector<std::optional<AseDirectionConfiguration>>>&
        direction_configurations,
    const std::vector<std::optional<AseDirectionRequirement>>& requirements,
    std::optional<std::vector<std::optional<AseDirectionConfiguration>>>&
        valid_direction_configurations,
    bool isExact) {
  if (!direction_configurations.has_value()) return;

  if (!valid_direction_configurations.has_value()) {
    valid_direction_configurations =
        std::vector<std::optional<AseDirectionConfiguration>>();
  }

  if (isExact) {
    // Exact matching process
    // Need to respect the number of device
    for (int i = 0; i < requirements.size(); ++i) {
      auto requirement = requirements[i];
      auto direction_configuration = direction_configurations.value()[i];
      if (!direction_configuration.has_value()) {
        valid_direction_configurations = std::nullopt;
        return;
      }
      auto cfg = direction_configuration.value();
      if (!filterMatchedAseConfiguration(
              cfg.aseConfiguration, requirement.value().aseConfiguration)) {
        valid_direction_configurations = std::nullopt;
        return;  // No way to match
      }
      // For exact match, we require this direction to have the same allocation.
      // If stereo, need stereo.
      // If mono, need mono (modified to the correct required allocation)
      auto req_allocation_bitmask = getLeAudioAseConfigurationAllocationBitmask(
          requirement.value().aseConfiguration);
      int req_channel_count = getCountFromBitmask(req_allocation_bitmask);
      int cfg_bitmask =
          getLeAudioAseConfigurationAllocationBitmask(cfg.aseConfiguration);
      int cfg_channel_count = getCountFromBitmask(cfg_bitmask);
      if (req_channel_count <= 1) {
        // MONO case, is a match if also mono, modify to the same allocation
        if (cfg_channel_count > 1) {
          valid_direction_configurations = std::nullopt;
          return;  // Not a match
        }
        // Modify the bitmask to be the same as the requirement
        for (auto& codec_cfg : cfg.aseConfiguration.codecConfiguration) {
          if (codec_cfg.getTag() ==
              CodecSpecificConfigurationLtv::Tag::audioChannelAllocation) {
            codec_cfg
                .get<CodecSpecificConfigurationLtv::Tag::
                         audioChannelAllocation>()
                .bitmask = req_allocation_bitmask;
            break;
          }
        }
      } else {
        // STEREO case, is a match if same allocation
        if (req_allocation_bitmask != cfg_bitmask) {
          valid_direction_configurations = std::nullopt;
          return;  // Not a match
        }
      }
      // Push to list if valid
      valid_direction_configurations.value().push_back(cfg);
    }
  } else {
    // Loose matching process
    for (auto& requirement : requirements) {
      if (!requirement.has_value()) continue;
      auto req_allocation_bitmask = getLeAudioAseConfigurationAllocationBitmask(
          requirement.value().aseConfiguration);
      auto req_channel_count = getCountFromBitmask(req_allocation_bitmask);

      auto temp = std::vector<AseDirectionConfiguration>();

      for (auto direction_configuration : direction_configurations.value()) {
        if (!direction_configuration.has_value()) continue;
        if (!filterMatchedAseConfiguration(
                direction_configuration.value().aseConfiguration,
                requirement.value().aseConfiguration))
          continue;
        // Valid if match any requirement.
        temp.push_back(direction_configuration.value());
      }

      // Get the best matching config based on channel allocation
      auto total_cfg_channel_count = 0;
      auto req_valid_configs = getValidConfigurationsFromAllocation(
          req_allocation_bitmask, temp, isExact);
      // Count and check required channel counts
      for (auto& cfg : req_valid_configs) {
        total_cfg_channel_count += getCountFromBitmask(
            getLeAudioAseConfigurationAllocationBitmask(cfg.aseConfiguration));
        valid_direction_configurations.value().push_back(cfg);
      }
      if (total_cfg_channel_count != req_channel_count) {
        valid_direction_configurations = std::nullopt;
        return;
      }
    }
  }
}

/* Get a new LeAudioAseConfigurationSetting by matching a setting with a
 * capabilities. The new setting will have a filtered list of
 * AseDirectionConfiguration that matched the capabilities */
std::optional<LeAudioAseConfigurationSetting>
LeAudioOffloadAudioProvider::getCapabilitiesMatchedAseConfigurationSettings(
    IBluetoothAudioProvider::LeAudioAseConfigurationSetting& setting,
    const IBluetoothAudioProvider::LeAudioDeviceCapabilities& capabilities,
    uint8_t direction) {
  // Create a new LeAudioAseConfigurationSetting and return
  LeAudioAseConfigurationSetting filtered_setting{
      .audioContext = setting.audioContext,
      .sinkAseConfiguration = setting.sinkAseConfiguration,
      .sourceAseConfiguration = setting.sourceAseConfiguration,
      .flags = setting.flags,
      .packing = setting.packing,
  };

  // Get a list of all matched AseDirectionConfiguration
  // for the input direction
  std::vector<std::optional<AseDirectionConfiguration>>*
      direction_configuration = nullptr;
  if (direction == kLeAudioDirectionSink) {
    if (!filtered_setting.sinkAseConfiguration.has_value()) return std::nullopt;
    direction_configuration = &filtered_setting.sinkAseConfiguration.value();
  } else {
    if (!filtered_setting.sourceAseConfiguration.has_value())
      return std::nullopt;
    direction_configuration = &filtered_setting.sourceAseConfiguration.value();
  }
  std::vector<std::optional<AseDirectionConfiguration>>
      valid_direction_configuration;
  filterCapabilitiesAseDirectionConfiguration(
      *direction_configuration, capabilities, valid_direction_configuration);

  // No valid configuration for this direction
  if (valid_direction_configuration.empty()) {
    return std::nullopt;
  }

  // Create a new LeAudioAseConfigurationSetting and return
  // For other direction will contain all settings
  if (direction == kLeAudioDirectionSink) {
    filtered_setting.sinkAseConfiguration = valid_direction_configuration;
  } else {
    filtered_setting.sourceAseConfiguration = valid_direction_configuration;
  }

  return filtered_setting;
}

/* Get a new LeAudioAseConfigurationSetting by matching a setting with a
 * requirement. The new setting will have a filtered list of
 * AseDirectionConfiguration that matched the requirement */
std::optional<LeAudioAseConfigurationSetting>
LeAudioOffloadAudioProvider::getRequirementMatchedAseConfigurationSettings(
    IBluetoothAudioProvider::LeAudioAseConfigurationSetting& setting,
    const IBluetoothAudioProvider::LeAudioConfigurationRequirement& requirement,
    bool isExact) {
  // Create a new LeAudioAseConfigurationSetting to return
  // Make context the same as the requirement
  LeAudioAseConfigurationSetting filtered_setting{
      .audioContext = requirement.audioContext,
      .packing = setting.packing,
      .flags = setting.flags,
  };

  // The number of AseDirectionRequirement in the requirement
  // is the number of device.

  // The exact matching process is as follow:
  // 1. Setting direction has the same number of cfg (ignore when null
  // require)
  // 2. For each index, it's a 1-1 filter / mapping.
  if (isExact) {
    if (requirement.sinkAseRequirement.has_value() &&
        requirement.sinkAseRequirement.value().size() !=
            setting.sinkAseConfiguration.value().size()) {
      return std::nullopt;
    }

    if (requirement.sourceAseRequirement.has_value() &&
        requirement.sourceAseRequirement.value().size() !=
            setting.sourceAseConfiguration.value().size()) {
      return std::nullopt;
    }
  }

  if (requirement.sinkAseRequirement.has_value()) {
    filterRequirementAseDirectionConfiguration(
        setting.sinkAseConfiguration, requirement.sinkAseRequirement.value(),
        filtered_setting.sinkAseConfiguration, isExact);
    if (!filtered_setting.sinkAseConfiguration.has_value()) {
      return std::nullopt;
    }
  }

  if (requirement.sourceAseRequirement.has_value()) {
    filterRequirementAseDirectionConfiguration(
        setting.sourceAseConfiguration,
        requirement.sourceAseRequirement.value(),
        filtered_setting.sourceAseConfiguration, isExact);
    if (!filtered_setting.sourceAseConfiguration.has_value()) {
      return std::nullopt;
    }
  }

  return filtered_setting;
}

std::optional<std::pair<
    std::string, IBluetoothAudioProvider::LeAudioAseConfigurationSetting>>
LeAudioOffloadAudioProvider::matchWithRequirement(
    std::vector<std::pair<
        std::string, IBluetoothAudioProvider::LeAudioAseConfigurationSetting>>&
        matched_ase_configuration_settings,
    const IBluetoothAudioProvider::LeAudioConfigurationRequirement& requirement,
    bool isMatchContext, bool isExact, bool isMatchFlags) {
  LOG(INFO) << __func__ << ": Trying to match for the requirement "
            << requirement.toString() << ", match context = " << isMatchContext
            << ", match exact = " << isExact
            << ", match flags = " << isMatchFlags;
  // Don't have to match with flag if requirements don't have flags.
  auto requirement_flags_bitmask = 0;
  if (isMatchFlags) {
    if (!requirement.flags.has_value()) return std::nullopt;
    requirement_flags_bitmask = requirement.flags.value().bitmask;
  }
  for (auto& [setting_name, setting] : matched_ase_configuration_settings) {
    // Try to match context.
    if (isMatchContext) {
      if ((setting.audioContext.bitmask & requirement.audioContext.bitmask) !=
          requirement.audioContext.bitmask)
        continue;
      LOG(DEBUG) << __func__
                 << ": Setting with matched context: name: " << setting_name
                 << ", setting: " << setting.toString();
    }

    // Try to match configuration flags
    if (isMatchFlags) {
      if (!setting.flags.has_value()) continue;
      if ((setting.flags.value().bitmask & requirement_flags_bitmask) !=
          requirement_flags_bitmask)
        continue;
      LOG(DEBUG) << __func__
                 << ": Setting with matched flags: name: " << setting_name
                 << ", setting: " << setting.toString();
    }

    auto filtered_ase_configuration_setting =
        getRequirementMatchedAseConfigurationSettings(setting, requirement,
                                                      isExact);
    if (filtered_ase_configuration_setting.has_value()) {
      LOG(INFO) << __func__ << ": Result found: name: " << setting_name
                << ", setting: "
                << filtered_ase_configuration_setting.value().toString();
      // Found a matched setting, ignore other settings
      return {{setting_name, filtered_ase_configuration_setting.value()}};
    }
  }
  // If cannot satisfy this requirement, return nullopt
  LOG(WARNING) << __func__ << ": Cannot match the requirement "
               << requirement.toString()
               << ", match context = " << isMatchContext;
  return std::nullopt;
}

// For each requirement, a valid ASE configuration will satify:
// - matched with the sink capability (if presented)
// - AND matched with the source capability (if presented)
// - and the setting need to pass the requirement
ndk::ScopedAStatus LeAudioOffloadAudioProvider::getLeAudioAseConfiguration(
    const std::optional<std::vector<
        std::optional<IBluetoothAudioProvider::LeAudioDeviceCapabilities>>>&
        in_remoteSinkAudioCapabilities,
    const std::optional<std::vector<
        std::optional<IBluetoothAudioProvider::LeAudioDeviceCapabilities>>>&
        in_remoteSourceAudioCapabilities,
    const std::vector<IBluetoothAudioProvider::LeAudioConfigurationRequirement>&
        in_requirements,
    std::vector<IBluetoothAudioProvider::LeAudioAseConfigurationSetting>*
        _aidl_return) {
  // Get all configuration settings
  std::vector<std::pair<
      std::string, IBluetoothAudioProvider::LeAudioAseConfigurationSetting>>
      ase_configuration_settings =
          BluetoothAudioCodecs::GetLeAudioAseConfigurationSettings();

  if (!in_remoteSinkAudioCapabilities.has_value() &&
      !in_remoteSourceAudioCapabilities.has_value()) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
  }

  // Matched ASE configuration with ignored audio context
  std::vector<std::pair<
      std::string, IBluetoothAudioProvider::LeAudioAseConfigurationSetting>>
      sink_matched_ase_configuration_settings;
  std::vector<std::pair<
      std::string, IBluetoothAudioProvider::LeAudioAseConfigurationSetting>>
      matched_ase_configuration_settings;

  // A setting must match both source and sink.
  // First filter all setting matched with sink capability
  if (in_remoteSinkAudioCapabilities.has_value()) {
    for (auto& [setting_name, setting] : ase_configuration_settings) {
      for (auto& capability : in_remoteSinkAudioCapabilities.value()) {
        if (!capability.has_value()) continue;
        auto filtered_ase_configuration_setting =
            getCapabilitiesMatchedAseConfigurationSettings(
                setting, capability.value(), kLeAudioDirectionSink);
        if (filtered_ase_configuration_setting.has_value()) {
          sink_matched_ase_configuration_settings.push_back(
              {setting_name, filtered_ase_configuration_setting.value()});
        }
      }
    }
  } else {
    sink_matched_ase_configuration_settings = ase_configuration_settings;
  }

  // Combine filter every source capability
  if (in_remoteSourceAudioCapabilities.has_value()) {
    for (auto& [setting_name, setting] :
         sink_matched_ase_configuration_settings)
      for (auto& capability : in_remoteSourceAudioCapabilities.value()) {
        if (!capability.has_value()) continue;
        auto filtered_ase_configuration_setting =
            getCapabilitiesMatchedAseConfigurationSettings(
                setting, capability.value(), kLeAudioDirectionSource);
        if (filtered_ase_configuration_setting.has_value()) {
          matched_ase_configuration_settings.push_back(
              {setting_name, filtered_ase_configuration_setting.value()});
        }
      }
  } else {
    matched_ase_configuration_settings =
        sink_matched_ase_configuration_settings;
  }

  std::vector<IBluetoothAudioProvider::LeAudioAseConfigurationSetting>
      result_no_name;
  std::vector<std::pair<
      std::string, IBluetoothAudioProvider::LeAudioAseConfigurationSetting>>
      result;
  for (auto& requirement : in_requirements) {
    // For each requirement, try to match with a setting.
    // If we cannot match, return an empty result.

    // Matching priority list:
    // Matched configuration flags, i.e. for asymmetric requirement.
    // Preferred context - exact match with allocation
    // Preferred context - loose match with allocation
    // Any context - exact match with allocation
    // Any context - loose match with allocation
    bool found = false;
    for (bool match_flag : {true, false}) {
      for (bool match_context : {true, false}) {
        for (bool match_exact : {true, false}) {
          auto matched_setting = matchWithRequirement(
              matched_ase_configuration_settings, requirement, match_context,
              match_exact, match_flag);
          if (matched_setting.has_value()) {
            result.push_back(matched_setting.value());
            found = true;
            break;
          }
        }
        if (found) break;
      }
      if (found) break;
    }

    if (!found) {
      LOG(ERROR) << __func__
                 << ": Cannot find any match for this requirement, exitting...";
      *_aidl_return = result_no_name;
      return ndk::ScopedAStatus::ok();
    }
  }

  LOG(INFO) << __func__
            << ": Found matches for all requirements, chosen settings:";
  for (auto& [setting_name, setting] : result) {
    LOG(INFO) << __func__ << ": name: " << setting_name
              << ", setting: " << setting.toString();
    result_no_name.push_back(setting);
  }
  *_aidl_return = result_no_name;
  return ndk::ScopedAStatus::ok();
};

bool LeAudioOffloadAudioProvider::isMatchedQosRequirement(
    LeAudioAseQosConfiguration setting_qos,
    AseQosDirectionRequirement requirement_qos) {
  if (setting_qos.retransmissionNum !=
      requirement_qos.preferredRetransmissionNum) {
    return false;
  }
  if (setting_qos.maxTransportLatencyMs >
      requirement_qos.maxTransportLatencyMs) {
    return false;
  }
  return true;
}

bool isValidQosRequirement(AseQosDirectionRequirement qosRequirement) {
  return ((qosRequirement.maxTransportLatencyMs > 0) &&
          (qosRequirement.presentationDelayMaxUs > 0) &&
          (qosRequirement.presentationDelayMaxUs >=
           qosRequirement.presentationDelayMinUs));
}

std::optional<LeAudioAseQosConfiguration>
LeAudioOffloadAudioProvider::getDirectionQosConfiguration(
    uint8_t direction,
    const IBluetoothAudioProvider::LeAudioAseQosConfigurationRequirement&
        qosRequirement,
    std::vector<std::pair<std::string, LeAudioAseConfigurationSetting>>&
        ase_configuration_settings,
    bool isExact, bool isMatchFlags) {
  auto requirement_flags_bitmask = 0;
  if (isMatchFlags) {
    if (!qosRequirement.flags.has_value()) return std::nullopt;
    requirement_flags_bitmask = qosRequirement.flags.value().bitmask;
  }

  std::optional<AseQosDirectionRequirement> direction_qos_requirement =
      std::nullopt;

  // Get the correct direction
  if (direction == kLeAudioDirectionSink) {
    direction_qos_requirement = qosRequirement.sinkAseQosRequirement.value();
  } else {
    direction_qos_requirement = qosRequirement.sourceAseQosRequirement.value();
  }

  for (auto& [setting_name, setting] : ase_configuration_settings) {
    // Context matching
    if ((setting.audioContext.bitmask & qosRequirement.audioContext.bitmask) !=
        qosRequirement.audioContext.bitmask)
      continue;
    LOG(DEBUG) << __func__
               << ": Setting with matched context: name: " << setting_name
               << ", setting: " << setting.toString();

    // Match configuration flags
    if (isMatchFlags) {
      if (!setting.flags.has_value()) continue;
      if ((setting.flags.value().bitmask & requirement_flags_bitmask) !=
          requirement_flags_bitmask)
        continue;
      LOG(DEBUG) << __func__
                 << ": Setting with matched flags: name: " << setting_name
                 << ", setting: " << setting.toString();
    }

    // Get a list of all matched AseDirectionConfiguration
    // for the input direction
    std::optional<std::vector<std::optional<AseDirectionConfiguration>>>
        direction_configuration = std::nullopt;
    if (direction == kLeAudioDirectionSink) {
      if (!setting.sinkAseConfiguration.has_value()) continue;
      direction_configuration.emplace(setting.sinkAseConfiguration.value());
    } else {
      if (!setting.sourceAseConfiguration.has_value()) continue;
      direction_configuration.emplace(setting.sourceAseConfiguration.value());
    }

    if (!direction_configuration.has_value()) {
      return std::nullopt;
    }

    // Collect all valid cfg into a vector
    // Then try to get the best match for audio allocation

    auto temp = std::vector<AseDirectionConfiguration>();

    for (auto& cfg : direction_configuration.value()) {
      if (!cfg.has_value()) continue;
      // If no requirement, return the first QoS
      if (!direction_qos_requirement.has_value()) {
        return cfg.value().qosConfiguration;
      }

      // If has requirement, return the first matched QoS
      // Try to match the ASE configuration
      // and QoS with requirement
      if (!cfg.value().qosConfiguration.has_value()) continue;
      if (filterMatchedAseConfiguration(
              cfg.value().aseConfiguration,
              direction_qos_requirement.value().aseConfiguration) &&
          isMatchedQosRequirement(cfg.value().qosConfiguration.value(),
                                  direction_qos_requirement.value())) {
        temp.push_back(cfg.value());
      }
    }
    LOG(WARNING) << __func__ << ": Got " << temp.size()
                 << " configs, start matching allocation";

    int qos_allocation_bitmask = getLeAudioAseConfigurationAllocationBitmask(
        direction_qos_requirement.value().aseConfiguration);
    // Get the best matching config based on channel allocation
    auto req_valid_configs = getValidConfigurationsFromAllocation(
        qos_allocation_bitmask, temp, isExact);
    if (req_valid_configs.empty()) {
      LOG(WARNING) << __func__
                   << ": Cannot find matching allocation for bitmask "
                   << qos_allocation_bitmask;

    } else {
      return req_valid_configs[0].qosConfiguration;
    }
  }

  return std::nullopt;
}

ndk::ScopedAStatus LeAudioOffloadAudioProvider::getLeAudioAseQosConfiguration(
    const IBluetoothAudioProvider::LeAudioAseQosConfigurationRequirement&
        in_qosRequirement,
    IBluetoothAudioProvider::LeAudioAseQosConfigurationPair* _aidl_return) {
  IBluetoothAudioProvider::LeAudioAseQosConfigurationPair result;

  // Get all configuration settings
  std::vector<std::pair<
      std::string, IBluetoothAudioProvider::LeAudioAseConfigurationSetting>>
      ase_configuration_settings =
          BluetoothAudioCodecs::GetLeAudioAseConfigurationSettings();

  // Direction QoS matching
  // Only handle one direction input case
  if (in_qosRequirement.sinkAseQosRequirement.has_value()) {
    if (!isValidQosRequirement(in_qosRequirement.sinkAseQosRequirement.value()))
      return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    bool found = false;
    for (bool match_flag : {true, false}) {
      for (bool match_exact : {true, false}) {
        auto setting = getDirectionQosConfiguration(
            kLeAudioDirectionSink, in_qosRequirement,
            ase_configuration_settings, match_exact, match_flag);
        if (setting.has_value()) {
          found = true;
          result.sinkQosConfiguration = setting;
          break;
        }
      }
      if (found) break;
    }
  }
  if (in_qosRequirement.sourceAseQosRequirement.has_value()) {
    if (!isValidQosRequirement(
            in_qosRequirement.sourceAseQosRequirement.value()))
      return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    bool found = false;
    for (bool match_flag : {true, false}) {
      for (bool match_exact : {true, false}) {
        auto setting = getDirectionQosConfiguration(
            kLeAudioDirectionSource, in_qosRequirement,
            ase_configuration_settings, match_exact, match_flag);
        if (setting.has_value()) {
          found = true;
          result.sourceQosConfiguration = setting;
          break;
        }
      }
      if (found) break;
    }
  }

  *_aidl_return = result;
  return ndk::ScopedAStatus::ok();
};

ndk::ScopedAStatus LeAudioOffloadAudioProvider::onSinkAseMetadataChanged(
    IBluetoothAudioProvider::AseState in_state, int32_t /*in_cigId*/,
    int32_t /*in_cisId*/,
    const std::optional<std::vector<std::optional<MetadataLtv>>>& in_metadata) {
  (void)in_state;
  (void)in_metadata;
  return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
};

ndk::ScopedAStatus LeAudioOffloadAudioProvider::onSourceAseMetadataChanged(
    IBluetoothAudioProvider::AseState in_state, int32_t /*in_cigId*/,
    int32_t /*in_cisId*/,
    const std::optional<std::vector<std::optional<MetadataLtv>>>& in_metadata) {
  (void)in_state;
  (void)in_metadata;
  return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
};

LeAudioBroadcastConfigurationSetting getDefaultBroadcastSetting(
    int context_bitmask, IBluetoothAudioProvider::BroadcastQuality quality) {
  LeAudioBroadcastConfigurationSetting setting;
  setting.retransmitionNum = 4;
  setting.maxTransportLatencyMs = 60;
  setting.sduIntervalUs = 10000;
  setting.maxSduOctets = 40;

  if (quality == IBluetoothAudioProvider::BroadcastQuality::HIGH) {
    LOG(INFO) << __func__ << ": High quality, returning high quality settings";
    setting.retransmitionNum = 4;
    setting.maxTransportLatencyMs = 65;
    setting.maxSduOctets = 200;
    return setting;
  }

  // Populate other settings base on context
  // TODO: Populate with better design
  if (context_bitmask & (AudioContext::LIVE_AUDIO | AudioContext::GAME)) {
    setting.retransmitionNum = 2;
    setting.maxTransportLatencyMs = 10;
    setting.maxSduOctets = 120;
  } else if (context_bitmask & (AudioContext::INSTRUCTIONAL)) {
    setting.retransmitionNum = 2;
    setting.maxTransportLatencyMs = 10;
    setting.maxSduOctets = 40;
  } else if (context_bitmask &
             (AudioContext::SOUND_EFFECTS | AudioContext::UNSPECIFIED)) {
    setting.retransmitionNum = 4;
    setting.maxTransportLatencyMs = 60;
    setting.maxSduOctets = 80;
  } else if (context_bitmask &
             (AudioContext::ALERTS | AudioContext::NOTIFICATIONS |
              AudioContext::EMERGENCY_ALARM)) {
    setting.retransmitionNum = 4;
    setting.maxTransportLatencyMs = 60;
    setting.maxSduOctets = 40;
  } else if (context_bitmask & AudioContext::MEDIA) {
    setting.retransmitionNum = 4;
    setting.maxTransportLatencyMs = 60;
    setting.maxSduOctets = 120;
  }

  return setting;
}
void modifySubBISConfigAllocation(
    IBluetoothAudioProvider::LeAudioSubgroupBisConfiguration& sub_bis_cfg,
    int allocation_bitmask) {
  for (auto& codec_cfg : sub_bis_cfg.bisConfiguration.codecConfiguration) {
    if (codec_cfg.getTag() ==
        CodecSpecificConfigurationLtv::audioChannelAllocation) {
      codec_cfg.get<CodecSpecificConfigurationLtv::audioChannelAllocation>()
          .bitmask = allocation_bitmask;
      break;
    }
  }
}
void modifySubgroupConfiguration(
    IBluetoothAudioProvider::LeAudioBroadcastSubgroupConfiguration&
        subgroup_cfg,
    int context_bitmask) {
  // STEREO configs
  // Split into 2 sub BIS config, each has numBis = 1
  if (context_bitmask & (AudioContext::LIVE_AUDIO | AudioContext::GAME |
                         AudioContext::SOUND_EFFECTS |
                         AudioContext::UNSPECIFIED | AudioContext::MEDIA)) {
    if (subgroup_cfg.bisConfigurations.size() == 1)
      subgroup_cfg.bisConfigurations.push_back(
          subgroup_cfg.bisConfigurations[0]);

    subgroup_cfg.bisConfigurations[0].numBis = 1;
    modifySubBISConfigAllocation(
        subgroup_cfg.bisConfigurations[0],
        CodecSpecificConfigurationLtv::AudioChannelAllocation::FRONT_LEFT);

    subgroup_cfg.bisConfigurations[1].numBis = 1;
    modifySubBISConfigAllocation(
        subgroup_cfg.bisConfigurations[1],
        CodecSpecificConfigurationLtv::AudioChannelAllocation::FRONT_RIGHT);
    return;
  }

  // MONO configs
  for (auto& sub_bis_cfg : subgroup_cfg.bisConfigurations) {
    sub_bis_cfg.numBis = 1;
    modifySubBISConfigAllocation(
        sub_bis_cfg,
        CodecSpecificConfigurationLtv::AudioChannelAllocation::FRONT_CENTER);
  }
}

void LeAudioOffloadAudioProvider::getBroadcastSettings() {
  if (!broadcast_settings.empty()) return;

  LOG(INFO) << __func__
            << ": Loading basic broadcast settings from provider info";

  std::vector<CodecInfo> db_codec_info =
      BluetoothAudioCodecs::GetLeAudioOffloadCodecInfo(
          SessionType::LE_AUDIO_BROADCAST_HARDWARE_OFFLOAD_ENCODING_DATAPATH);
  for (auto x : db_codec_info) {
    LOG(INFO) << __func__ << ": codec info = " << x.toString();
  }
  broadcast_settings.clear();

  // Default value population
  CodecSpecificConfigurationLtv::AudioChannelAllocation default_allocation;
  default_allocation.bitmask =
      CodecSpecificConfigurationLtv::AudioChannelAllocation::FRONT_CENTER;
  CodecSpecificConfigurationLtv::CodecFrameBlocksPerSDU default_frame;
  default_frame.value = 1;

  for (auto& codec_info : db_codec_info) {
    if (codec_info.transport.getTag() != CodecInfo::Transport::leAudio)
      continue;
    auto& transport = codec_info.transport.get<CodecInfo::Transport::leAudio>();
    LeAudioBroadcastConfigurationSetting setting;
    setting.retransmitionNum = 4;
    setting.maxTransportLatencyMs = 60;
    setting.sduIntervalUs = 10000;
    setting.maxSduOctets = 40;
    // Default setting
    setting.numBis = 1;
    setting.phy = {Phy::TWO_M};
    // Populate BIS configuration info using codec_info
    LeAudioBisConfiguration bis_cfg;
    bis_cfg.codecId = codec_info.id;

    CodecSpecificConfigurationLtv::OctetsPerCodecFrame octets;
    octets.value = transport.bitdepth[0];

    bis_cfg.codecConfiguration = {
        sampling_freq_map[transport.samplingFrequencyHz[0]],
        octets,
        frame_duration_map[transport.frameDurationUs[0]],
        default_allocation,
        default_frame,
    };

    // Ignore bis_cfg.metadata

    // Add information to structure
    IBluetoothAudioProvider::LeAudioSubgroupBisConfiguration sub_bis_cfg;
    sub_bis_cfg.numBis = 1;
    sub_bis_cfg.bisConfiguration = bis_cfg;
    IBluetoothAudioProvider::LeAudioBroadcastSubgroupConfiguration sub_cfg;
    // Populate the same sub config
    sub_cfg.bisConfigurations = {sub_bis_cfg};
    setting.subgroupsConfigurations = {sub_cfg};

    broadcast_settings.push_back(setting);
  }

  LOG(INFO) << __func__
            << ": Done loading broadcast settings from provider info";
}

/* Get a new LeAudioAseConfigurationSetting by matching a setting with a
 * capabilities. The new setting will have a filtered list of
 * AseDirectionConfiguration that matched the capabilities */
std::optional<LeAudioBroadcastConfigurationSetting>
LeAudioOffloadAudioProvider::
    getCapabilitiesMatchedBroadcastConfigurationSettings(
        LeAudioBroadcastConfigurationSetting& setting,
        const IBluetoothAudioProvider::LeAudioDeviceCapabilities&
            capabilities) {
  std::vector<IBluetoothAudioProvider::LeAudioBroadcastSubgroupConfiguration>
      filter_subgroup;
  for (auto& sub_cfg : setting.subgroupsConfigurations) {
    std::vector<IBluetoothAudioProvider::LeAudioSubgroupBisConfiguration>
        filtered_bis_cfg;
    for (auto& bis_cfg : sub_cfg.bisConfigurations)
      if (isMatchedBISConfiguration(bis_cfg.bisConfiguration, capabilities)) {
        filtered_bis_cfg.push_back(bis_cfg);
      }
    if (!filtered_bis_cfg.empty()) {
      IBluetoothAudioProvider::LeAudioBroadcastSubgroupConfiguration
          subgroup_cfg;
      subgroup_cfg.bisConfigurations = filtered_bis_cfg;
      filter_subgroup.push_back(subgroup_cfg);
    }
  }
  if (filter_subgroup.empty()) return std::nullopt;

  // Create a new LeAudioAseConfigurationSetting and return
  LeAudioBroadcastConfigurationSetting filtered_setting(setting);
  filtered_setting.subgroupsConfigurations = filter_subgroup;

  return filtered_setting;
}

std::vector<CodecSpecificConfigurationLtv> getCodecRequirementBasedOnContext(
    int context_bitmask, IBluetoothAudioProvider::BroadcastQuality quality) {
  // Default requirement: lc3_stereo_16_2
  std::vector<CodecSpecificConfigurationLtv> requirement = {
      CodecSpecificConfigurationLtv::SamplingFrequency::HZ16000,
      CodecSpecificConfigurationLtv::FrameDuration::US10000,
  };

  if (quality == IBluetoothAudioProvider::BroadcastQuality::HIGH) {
    LOG(INFO) << __func__
              << ": High quality, returning high quality requirement";
    requirement = {
        CodecSpecificConfigurationLtv::SamplingFrequency::HZ48000,
        CodecSpecificConfigurationLtv::FrameDuration::US10000,
    };
    return requirement;
  }

  if (context_bitmask & (AudioContext::LIVE_AUDIO | AudioContext::GAME)) {
    // lc3_stereo_24_2_1
    requirement = {
        CodecSpecificConfigurationLtv::SamplingFrequency::HZ24000,
        CodecSpecificConfigurationLtv::FrameDuration::US10000,
    };
  } else if (context_bitmask & (AudioContext::INSTRUCTIONAL)) {
    // lc3_mono_16_2
    requirement = {
        CodecSpecificConfigurationLtv::SamplingFrequency::HZ16000,
        CodecSpecificConfigurationLtv::FrameDuration::US10000,
    };
  } else if (context_bitmask &
             (AudioContext::SOUND_EFFECTS | AudioContext::UNSPECIFIED)) {
    // lc3_stereo_16_2
    requirement = {
        CodecSpecificConfigurationLtv::SamplingFrequency::HZ16000,
        CodecSpecificConfigurationLtv::FrameDuration::US10000,
    };
  } else if (context_bitmask &
             (AudioContext::ALERTS | AudioContext::NOTIFICATIONS |
              AudioContext::EMERGENCY_ALARM)) {
    // Default requirement: lc3_stereo_16_2
    requirement = {
        CodecSpecificConfigurationLtv::SamplingFrequency::HZ16000,
        CodecSpecificConfigurationLtv::FrameDuration::US10000,
    };
  } else if (context_bitmask & AudioContext::MEDIA) {
    // Default requirement: lc3_stereo_16_2
    // Return the 48k requirement
    requirement = {
        CodecSpecificConfigurationLtv::SamplingFrequency::HZ24000,
        CodecSpecificConfigurationLtv::FrameDuration::US10000,
    };
  }
  return requirement;
}

bool LeAudioOffloadAudioProvider::isSubgroupConfigurationMatchedContext(
    AudioContext requirement_context,
    IBluetoothAudioProvider::BroadcastQuality quality,
    LeAudioBroadcastSubgroupConfiguration configuration) {
  // Find any valid context metadata in the bisConfigurations
  // assuming the bis configuration in the same bis subgroup
  // will have the same context metadata
  std::optional<AudioContext> config_context = std::nullopt;

  auto codec_requirement =
      getCodecRequirementBasedOnContext(requirement_context.bitmask, quality);
  std::map<CodecSpecificConfigurationLtv::Tag, CodecSpecificConfigurationLtv>
      req_tag_map;
  for (auto x : codec_requirement) req_tag_map[x.getTag()] = x;

  for (auto& bis_cfg : configuration.bisConfigurations) {
    // Check every sub_bis_cfg to see which match
    for (auto& x : bis_cfg.bisConfiguration.codecConfiguration) {
      auto p = req_tag_map.find(x.getTag());
      if (p == req_tag_map.end()) continue;
      if (p->second != x) {
        LOG(WARNING) << __func__ << ": does not match for context "
                     << requirement_context.toString()
                     << ", cfg = " << x.toString();
        return false;
      }
    }
  }
  return true;
}

ndk::ScopedAStatus
LeAudioOffloadAudioProvider::getLeAudioBroadcastConfiguration(
    const std::optional<std::vector<
        std::optional<IBluetoothAudioProvider::LeAudioDeviceCapabilities>>>&
        in_remoteSinkAudioCapabilities,
    const IBluetoothAudioProvider::LeAudioBroadcastConfigurationRequirement&
        in_requirement,
    LeAudioBroadcastConfigurationSetting* _aidl_return) {
  if (in_requirement.subgroupConfigurationRequirements.empty()) {
    LOG(WARNING) << __func__ << ": Empty requirement";
    return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
  }

  // Broadcast setting are from provider info
  // We will allow empty capability input, match all settings with requirements.
  getBroadcastSettings();
  std::vector<LeAudioBroadcastConfigurationSetting> filtered_settings;
  if (!in_remoteSinkAudioCapabilities.has_value() ||
      in_remoteSinkAudioCapabilities.value().empty()) {
    LOG(INFO) << __func__ << ": Empty capability, get all broadcast settings";
    filtered_settings = broadcast_settings;
  } else {
    for (auto& setting : broadcast_settings) {
      for (auto& capability : in_remoteSinkAudioCapabilities.value()) {
        if (!capability.has_value()) continue;
        auto filtered_setting =
            getCapabilitiesMatchedBroadcastConfigurationSettings(
                setting, capability.value());
        if (filtered_setting.has_value())
          filtered_settings.push_back(filtered_setting.value());
      }
    }
  }

  if (filtered_settings.empty()) {
    LOG(WARNING) << __func__ << ": Cannot match any remote capability";
    return ndk::ScopedAStatus::ok();
  }

  if (in_requirement.subgroupConfigurationRequirements.empty()) {
    LOG(INFO) << __func__ << ": Empty requirement";
    *_aidl_return = filtered_settings[0];
    return ndk::ScopedAStatus::ok();
  }

  // For each subgroup config requirement, find a suitable subgroup config.
  // Gather these suitable subgroup config in an array.
  // If the setting can satisfy all requirement, we can return the setting
  // with the filtered array.

  auto context_bitmask =
      in_requirement.subgroupConfigurationRequirements[0].audioContext.bitmask;
  auto quality = in_requirement.subgroupConfigurationRequirements[0].quality;
  LeAudioBroadcastConfigurationSetting return_setting =
      getDefaultBroadcastSetting(context_bitmask, quality);
  // Default setting
  return_setting.numBis = 0;
  return_setting.subgroupsConfigurations = {};

  LeAudioDataPathConfiguration path;
  path.isoDataPathConfiguration.isTransparent = true;
  path.dataPathId = kIsoDataPathPlatformDefault;

  // Each subreq, find a setting that match
  for (auto& sub_req : in_requirement.subgroupConfigurationRequirements) {
    bool is_setting_matched = false;
    for (auto setting : filtered_settings) {
      bool is_matched = true;
      // Check if every sub BIS config satisfy
      for (auto& sub_group_config : setting.subgroupsConfigurations) {
        if (!isSubgroupConfigurationMatchedContext(
                sub_req.audioContext, sub_req.quality, sub_group_config)) {
          is_matched = false;
          break;
        }
        path.isoDataPathConfiguration.codecId =
            sub_group_config.bisConfigurations[0].bisConfiguration.codecId;
        // Also modify the subgroup config to match the context
        modifySubgroupConfiguration(sub_group_config, context_bitmask);
      }

      if (is_matched) {
        is_setting_matched = true;
        for (auto& sub_group_config : setting.subgroupsConfigurations)
          return_setting.subgroupsConfigurations.push_back(sub_group_config);
        break;
      }
    }

    if (!is_setting_matched) {
      LOG(WARNING) << __func__
                   << ": Cannot find a setting that match requirement "
                   << sub_req.toString();
      return ndk::ScopedAStatus::ok();
    }
  }

  // Populate all numBis
  for (auto& sub_group_config : return_setting.subgroupsConfigurations) {
    for (auto& sub_bis_config : sub_group_config.bisConfigurations) {
      return_setting.numBis += sub_bis_config.numBis;
    }
  }
  return_setting.phy = std::vector<Phy>(return_setting.numBis, Phy::TWO_M);
  // Populate data path config
  return_setting.dataPathConfiguration = path;
  // TODO: Workaround for STEREO configs maxSduOctets being doubled
  if (context_bitmask & (AudioContext::LIVE_AUDIO | AudioContext::GAME |
                         AudioContext::SOUND_EFFECTS |
                         AudioContext::UNSPECIFIED | AudioContext::MEDIA)) {
    return_setting.maxSduOctets /= 2;
  }
  LOG(INFO) << __func__
            << ": Combined setting that match: " << return_setting.toString();
  *_aidl_return = return_setting;
  return ndk::ScopedAStatus::ok();
};

}  // namespace audio
}  // namespace bluetooth
}  // namespace hardware
}  // namespace android
}  // namespace aidl
