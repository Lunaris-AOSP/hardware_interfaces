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

#define LOG_TAG "ExtCamDevSsn"
// #define LOG_NDEBUG 0
#include <log/log.h>

#include "ExternalCameraDeviceSession.h"

#include <Exif.h>
#include <ExternalCameraOfflineSession.h>
#include <aidl/android/hardware/camera/device/CameraBlob.h>
#include <aidl/android/hardware/camera/device/CameraBlobId.h>
#include <aidl/android/hardware/camera/device/ErrorMsg.h>
#include <aidl/android/hardware/camera/device/ShutterMsg.h>
#include <aidl/android/hardware/camera/device/StreamBufferRet.h>
#include <aidl/android/hardware/camera/device/StreamBuffersVal.h>
#include <aidl/android/hardware/camera/device/StreamConfigurationMode.h>
#include <aidl/android/hardware/camera/device/StreamRotation.h>
#include <aidl/android/hardware/camera/device/StreamType.h>
#include <aidl/android/hardware/graphics/common/Dataspace.h>
#include <aidlcommonsupport/NativeHandle.h>
#include <convert.h>
#include <linux/videodev2.h>
#include <sync/sync.h>
#include <utils/Trace.h>
#include <deque>

#define HAVE_JPEG  // required for libyuv.h to export MJPEG decode APIs
#include <libyuv.h>
#include <libyuv/convert.h>

namespace android {
namespace hardware {
namespace camera {
namespace device {
namespace implementation {

namespace {

// Size of request/result metadata fast message queue. Change to 0 to always use hwbinder buffer.
static constexpr size_t kMetadataMsgQueueSize = 1 << 18 /* 256kB */;

const int kBadFramesAfterStreamOn = 1;  // drop x frames after streamOn to get rid of some initial
                                        // bad frames. TODO: develop a better bad frame detection
                                        // method
constexpr int MAX_RETRY = 15;  // Allow retry some ioctl failures a few times to account for some
                               // webcam showing temporarily ioctl failures.
constexpr int IOCTL_RETRY_SLEEP_US = 33000;  // 33ms * MAX_RETRY = 0.5 seconds

// Constants for tryLock during dumpstate
static constexpr int kDumpLockRetries = 50;
static constexpr int kDumpLockSleep = 60000;

bool tryLock(Mutex& mutex) {
    bool locked = false;
    for (int i = 0; i < kDumpLockRetries; ++i) {
        if (mutex.tryLock() == NO_ERROR) {
            locked = true;
            break;
        }
        usleep(kDumpLockSleep);
    }
    return locked;
}

bool tryLock(std::mutex& mutex) {
    bool locked = false;
    for (int i = 0; i < kDumpLockRetries; ++i) {
        if (mutex.try_lock()) {
            locked = true;
            break;
        }
        usleep(kDumpLockSleep);
    }
    return locked;
}

}  // anonymous namespace

using ::aidl::android::hardware::camera::device::BufferRequestStatus;
using ::aidl::android::hardware::camera::device::CameraBlob;
using ::aidl::android::hardware::camera::device::CameraBlobId;
using ::aidl::android::hardware::camera::device::ErrorMsg;
using ::aidl::android::hardware::camera::device::ShutterMsg;
using ::aidl::android::hardware::camera::device::StreamBuffer;
using ::aidl::android::hardware::camera::device::StreamBufferRet;
using ::aidl::android::hardware::camera::device::StreamBuffersVal;
using ::aidl::android::hardware::camera::device::StreamConfigurationMode;
using ::aidl::android::hardware::camera::device::StreamRotation;
using ::aidl::android::hardware::camera::device::StreamType;
using ::aidl::android::hardware::graphics::common::Dataspace;
using ::android::hardware::camera::common::V1_0::helper::ExifUtils;

// Static instances
const int ExternalCameraDeviceSession::kMaxProcessedStream;
const int ExternalCameraDeviceSession::kMaxStallStream;
HandleImporter ExternalCameraDeviceSession::sHandleImporter;

ExternalCameraDeviceSession::ExternalCameraDeviceSession(
        const std::shared_ptr<ICameraDeviceCallback>& callback, const ExternalCameraConfig& cfg,
        const std::vector<SupportedV4L2Format>& sortedFormats, const CroppingType& croppingType,
        const common::V1_0::helper::CameraMetadata& chars, const std::string& cameraId,
        unique_fd v4l2Fd)
    : mCallback(callback),
      mCfg(cfg),
      mCameraCharacteristics(chars),
      mSupportedFormats(sortedFormats),
      mCroppingType(croppingType),
      mCameraId(cameraId),
      mV4l2Fd(std::move(v4l2Fd)),
      mMaxThumbResolution(getMaxThumbResolution()),
      mMaxJpegResolution(getMaxJpegResolution()) {}

Size ExternalCameraDeviceSession::getMaxThumbResolution() const {
    return getMaxThumbnailResolution(mCameraCharacteristics);
}

Size ExternalCameraDeviceSession::getMaxJpegResolution() const {
    Size ret{0, 0};
    for (auto& fmt : mSupportedFormats) {
        if (fmt.width * fmt.height > ret.width * ret.height) {
            ret = Size{fmt.width, fmt.height};
        }
    }
    return ret;
}

bool ExternalCameraDeviceSession::initialize() {
    if (mV4l2Fd.get() < 0) {
        ALOGE("%s: invalid v4l2 device fd %d!", __FUNCTION__, mV4l2Fd.get());
        return true;
    }

    struct v4l2_capability capability;
    int ret = ioctl(mV4l2Fd.get(), VIDIOC_QUERYCAP, &capability);
    std::string make, model;
    if (ret < 0) {
        ALOGW("%s v4l2 QUERYCAP failed", __FUNCTION__);
        mExifMake = "Generic UVC webcam";
        mExifModel = "Generic UVC webcam";
    } else {
        // capability.card is UTF-8 encoded
        char card[32];
        int j = 0;
        for (int i = 0; i < 32; i++) {
            if (capability.card[i] < 128) {
                card[j++] = capability.card[i];
            }
            if (capability.card[i] == '\0') {
                break;
            }
        }
        if (j == 0 || card[j - 1] != '\0') {
            mExifMake = "Generic UVC webcam";
            mExifModel = "Generic UVC webcam";
        } else {
            mExifMake = card;
            mExifModel = card;
        }
    }

    initOutputThread();
    if (mOutputThread == nullptr) {
        ALOGE("%s: init OutputThread failed!", __FUNCTION__);
        return true;
    }
    mOutputThread->setExifMakeModel(mExifMake, mExifModel);

    status_t status = initDefaultRequests();
    if (status != OK) {
        ALOGE("%s: init default requests failed!", __FUNCTION__);
        return true;
    }

    mRequestMetadataQueue =
            std::make_unique<RequestMetadataQueue>(kMetadataMsgQueueSize, false /* non blocking */);
    if (!mRequestMetadataQueue->isValid()) {
        ALOGE("%s: invalid request fmq", __FUNCTION__);
        return true;
    }

    mResultMetadataQueue =
            std::make_shared<ResultMetadataQueue>(kMetadataMsgQueueSize, false /* non blocking */);
    if (!mResultMetadataQueue->isValid()) {
        ALOGE("%s: invalid result fmq", __FUNCTION__);
        return true;
    }

    mOutputThread->run();
    return false;
}

bool ExternalCameraDeviceSession::isInitFailed() {
    Mutex::Autolock _l(mLock);
    if (!mInitialized) {
        mInitFail = initialize();
        mInitialized = true;
    }
    return mInitFail;
}

void ExternalCameraDeviceSession::initOutputThread() {
    // Grab a shared_ptr to 'this' from ndk::SharedRefBase::ref()
    std::shared_ptr<ExternalCameraDeviceSession> thiz = ref<ExternalCameraDeviceSession>();

    mBufferRequestThread = std::make_shared<BufferRequestThread>(/*parent=*/thiz, mCallback);
    mBufferRequestThread->run();
    mOutputThread = std::make_shared<OutputThread>(/*parent=*/thiz, mCroppingType,
                                                   mCameraCharacteristics, mBufferRequestThread);
}

void ExternalCameraDeviceSession::closeOutputThread() {
    if (mOutputThread != nullptr) {
        mOutputThread->flush();
        mOutputThread->requestExitAndWait();
        mOutputThread.reset();
    }
}

void ExternalCameraDeviceSession::closeBufferRequestThread() {
    if (mBufferRequestThread != nullptr) {
        mBufferRequestThread->requestExitAndWait();
        mBufferRequestThread.reset();
    }
}

Status ExternalCameraDeviceSession::initStatus() const {
    Mutex::Autolock _l(mLock);
    Status status = Status::OK;
    if (mInitFail || mClosed) {
        ALOGI("%s: session initFailed %d closed %d", __FUNCTION__, mInitFail, mClosed);
        status = Status::INTERNAL_ERROR;
    }
    return status;
}

ExternalCameraDeviceSession::~ExternalCameraDeviceSession() {
    if (!isClosed()) {
        ALOGE("ExternalCameraDeviceSession deleted before close!");
        closeImpl();
    }
}

ScopedAStatus ExternalCameraDeviceSession::constructDefaultRequestSettings(
        RequestTemplate in_type, CameraMetadata* _aidl_return) {
    CameraMetadata emptyMetadata;
    Status status = initStatus();
    if (status != Status::OK) {
        return fromStatus(status);
    }
    switch (in_type) {
        case RequestTemplate::PREVIEW:
        case RequestTemplate::STILL_CAPTURE:
        case RequestTemplate::VIDEO_RECORD:
        case RequestTemplate::VIDEO_SNAPSHOT: {
            *_aidl_return = mDefaultRequests[in_type];
            break;
        }
        case RequestTemplate::MANUAL:
        case RequestTemplate::ZERO_SHUTTER_LAG:
            // Don't support MANUAL, ZSL templates
            status = Status::ILLEGAL_ARGUMENT;
            break;
        default:
            ALOGE("%s: unknown request template type %d", __FUNCTION__, static_cast<int>(in_type));
            status = Status::ILLEGAL_ARGUMENT;
            break;
    }
    return fromStatus(status);
}

ScopedAStatus ExternalCameraDeviceSession::configureStreams(
        const StreamConfiguration& in_requestedConfiguration,
        std::vector<HalStream>* _aidl_return) {
    uint32_t blobBufferSize = 0;
    _aidl_return->clear();
    Mutex::Autolock _il(mInterfaceLock);

    Status status =
            isStreamCombinationSupported(in_requestedConfiguration, mSupportedFormats, mCfg);
    if (status != Status::OK) {
        return fromStatus(status);
    }

    status = initStatus();
    if (status != Status::OK) {
        return fromStatus(status);
    }

    {
        std::lock_guard<std::mutex> lk(mInflightFramesLock);
        if (!mInflightFrames.empty()) {
            ALOGE("%s: trying to configureStreams while there are still %zu inflight frames!",
                  __FUNCTION__, mInflightFrames.size());
            return fromStatus(Status::INTERNAL_ERROR);
        }
    }

    Mutex::Autolock _l(mLock);
    {
        Mutex::Autolock _cl(mCbsLock);
        // Add new streams
        for (const auto& stream : in_requestedConfiguration.streams) {
            if (mStreamMap.count(stream.id) == 0) {
                mStreamMap[stream.id] = stream;
                mCirculatingBuffers.emplace(stream.id, CirculatingBuffers{});
            }
        }

        // Cleanup removed streams
        for (auto it = mStreamMap.begin(); it != mStreamMap.end();) {
            int id = it->first;
            bool found = false;
            for (const auto& stream : in_requestedConfiguration.streams) {
                if (id == stream.id) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                // Unmap all buffers of deleted stream
                cleanupBuffersLocked(id);
                it = mStreamMap.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Now select a V4L2 format to produce all output streams
    float desiredAr = (mCroppingType == VERTICAL) ? kMaxAspectRatio : kMinAspectRatio;
    uint32_t maxDim = 0;
    for (const auto& stream : in_requestedConfiguration.streams) {
        float aspectRatio = ASPECT_RATIO(stream);
        ALOGI("%s: request stream %dx%d", __FUNCTION__, stream.width, stream.height);
        if ((mCroppingType == VERTICAL && aspectRatio < desiredAr) ||
            (mCroppingType == HORIZONTAL && aspectRatio > desiredAr)) {
            desiredAr = aspectRatio;
        }

        // The dimension that's not cropped
        uint32_t dim = (mCroppingType == VERTICAL) ? stream.width : stream.height;
        if (dim > maxDim) {
            maxDim = dim;
        }
    }

    // Find the smallest format that matches the desired aspect ratio and is wide/high enough
    SupportedV4L2Format v4l2Fmt{.width = 0, .height = 0};
    for (const auto& fmt : mSupportedFormats) {
        uint32_t dim = (mCroppingType == VERTICAL) ? fmt.width : fmt.height;
        if (dim >= maxDim) {
            float aspectRatio = ASPECT_RATIO(fmt);
            if (isAspectRatioClose(aspectRatio, desiredAr)) {
                v4l2Fmt = fmt;
                // since mSupportedFormats is sorted by width then height, the first matching fmt
                // will be the smallest one with matching aspect ratio
                break;
            }
        }
    }

    if (v4l2Fmt.width == 0) {
        // Cannot find exact good aspect ratio candidate, try to find a close one
        for (const auto& fmt : mSupportedFormats) {
            uint32_t dim = (mCroppingType == VERTICAL) ? fmt.width : fmt.height;
            if (dim >= maxDim) {
                float aspectRatio = ASPECT_RATIO(fmt);
                if ((mCroppingType == VERTICAL && aspectRatio < desiredAr) ||
                    (mCroppingType == HORIZONTAL && aspectRatio > desiredAr)) {
                    v4l2Fmt = fmt;
                    break;
                }
            }
        }
    }

    if (v4l2Fmt.width == 0) {
        ALOGE("%s: unable to find a resolution matching (%s at least %d, aspect ratio %f)",
              __FUNCTION__, (mCroppingType == VERTICAL) ? "width" : "height", maxDim, desiredAr);
        return fromStatus(Status::ILLEGAL_ARGUMENT);
    }

    if (configureV4l2StreamLocked(v4l2Fmt) != 0) {
        ALOGE("V4L configuration failed!, format:%c%c%c%c, w %d, h %d", v4l2Fmt.fourcc & 0xFF,
              (v4l2Fmt.fourcc >> 8) & 0xFF, (v4l2Fmt.fourcc >> 16) & 0xFF,
              (v4l2Fmt.fourcc >> 24) & 0xFF, v4l2Fmt.width, v4l2Fmt.height);
        return fromStatus(Status::INTERNAL_ERROR);
    }

    Size v4lSize = {v4l2Fmt.width, v4l2Fmt.height};
    Size thumbSize{0, 0};
    camera_metadata_ro_entry entry =
            mCameraCharacteristics.find(ANDROID_JPEG_AVAILABLE_THUMBNAIL_SIZES);
    for (uint32_t i = 0; i < entry.count; i += 2) {
        Size sz{entry.data.i32[i], entry.data.i32[i + 1]};
        if (sz.width * sz.height > thumbSize.width * thumbSize.height) {
            thumbSize = sz;
        }
    }

    if (thumbSize.width * thumbSize.height == 0) {
        ALOGE("%s: non-zero thumbnail size not available", __FUNCTION__);
        return fromStatus(Status::INTERNAL_ERROR);
    }

    mBlobBufferSize = blobBufferSize;
    status = mOutputThread->allocateIntermediateBuffers(
            v4lSize, mMaxThumbResolution, in_requestedConfiguration.streams, blobBufferSize);
    if (status != Status::OK) {
        ALOGE("%s: allocating intermediate buffers failed!", __FUNCTION__);
        return fromStatus(status);
    }

    std::vector<HalStream>& out = *_aidl_return;
    out.resize(in_requestedConfiguration.streams.size());
    for (size_t i = 0; i < in_requestedConfiguration.streams.size(); i++) {
        out[i].overrideDataSpace = in_requestedConfiguration.streams[i].dataSpace;
        out[i].id = in_requestedConfiguration.streams[i].id;
        // TODO: double check should we add those CAMERA flags
        mStreamMap[in_requestedConfiguration.streams[i].id].usage = out[i].producerUsage =
                static_cast<BufferUsage>(((int64_t)in_requestedConfiguration.streams[i].usage) |
                                         ((int64_t)BufferUsage::CPU_WRITE_OFTEN) |
                                         ((int64_t)BufferUsage::CAMERA_OUTPUT));
        out[i].consumerUsage = static_cast<BufferUsage>(0);
        out[i].maxBuffers = static_cast<int32_t>(mV4L2BufferCount);

        switch (in_requestedConfiguration.streams[i].format) {
            case PixelFormat::BLOB:
            case PixelFormat::YCBCR_420_888:
            case PixelFormat::YV12:  // Used by SurfaceTexture
            case PixelFormat::Y16:
                // No override
                out[i].overrideFormat = in_requestedConfiguration.streams[i].format;
                break;
            case PixelFormat::IMPLEMENTATION_DEFINED:
                // Implementation Defined
                // This should look at the Stream's dataspace flag to determine the format or leave
                // it as is if the rest of the system knows how to handle a private format. To keep
                // this HAL generic, this is being overridden to YUV420
                out[i].overrideFormat = PixelFormat::YCBCR_420_888;
                // Save overridden format in mStreamMap
                mStreamMap[in_requestedConfiguration.streams[i].id].format = out[i].overrideFormat;
                break;
            default:
                ALOGE("%s: unsupported format 0x%x", __FUNCTION__,
                      in_requestedConfiguration.streams[i].format);
                return fromStatus(Status::ILLEGAL_ARGUMENT);
        }
    }

    mFirstRequest = true;
    mLastStreamConfigCounter = in_requestedConfiguration.streamConfigCounter;
    return fromStatus(Status::OK);
}

ScopedAStatus ExternalCameraDeviceSession::flush() {
    ATRACE_CALL();
    Mutex::Autolock _il(mInterfaceLock);
    Status status = initStatus();
    if (status != Status::OK) {
        return fromStatus(status);
    }
    mOutputThread->flush();
    return fromStatus(Status::OK);
}

ScopedAStatus ExternalCameraDeviceSession::getCaptureRequestMetadataQueue(
        MQDescriptor<int8_t, SynchronizedReadWrite>* _aidl_return) {
    Mutex::Autolock _il(mInterfaceLock);
    *_aidl_return = mRequestMetadataQueue->dupeDesc();
    return fromStatus(Status::OK);
}

ScopedAStatus ExternalCameraDeviceSession::getCaptureResultMetadataQueue(
        MQDescriptor<int8_t, SynchronizedReadWrite>* _aidl_return) {
    Mutex::Autolock _il(mInterfaceLock);
    *_aidl_return = mResultMetadataQueue->dupeDesc();
    return fromStatus(Status::OK);
}

ScopedAStatus ExternalCameraDeviceSession::isReconfigurationRequired(
        const CameraMetadata& in_oldSessionParams, const CameraMetadata& in_newSessionParams,
        bool* _aidl_return) {
    // reconfiguration required if there is any change in the session params
    *_aidl_return = in_oldSessionParams != in_newSessionParams;
    return fromStatus(Status::OK);
}

ScopedAStatus ExternalCameraDeviceSession::processCaptureRequest(
        const std::vector<CaptureRequest>& in_requests,
        const std::vector<BufferCache>& in_cachesToRemove, int32_t* _aidl_return) {
    Mutex::Autolock _il(mInterfaceLock);
    updateBufferCaches(in_cachesToRemove);

    int32_t& numRequestProcessed = *_aidl_return;
    numRequestProcessed = 0;
    Status s = Status::OK;
    for (size_t i = 0; i < in_requests.size(); i++, numRequestProcessed++) {
        s = processOneCaptureRequest(in_requests[i]);
        if (s != Status::OK) {
            break;
        }
    }

    return fromStatus(s);
}

Status ExternalCameraDeviceSession::processOneCaptureRequest(const CaptureRequest& request) {
    ATRACE_CALL();
    Status status = initStatus();
    if (status != Status::OK) {
        return status;
    }

    if (request.inputBuffer.streamId != -1) {
        ALOGE("%s: external camera does not support reprocessing!", __FUNCTION__);
        return Status::ILLEGAL_ARGUMENT;
    }

    Mutex::Autolock _l(mLock);
    if (!mV4l2Streaming) {
        ALOGE("%s: cannot process request in streamOff state!", __FUNCTION__);
        return Status::INTERNAL_ERROR;
    }

    if (request.outputBuffers.empty()) {
        ALOGE("%s: No output buffers provided.", __FUNCTION__);
        return Status::ILLEGAL_ARGUMENT;
    }

    for (auto& outputBuf : request.outputBuffers) {
        if (outputBuf.streamId == -1 || mStreamMap.find(outputBuf.streamId) == mStreamMap.end()) {
            ALOGE("%s: Invalid streamId in CaptureRequest.outputBuffers: %d", __FUNCTION__,
                  outputBuf.streamId);
            return Status::ILLEGAL_ARGUMENT;
        }
    }

    const camera_metadata_t* rawSettings = nullptr;
    bool converted;
    CameraMetadata settingsFmq;  // settings from FMQ

    if (request.fmqSettingsSize > 0) {
        // non-blocking read; client must write metadata before calling
        // processOneCaptureRequest
        settingsFmq.metadata.resize(request.fmqSettingsSize);
        bool read = mRequestMetadataQueue->read(
                reinterpret_cast<int8_t*>(settingsFmq.metadata.data()), request.fmqSettingsSize);
        if (read) {
            converted = convertFromAidl(settingsFmq, &rawSettings);
        } else {
            ALOGE("%s: capture request settings metadata couldn't be read from fmq!", __FUNCTION__);
            converted = false;
        }
    } else {
        converted = convertFromAidl(request.settings, &rawSettings);
    }

    if (converted && rawSettings != nullptr) {
        mLatestReqSetting = rawSettings;
    }

    if (!converted) {
        ALOGE("%s: capture request settings metadata is corrupt!", __FUNCTION__);
        return Status::ILLEGAL_ARGUMENT;
    }

    if (mFirstRequest && rawSettings == nullptr) {
        ALOGE("%s: capture request settings must not be null for first request!", __FUNCTION__);
        return Status::ILLEGAL_ARGUMENT;
    }

    size_t numOutputBufs = request.outputBuffers.size();

    if (numOutputBufs == 0) {
        ALOGE("%s: capture request must have at least one output buffer!", __FUNCTION__);
        return Status::ILLEGAL_ARGUMENT;
    }

    camera_metadata_entry fpsRange = mLatestReqSetting.find(ANDROID_CONTROL_AE_TARGET_FPS_RANGE);
    if (fpsRange.count == 2) {
        double requestFpsMax = fpsRange.data.i32[1];
        double closestFps = 0.0;
        double fpsError = 1000.0;
        bool fpsSupported = false;
        for (const auto& fr : mV4l2StreamingFmt.frameRates) {
            double f = fr.getFramesPerSecond();
            if (std::fabs(requestFpsMax - f) < 1.0) {
                fpsSupported = true;
                break;
            }
            if (std::fabs(requestFpsMax - f) < fpsError) {
                fpsError = std::fabs(requestFpsMax - f);
                closestFps = f;
            }
        }
        if (!fpsSupported) {
            /* This can happen in a few scenarios:
             * 1. The application is sending an FPS range not supported by the configured outputs.
             * 2. The application is sending a valid FPS range for all configured outputs, but
             *    the selected V4L2 size can only run at slower speed. This should be very rare
             *    though: for this to happen a sensor needs to support at least 3 different aspect
             *    ratio outputs, and when (at least) two outputs are both not the main aspect ratio
             *    of the webcam, a third size that's larger might be picked and runs into this
             *    issue.
             */
            ALOGW("%s: cannot reach fps %d! Will do %f instead", __FUNCTION__, fpsRange.data.i32[1],
                  closestFps);
            requestFpsMax = closestFps;
        }

        if (requestFpsMax != mV4l2StreamingFps) {
            {
                std::unique_lock<std::mutex> lk(mV4l2BufferLock);
                while (mNumDequeuedV4l2Buffers != 0) {
                    // Wait until pipeline is idle before reconfigure stream
                    int waitRet = waitForV4L2BufferReturnLocked(lk);
                    if (waitRet != 0) {
                        ALOGE("%s: wait for pipeline idle failed!", __FUNCTION__);
                        return Status::INTERNAL_ERROR;
                    }
                }
            }
            configureV4l2StreamLocked(mV4l2StreamingFmt, requestFpsMax);
        }
    }

    nsecs_t shutterTs = 0;
    std::unique_ptr<V4L2Frame> frameIn = dequeueV4l2FrameLocked(&shutterTs);
    if (frameIn == nullptr) {
        ALOGE("%s: V4L2 deque frame failed!", __FUNCTION__);
        return Status::INTERNAL_ERROR;
    }

    std::shared_ptr<HalRequest> halReq = std::make_shared<HalRequest>();
    halReq->frameNumber = request.frameNumber;
    halReq->setting = mLatestReqSetting;
    halReq->frameIn = std::move(frameIn);
    halReq->shutterTs = shutterTs;
    halReq->buffers.resize(numOutputBufs);
    for (size_t i = 0; i < numOutputBufs; i++) {
        HalStreamBuffer& halBuf = halReq->buffers[i];
        int streamId = halBuf.streamId = request.outputBuffers[i].streamId;
        halBuf.bufferId = request.outputBuffers[i].bufferId;
        const Stream& stream = mStreamMap[streamId];
        halBuf.width = stream.width;
        halBuf.height = stream.height;
        halBuf.format = stream.format;
        halBuf.usage = stream.usage;
        halBuf.bufPtr = nullptr;  // threadloop will request buffer from cameraservice
        halBuf.acquireFence = 0;  // threadloop will request fence from cameraservice
        halBuf.fenceTimeout = false;
    }
    {
        std::lock_guard<std::mutex> lk(mInflightFramesLock);
        mInflightFrames.insert(halReq->frameNumber);
    }
    // Send request to OutputThread for the rest of processing
    mOutputThread->submitRequest(halReq);
    mFirstRequest = false;
    return Status::OK;
}

ScopedAStatus ExternalCameraDeviceSession::signalStreamFlush(
        const std::vector<int32_t>& /*in_streamIds*/, int32_t in_streamConfigCounter) {
    {
        Mutex::Autolock _l(mLock);
        if (in_streamConfigCounter < mLastStreamConfigCounter) {
            // stale call. new streams have been configured since this call was issued.
            // Do nothing.
            return fromStatus(Status::OK);
        }
    }

    // TODO: implement if needed.
    return fromStatus(Status::OK);
}

ScopedAStatus ExternalCameraDeviceSession::switchToOffline(
        const std::vector<int32_t>& in_streamsToKeep,
        CameraOfflineSessionInfo* out_offlineSessionInfo,
        std::shared_ptr<ICameraOfflineSession>* _aidl_return) {
    std::vector<NotifyMsg> msgs;
    std::vector<CaptureResult> results;
    CameraOfflineSessionInfo info;
    std::shared_ptr<ICameraOfflineSession> session;
    Status st = switchToOffline(in_streamsToKeep, &msgs, &results, &info, &session);

    mCallback->notify(msgs);
    invokeProcessCaptureResultCallback(results, /* tryWriteFmq= */ true);
    freeReleaseFences(results);

    // setup return values
    *out_offlineSessionInfo = info;
    *_aidl_return = session;
    return fromStatus(st);
}

Status ExternalCameraDeviceSession::switchToOffline(
        const std::vector<int32_t>& offlineStreams, std::vector<NotifyMsg>* msgs,
        std::vector<CaptureResult>* results, CameraOfflineSessionInfo* info,
        std::shared_ptr<ICameraOfflineSession>* session) {
    ATRACE_CALL();
    if (offlineStreams.size() > 1) {
        ALOGE("%s: more than one offline stream is not supported", __FUNCTION__);
        return Status::ILLEGAL_ARGUMENT;
    }

    if (msgs == nullptr || results == nullptr || info == nullptr || session == nullptr) {
        ALOGE("%s, output arguments (%p, %p, %p, %p) must not be null", __FUNCTION__, msgs, results,
              info, session);
    }

    Mutex::Autolock _il(mInterfaceLock);
    Status status = initStatus();
    if (status != Status::OK) {
        return status;
    }

    Mutex::Autolock _l(mLock);
    for (auto streamId : offlineStreams) {
        if (!supportOfflineLocked(streamId)) {
            return Status::ILLEGAL_ARGUMENT;
        }
    }

    // pause output thread and get all remaining inflight requests
    auto remainingReqs = mOutputThread->switchToOffline();
    std::vector<std::shared_ptr<HalRequest>> halReqs;

    // Send out buffer/request error for remaining requests and filter requests
    // to be handled in offline mode
    for (auto& halReq : remainingReqs) {
        bool dropReq = canDropRequest(offlineStreams, halReq);
        if (dropReq) {
            // Request is dropped completely. Just send request error and
            // there is no need to send the request to offline session
            processCaptureRequestError(halReq, msgs, results);
            continue;
        }

        // All requests reach here must have at least one offline stream output
        NotifyMsg shutter;
        aidl::android::hardware::camera::device::ShutterMsg shutterMsg = {
                .frameNumber = static_cast<int32_t>(halReq->frameNumber),
                .timestamp = halReq->shutterTs};
        shutter.set<NotifyMsg::Tag::shutter>(shutterMsg);
        msgs->push_back(shutter);

        std::vector<HalStreamBuffer> offlineBuffers;
        for (const auto& buffer : halReq->buffers) {
            bool dropBuffer = true;
            for (auto offlineStreamId : offlineStreams) {
                if (buffer.streamId == offlineStreamId) {
                    dropBuffer = false;
                    break;
                }
            }
            if (dropBuffer) {
                aidl::android::hardware::camera::device::ErrorMsg errorMsg = {
                        .frameNumber = static_cast<int32_t>(halReq->frameNumber),
                        .errorStreamId = buffer.streamId,
                        .errorCode = ErrorCode::ERROR_BUFFER};

                NotifyMsg error;
                error.set<NotifyMsg::Tag::error>(errorMsg);
                msgs->push_back(error);

                results->push_back({
                        .frameNumber = static_cast<int32_t>(halReq->frameNumber),
                        .outputBuffers = {},
                        .inputBuffer = {.streamId = -1},
                        .partialResult = 0,  // buffer only result
                });

                CaptureResult& result = results->back();
                result.outputBuffers.resize(1);
                StreamBuffer& outputBuffer = result.outputBuffers[0];
                outputBuffer.streamId = buffer.streamId;
                outputBuffer.bufferId = buffer.bufferId;
                outputBuffer.status = BufferStatus::ERROR;
                if (buffer.acquireFence >= 0) {
                    native_handle_t* handle = native_handle_create(/*numFds*/ 1, /*numInts*/ 0);
                    handle->data[0] = buffer.acquireFence;
                    outputBuffer.releaseFence = android::dupToAidl(handle);
                    native_handle_delete(handle);
                }
            } else {
                offlineBuffers.push_back(buffer);
            }
        }
        halReq->buffers = offlineBuffers;
        halReqs.push_back(halReq);
    }

    // convert hal requests to offline request
    std::deque<std::shared_ptr<HalRequest>> offlineReqs(halReqs.size());
    size_t i = 0;
    for (auto& v4lReq : halReqs) {
        offlineReqs[i] = std::make_shared<HalRequest>();
        offlineReqs[i]->frameNumber = v4lReq->frameNumber;
        offlineReqs[i]->setting = v4lReq->setting;
        offlineReqs[i]->shutterTs = v4lReq->shutterTs;
        offlineReqs[i]->buffers = v4lReq->buffers;
        std::shared_ptr<V4L2Frame> v4l2Frame(static_cast<V4L2Frame*>(v4lReq->frameIn.get()));
        offlineReqs[i]->frameIn = std::make_shared<AllocatedV4L2Frame>(v4l2Frame);
        i++;
        // enqueue V4L2 frame
        enqueueV4l2Frame(v4l2Frame);
    }

    // Collect buffer caches/streams
    std::vector<Stream> streamInfos(offlineStreams.size());
    std::map<int, CirculatingBuffers> circulatingBuffers;
    {
        Mutex::Autolock _cbsl(mCbsLock);
        for (auto streamId : offlineStreams) {
            circulatingBuffers[streamId] = mCirculatingBuffers.at(streamId);
            mCirculatingBuffers.erase(streamId);
            streamInfos.push_back(mStreamMap.at(streamId));
            mStreamMap.erase(streamId);
        }
    }

    fillOfflineSessionInfo(offlineStreams, offlineReqs, circulatingBuffers, info);
    // create the offline session object
    bool afTrigger;
    {
        std::lock_guard<std::mutex> _lk(mAfTriggerLock);
        afTrigger = mAfTrigger;
    }

    std::shared_ptr<ExternalCameraOfflineSession> sessionImpl =
            ndk::SharedRefBase::make<ExternalCameraOfflineSession>(
                    mCroppingType, mCameraCharacteristics, mCameraId, mExifMake, mExifModel,
                    mBlobBufferSize, afTrigger, streamInfos, offlineReqs, circulatingBuffers);

    bool initFailed = sessionImpl->initialize();
    if (initFailed) {
        ALOGE("%s: offline session initialize failed!", __FUNCTION__);
        return Status::INTERNAL_ERROR;
    }

    // cleanup stream and buffer caches
    {
        Mutex::Autolock _cbsl(mCbsLock);
        for (auto pair : mStreamMap) {
            cleanupBuffersLocked(/*Stream ID*/ pair.first);
        }
        mCirculatingBuffers.clear();
    }
    mStreamMap.clear();

    // update inflight records
    {
        std::lock_guard<std::mutex> _lk(mInflightFramesLock);
        mInflightFrames.clear();
    }

    // stop v4l2 streaming
    if (v4l2StreamOffLocked() != 0) {
        ALOGE("%s: stop V4L2 streaming failed!", __FUNCTION__);
        return Status::INTERNAL_ERROR;
    }

    // No need to return session if there is no offline requests left
    if (!offlineReqs.empty()) {
        *session = sessionImpl;
    } else {
        *session = nullptr;
    }

    return Status::OK;
}

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))
#define UPDATE(md, tag, data, size)               \
    do {                                          \
        if ((md).update((tag), (data), (size))) { \
            ALOGE("Update " #tag " failed!");     \
            return BAD_VALUE;                     \
        }                                         \
    } while (0)

status_t ExternalCameraDeviceSession::initDefaultRequests() {
    common::V1_0::helper::CameraMetadata md;

    const uint8_t aberrationMode = ANDROID_COLOR_CORRECTION_ABERRATION_MODE_OFF;
    UPDATE(md, ANDROID_COLOR_CORRECTION_ABERRATION_MODE, &aberrationMode, 1);

    const int32_t exposureCompensation = 0;
    UPDATE(md, ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION, &exposureCompensation, 1);

    const uint8_t videoStabilizationMode = ANDROID_CONTROL_VIDEO_STABILIZATION_MODE_OFF;
    UPDATE(md, ANDROID_CONTROL_VIDEO_STABILIZATION_MODE, &videoStabilizationMode, 1);

    const uint8_t awbMode = ANDROID_CONTROL_AWB_MODE_AUTO;
    UPDATE(md, ANDROID_CONTROL_AWB_MODE, &awbMode, 1);

    const uint8_t aeMode = ANDROID_CONTROL_AE_MODE_ON;
    UPDATE(md, ANDROID_CONTROL_AE_MODE, &aeMode, 1);

    const uint8_t aePrecaptureTrigger = ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER_IDLE;
    UPDATE(md, ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER, &aePrecaptureTrigger, 1);

    const uint8_t afMode = ANDROID_CONTROL_AF_MODE_AUTO;
    UPDATE(md, ANDROID_CONTROL_AF_MODE, &afMode, 1);

    const uint8_t afTrigger = ANDROID_CONTROL_AF_TRIGGER_IDLE;
    UPDATE(md, ANDROID_CONTROL_AF_TRIGGER, &afTrigger, 1);

    const uint8_t sceneMode = ANDROID_CONTROL_SCENE_MODE_DISABLED;
    UPDATE(md, ANDROID_CONTROL_SCENE_MODE, &sceneMode, 1);

    const uint8_t effectMode = ANDROID_CONTROL_EFFECT_MODE_OFF;
    UPDATE(md, ANDROID_CONTROL_EFFECT_MODE, &effectMode, 1);

    const uint8_t flashMode = ANDROID_FLASH_MODE_OFF;
    UPDATE(md, ANDROID_FLASH_MODE, &flashMode, 1);

    const int32_t thumbnailSize[] = {240, 180};
    UPDATE(md, ANDROID_JPEG_THUMBNAIL_SIZE, thumbnailSize, 2);

    const uint8_t jpegQuality = 90;
    UPDATE(md, ANDROID_JPEG_QUALITY, &jpegQuality, 1);
    UPDATE(md, ANDROID_JPEG_THUMBNAIL_QUALITY, &jpegQuality, 1);

    const int32_t jpegOrientation = 0;
    UPDATE(md, ANDROID_JPEG_ORIENTATION, &jpegOrientation, 1);

    const uint8_t oisMode = ANDROID_LENS_OPTICAL_STABILIZATION_MODE_OFF;
    UPDATE(md, ANDROID_LENS_OPTICAL_STABILIZATION_MODE, &oisMode, 1);

    const uint8_t nrMode = ANDROID_NOISE_REDUCTION_MODE_OFF;
    UPDATE(md, ANDROID_NOISE_REDUCTION_MODE, &nrMode, 1);

    const int32_t testPatternModes = ANDROID_SENSOR_TEST_PATTERN_MODE_OFF;
    UPDATE(md, ANDROID_SENSOR_TEST_PATTERN_MODE, &testPatternModes, 1);

    const uint8_t fdMode = ANDROID_STATISTICS_FACE_DETECT_MODE_OFF;
    UPDATE(md, ANDROID_STATISTICS_FACE_DETECT_MODE, &fdMode, 1);

    const uint8_t hotpixelMode = ANDROID_STATISTICS_HOT_PIXEL_MAP_MODE_OFF;
    UPDATE(md, ANDROID_STATISTICS_HOT_PIXEL_MAP_MODE, &hotpixelMode, 1);

    bool support30Fps = false;
    int32_t maxFps = std::numeric_limits<int32_t>::min();
    for (const auto& supportedFormat : mSupportedFormats) {
        for (const auto& fr : supportedFormat.frameRates) {
            int32_t framerateInt = static_cast<int32_t>(fr.getFramesPerSecond());
            if (maxFps < framerateInt) {
                maxFps = framerateInt;
            }
            if (framerateInt == 30) {
                support30Fps = true;
                break;
            }
        }
        if (support30Fps) {
            break;
        }
    }

    int32_t defaultFramerate = support30Fps ? 30 : maxFps;
    int32_t defaultFpsRange[] = {defaultFramerate / 2, defaultFramerate};
    UPDATE(md, ANDROID_CONTROL_AE_TARGET_FPS_RANGE, defaultFpsRange, ARRAY_SIZE(defaultFpsRange));

    uint8_t antibandingMode = ANDROID_CONTROL_AE_ANTIBANDING_MODE_AUTO;
    UPDATE(md, ANDROID_CONTROL_AE_ANTIBANDING_MODE, &antibandingMode, 1);

    const uint8_t controlMode = ANDROID_CONTROL_MODE_AUTO;
    UPDATE(md, ANDROID_CONTROL_MODE, &controlMode, 1);

    for (const auto& type : ndk::enum_range<RequestTemplate>()) {
        common::V1_0::helper::CameraMetadata mdCopy = md;
        uint8_t intent = ANDROID_CONTROL_CAPTURE_INTENT_PREVIEW;
        switch (type) {
            case RequestTemplate::PREVIEW:
                intent = ANDROID_CONTROL_CAPTURE_INTENT_PREVIEW;
                break;
            case RequestTemplate::STILL_CAPTURE:
                intent = ANDROID_CONTROL_CAPTURE_INTENT_STILL_CAPTURE;
                break;
            case RequestTemplate::VIDEO_RECORD:
                intent = ANDROID_CONTROL_CAPTURE_INTENT_VIDEO_RECORD;
                break;
            case RequestTemplate::VIDEO_SNAPSHOT:
                intent = ANDROID_CONTROL_CAPTURE_INTENT_VIDEO_SNAPSHOT;
                break;
            default:
                ALOGV("%s: unsupported RequestTemplate type %d", __FUNCTION__, type);
                continue;
        }
        UPDATE(mdCopy, ANDROID_CONTROL_CAPTURE_INTENT, &intent, 1);
        camera_metadata_t* mdPtr = mdCopy.release();
        uint8_t* rawMd = reinterpret_cast<uint8_t*>(mdPtr);
        CameraMetadata aidlMd;
        aidlMd.metadata.assign(rawMd, rawMd + get_camera_metadata_size(mdPtr));
        mDefaultRequests[type] = aidlMd;
        free_camera_metadata(mdPtr);
    }
    return OK;
}

status_t ExternalCameraDeviceSession::fillCaptureResult(common::V1_0::helper::CameraMetadata& md,
                                                        nsecs_t timestamp) {
    bool afTrigger = false;
    {
        std::lock_guard<std::mutex> lk(mAfTriggerLock);
        afTrigger = mAfTrigger;
        if (md.exists(ANDROID_CONTROL_AF_TRIGGER)) {
            camera_metadata_entry entry = md.find(ANDROID_CONTROL_AF_TRIGGER);
            if (entry.data.u8[0] == ANDROID_CONTROL_AF_TRIGGER_START) {
                mAfTrigger = afTrigger = true;
            } else if (entry.data.u8[0] == ANDROID_CONTROL_AF_TRIGGER_CANCEL) {
                mAfTrigger = afTrigger = false;
            }
        }
    }

    // For USB camera, the USB camera handles everything and we don't have control
    // over AF. We only simply fake the AF metadata based on the request
    // received here.
    uint8_t afState;
    if (afTrigger) {
        afState = ANDROID_CONTROL_AF_STATE_FOCUSED_LOCKED;
    } else {
        afState = ANDROID_CONTROL_AF_STATE_INACTIVE;
    }
    UPDATE(md, ANDROID_CONTROL_AF_STATE, &afState, 1);

    camera_metadata_ro_entry activeArraySize =
            mCameraCharacteristics.find(ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE);

    return fillCaptureResultCommon(md, timestamp, activeArraySize);
}

int ExternalCameraDeviceSession::configureV4l2StreamLocked(const SupportedV4L2Format& v4l2Fmt,
                                                           double requestFps) {
    ATRACE_CALL();
    int ret = v4l2StreamOffLocked();
    if (ret != OK) {
        ALOGE("%s: stop v4l2 streaming failed: ret %d", __FUNCTION__, ret);
        return ret;
    }

    // VIDIOC_S_FMT w/h/fmt
    v4l2_format fmt;
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = v4l2Fmt.width;
    fmt.fmt.pix.height = v4l2Fmt.height;
    fmt.fmt.pix.pixelformat = v4l2Fmt.fourcc;

    {
        int numAttempt = 0;
        do {
            ret = TEMP_FAILURE_RETRY(ioctl(mV4l2Fd.get(), VIDIOC_S_FMT, &fmt));
            if (numAttempt == MAX_RETRY) {
                break;
            }
            numAttempt++;
            if (ret < 0) {
                ALOGW("%s: VIDIOC_S_FMT failed, wait 33ms and try again", __FUNCTION__);
                usleep(IOCTL_RETRY_SLEEP_US);  // sleep and try again
            }
        } while (ret < 0);
        if (ret < 0) {
            ALOGE("%s: S_FMT ioctl failed: %s", __FUNCTION__, strerror(errno));
            return -errno;
        }
    }

    if (v4l2Fmt.width != fmt.fmt.pix.width || v4l2Fmt.height != fmt.fmt.pix.height ||
        v4l2Fmt.fourcc != fmt.fmt.pix.pixelformat) {
        ALOGE("%s: S_FMT expect %c%c%c%c %dx%d, got %c%c%c%c %dx%d instead!", __FUNCTION__,
              v4l2Fmt.fourcc & 0xFF, (v4l2Fmt.fourcc >> 8) & 0xFF, (v4l2Fmt.fourcc >> 16) & 0xFF,
              (v4l2Fmt.fourcc >> 24) & 0xFF, v4l2Fmt.width, v4l2Fmt.height,
              fmt.fmt.pix.pixelformat & 0xFF, (fmt.fmt.pix.pixelformat >> 8) & 0xFF,
              (fmt.fmt.pix.pixelformat >> 16) & 0xFF, (fmt.fmt.pix.pixelformat >> 24) & 0xFF,
              fmt.fmt.pix.width, fmt.fmt.pix.height);
        return -EINVAL;
    }

    uint32_t bufferSize = fmt.fmt.pix.sizeimage;
    ALOGI("%s: V4L2 buffer size is %d", __FUNCTION__, bufferSize);
    uint32_t expectedMaxBufferSize = kMaxBytesPerPixel * fmt.fmt.pix.width * fmt.fmt.pix.height;
    if ((bufferSize == 0) || (bufferSize > expectedMaxBufferSize)) {
        ALOGE("%s: V4L2 buffer size: %u looks invalid. Expected maximum size: %u", __FUNCTION__,
              bufferSize, expectedMaxBufferSize);
        return -EINVAL;
    }
    mMaxV4L2BufferSize = bufferSize;

    const double kDefaultFps = 30.0;
    double fps = std::numeric_limits<double>::max();
    if (requestFps != 0.0) {
        fps = requestFps;
    } else {
        double maxFps = -1.0;
        // Try to pick the slowest fps that is at least 30
        for (const auto& fr : v4l2Fmt.frameRates) {
            double f = fr.getFramesPerSecond();
            if (maxFps < f) {
                maxFps = f;
            }
            if (f >= kDefaultFps && f < fps) {
                fps = f;
            }
        }
        // No fps > 30 found, use the highest fps available within supported formats.
        if (fps == std::numeric_limits<double>::max()) {
            fps = maxFps;
        }
    }

    int fpsRet = setV4l2FpsLocked(fps);
    if (fpsRet != 0 && fpsRet != -EINVAL) {
        ALOGE("%s: set fps failed: %s", __FUNCTION__, strerror(fpsRet));
        return fpsRet;
    }

    uint32_t v4lBufferCount = (fps >= kDefaultFps) ? mCfg.numVideoBuffers : mCfg.numStillBuffers;

    // Double the max lag in theory.
    mMaxLagNs = v4lBufferCount * 1000000000LL * 2 / fps;
    ALOGI("%s: set mMaxLagNs to %" PRIu64 " ns, v4lBufferCount %u", __FUNCTION__, mMaxLagNs,
          v4lBufferCount);

    // VIDIOC_REQBUFS: create buffers
    v4l2_requestbuffers req_buffers{};
    req_buffers.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req_buffers.memory = V4L2_MEMORY_MMAP;
    req_buffers.count = v4lBufferCount;
    if (TEMP_FAILURE_RETRY(ioctl(mV4l2Fd.get(), VIDIOC_REQBUFS, &req_buffers)) < 0) {
        ALOGE("%s: VIDIOC_REQBUFS failed: %s", __FUNCTION__, strerror(errno));
        return -errno;
    }

    // Driver can indeed return more buffer if it needs more to operate
    if (req_buffers.count < v4lBufferCount) {
        ALOGE("%s: VIDIOC_REQBUFS expected %d buffers, got %d instead", __FUNCTION__,
              v4lBufferCount, req_buffers.count);
        return NO_MEMORY;
    }

    // VIDIOC_QUERYBUF:  get buffer offset in the V4L2 fd
    // VIDIOC_QBUF: send buffer to driver
    mV4L2BufferCount = req_buffers.count;
    for (uint32_t i = 0; i < req_buffers.count; i++) {
        v4l2_buffer buffer = {
                .index = i, .type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP};

        if (TEMP_FAILURE_RETRY(ioctl(mV4l2Fd.get(), VIDIOC_QUERYBUF, &buffer)) < 0) {
            ALOGE("%s: QUERYBUF %d failed: %s", __FUNCTION__, i, strerror(errno));
            return -errno;
        }

        if (TEMP_FAILURE_RETRY(ioctl(mV4l2Fd.get(), VIDIOC_QBUF, &buffer)) < 0) {
            ALOGE("%s: QBUF %d failed: %s", __FUNCTION__, i, strerror(errno));
            return -errno;
        }
    }

    {
        // VIDIOC_STREAMON: start streaming
        v4l2_buf_type capture_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        int numAttempt = 0;
        do {
            ret = TEMP_FAILURE_RETRY(ioctl(mV4l2Fd.get(), VIDIOC_STREAMON, &capture_type));
            if (numAttempt == MAX_RETRY) {
                break;
            }
            numAttempt++;
            if (ret < 0) {
                ALOGW("%s: VIDIOC_STREAMON failed, wait 33ms and try again", __FUNCTION__);
                usleep(IOCTL_RETRY_SLEEP_US);  // sleep 100 ms and try again
            }
        } while (ret < 0);

        if (ret < 0) {
            ALOGE("%s: VIDIOC_STREAMON ioctl failed: %s", __FUNCTION__, strerror(errno));
            return -errno;
        }
    }

    // Swallow first few frames after streamOn to account for bad frames from some devices
    for (int i = 0; i < kBadFramesAfterStreamOn; i++) {
        v4l2_buffer buffer{};
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;
        if (TEMP_FAILURE_RETRY(ioctl(mV4l2Fd.get(), VIDIOC_DQBUF, &buffer)) < 0) {
            ALOGE("%s: DQBUF fails: %s", __FUNCTION__, strerror(errno));
            return -errno;
        }

        if (TEMP_FAILURE_RETRY(ioctl(mV4l2Fd.get(), VIDIOC_QBUF, &buffer)) < 0) {
            ALOGE("%s: QBUF index %d fails: %s", __FUNCTION__, buffer.index, strerror(errno));
            return -errno;
        }
    }

    ALOGI("%s: start V4L2 streaming %dx%d@%ffps", __FUNCTION__, v4l2Fmt.width, v4l2Fmt.height, fps);
    mV4l2StreamingFmt = v4l2Fmt;
    mV4l2Streaming = true;
    return OK;
}

std::unique_ptr<V4L2Frame> ExternalCameraDeviceSession::dequeueV4l2FrameLocked(nsecs_t* shutterTs) {
    ATRACE_CALL();
    std::unique_ptr<V4L2Frame> ret = nullptr;
    if (shutterTs == nullptr) {
        ALOGE("%s: shutterTs must not be null!", __FUNCTION__);
        return ret;
    }

    {
        std::unique_lock<std::mutex> lk(mV4l2BufferLock);
        if (mNumDequeuedV4l2Buffers == mV4L2BufferCount) {
            int waitRet = waitForV4L2BufferReturnLocked(lk);
            if (waitRet != 0) {
                return ret;
            }
        }
    }

    uint64_t lagNs = 0;
    v4l2_buffer buffer{};
    do {
        ATRACE_BEGIN("VIDIOC_DQBUF");
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;
        if (TEMP_FAILURE_RETRY(ioctl(mV4l2Fd.get(), VIDIOC_DQBUF, &buffer)) < 0) {
            ALOGE("%s: DQBUF fails: %s", __FUNCTION__, strerror(errno));
            return ret;
        }
        ATRACE_END();

        if (buffer.index >= mV4L2BufferCount) {
            ALOGE("%s: Invalid buffer id: %d", __FUNCTION__, buffer.index);
            return ret;
        }

        if (buffer.flags & V4L2_BUF_FLAG_ERROR) {
            ALOGE("%s: v4l2 buf error! buf flag 0x%x", __FUNCTION__, buffer.flags);
            // TODO: try to dequeue again
        }

        if (buffer.bytesused > mMaxV4L2BufferSize) {
            ALOGE("%s: v4l2 buffer bytes used: %u maximum %u", __FUNCTION__, buffer.bytesused,
                  mMaxV4L2BufferSize);
            return ret;
        }

        nsecs_t curTimeNs = systemTime(SYSTEM_TIME_MONOTONIC);

        if (buffer.flags & V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC) {
            // Ideally we should also check for V4L2_BUF_FLAG_TSTAMP_SRC_SOE, but
            // even V4L2_BUF_FLAG_TSTAMP_SRC_EOF is better than capture a timestamp now
            *shutterTs = static_cast<nsecs_t>(buffer.timestamp.tv_sec) * 1000000000LL +
                         buffer.timestamp.tv_usec * 1000LL;
        } else {
            *shutterTs = curTimeNs;
        }

        // The tactic only takes effect on v4l2 buffers with flag V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC.
        // Most USB cameras should have the feature.
        if (curTimeNs < *shutterTs) {
            lagNs = 0;
            ALOGW("%s: should not happen, the monotonic clock has issue, shutterTs is in the "
                  "future, curTimeNs %" PRId64 "  < "
                  "shutterTs %" PRId64 "",
                  __func__, curTimeNs, *shutterTs);
        } else {
            lagNs = curTimeNs - *shutterTs;
        }

        if (lagNs > mMaxLagNs) {
            ALOGI("%s: drop too old buffer, index %d, lag %" PRIu64 " ns > max %" PRIu64 " ns", __FUNCTION__,
                  buffer.index, lagNs, mMaxLagNs);
            int retVal = ioctl(mV4l2Fd.get(), VIDIOC_QBUF, &buffer);
            if (retVal) {
                ALOGE("%s: unexpected VIDIOC_QBUF failed, retVal %d", __FUNCTION__, retVal);
                return ret;
            }
        }
    } while (lagNs > mMaxLagNs);

    {
        std::lock_guard<std::mutex> lk(mV4l2BufferLock);
        mNumDequeuedV4l2Buffers++;
    }

    return std::make_unique<V4L2Frame>(mV4l2StreamingFmt.width, mV4l2StreamingFmt.height,
                                       mV4l2StreamingFmt.fourcc, buffer.index, mV4l2Fd.get(),
                                       buffer.bytesused, buffer.m.offset);
}

void ExternalCameraDeviceSession::enqueueV4l2Frame(const std::shared_ptr<V4L2Frame>& frame) {
    ATRACE_CALL();
    frame->unmap();
    ATRACE_BEGIN("VIDIOC_QBUF");
    v4l2_buffer buffer{};
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer.memory = V4L2_MEMORY_MMAP;
    buffer.index = frame->mBufferIndex;
    if (TEMP_FAILURE_RETRY(ioctl(mV4l2Fd.get(), VIDIOC_QBUF, &buffer)) < 0) {
        ALOGE("%s: QBUF index %d fails: %s", __FUNCTION__, frame->mBufferIndex, strerror(errno));
        return;
    }
    ATRACE_END();

    {
        std::lock_guard<std::mutex> lk(mV4l2BufferLock);
        mNumDequeuedV4l2Buffers--;
    }
    mV4L2BufferReturned.notify_one();
}

bool ExternalCameraDeviceSession::isSupported(
        const Stream& stream, const std::vector<SupportedV4L2Format>& supportedFormats,
        const ExternalCameraConfig& devCfg) {
    Dataspace ds = stream.dataSpace;
    PixelFormat fmt = stream.format;
    uint32_t width = stream.width;
    uint32_t height = stream.height;
    // TODO: check usage flags

    if (stream.streamType != StreamType::OUTPUT) {
        ALOGE("%s: does not support non-output stream type", __FUNCTION__);
        return false;
    }

    if (stream.rotation != StreamRotation::ROTATION_0) {
        ALOGE("%s: does not support stream rotation", __FUNCTION__);
        return false;
    }

    switch (fmt) {
        case PixelFormat::BLOB:
            if (ds != Dataspace::JFIF) {
                ALOGI("%s: BLOB format does not support dataSpace %x", __FUNCTION__, ds);
                return false;
            }
            break;
        case PixelFormat::IMPLEMENTATION_DEFINED:
        case PixelFormat::YCBCR_420_888:
        case PixelFormat::YV12:
            // TODO: check what dataspace we can support here.
            // intentional no-ops.
            break;
        case PixelFormat::Y16:
            if (!devCfg.depthEnabled) {
                ALOGI("%s: Depth is not Enabled", __FUNCTION__);
                return false;
            }
            if (!(static_cast<int32_t>(ds) & static_cast<int32_t>(Dataspace::DEPTH))) {
                ALOGI("%s: Y16 supports only dataSpace DEPTH", __FUNCTION__);
                return false;
            }
            break;
        default:
            ALOGI("%s: does not support format %x", __FUNCTION__, fmt);
            return false;
    }

    // Assume we can convert any V4L2 format to any of supported output format for now, i.e.
    // ignoring v4l2Fmt.fourcc for now. Might need more subtle check if we support more v4l format
    // in the futrue.
    for (const auto& v4l2Fmt : supportedFormats) {
        if (width == v4l2Fmt.width && height == v4l2Fmt.height) {
            return true;
        }
    }
    ALOGI("%s: resolution %dx%d is not supported", __FUNCTION__, width, height);
    return false;
}

Status ExternalCameraDeviceSession::importBuffer(int32_t streamId, uint64_t bufId,
                                                 buffer_handle_t buf,
                                                 /*out*/ buffer_handle_t** outBufPtr) {
    Mutex::Autolock _l(mCbsLock);
    return importBufferLocked(streamId, bufId, buf, outBufPtr);
}

Status ExternalCameraDeviceSession::importBufferLocked(int32_t streamId, uint64_t bufId,
                                                       buffer_handle_t buf,
                                                       buffer_handle_t** outBufPtr) {
    return importBufferImpl(mCirculatingBuffers, sHandleImporter, streamId, bufId, buf, outBufPtr);
}

ScopedAStatus ExternalCameraDeviceSession::close() {
    closeImpl();
    return fromStatus(Status::OK);
}

void ExternalCameraDeviceSession::closeImpl() {
    Mutex::Autolock _il(mInterfaceLock);
    bool closed = isClosed();
    if (!closed) {
        closeOutputThread();
        closeBufferRequestThread();

        Mutex::Autolock _l(mLock);
        // free all buffers
        {
            Mutex::Autolock _cbsl(mCbsLock);
            for (auto pair : mStreamMap) {
                cleanupBuffersLocked(/*Stream ID*/ pair.first);
            }
        }
        v4l2StreamOffLocked();
        ALOGV("%s: closing V4L2 camera FD %d", __FUNCTION__, mV4l2Fd.get());
        mV4l2Fd.reset();
        mClosed = true;
    }
}

bool ExternalCameraDeviceSession::isClosed() {
    Mutex::Autolock _l(mLock);
    return mClosed;
}

ScopedAStatus ExternalCameraDeviceSession::repeatingRequestEnd(
        int32_t /*in_frameNumber*/, const std::vector<int32_t>& /*in_streamIds*/) {
    // TODO: Figure this one out.
    return fromStatus(Status::OK);
}

int ExternalCameraDeviceSession::v4l2StreamOffLocked() {
    if (!mV4l2Streaming) {
        return OK;
    }

    {
        std::lock_guard<std::mutex> lk(mV4l2BufferLock);
        if (mNumDequeuedV4l2Buffers != 0) {
            ALOGE("%s: there are %zu inflight V4L buffers", __FUNCTION__, mNumDequeuedV4l2Buffers);
            return -1;
        }
    }
    mV4L2BufferCount = 0;

    // VIDIOC_STREAMOFF
    v4l2_buf_type capture_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (TEMP_FAILURE_RETRY(ioctl(mV4l2Fd.get(), VIDIOC_STREAMOFF, &capture_type)) < 0) {
        ALOGE("%s: STREAMOFF failed: %s", __FUNCTION__, strerror(errno));
        return -errno;
    }

    // VIDIOC_REQBUFS: clear buffers
    v4l2_requestbuffers req_buffers{};
    req_buffers.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req_buffers.memory = V4L2_MEMORY_MMAP;
    req_buffers.count = 0;
    if (TEMP_FAILURE_RETRY(ioctl(mV4l2Fd.get(), VIDIOC_REQBUFS, &req_buffers)) < 0) {
        ALOGE("%s: REQBUFS failed: %s", __FUNCTION__, strerror(errno));
        return -errno;
    }

    mV4l2Streaming = false;
    return OK;
}

int ExternalCameraDeviceSession::setV4l2FpsLocked(double fps) {
    // VIDIOC_G_PARM/VIDIOC_S_PARM: set fps
    v4l2_streamparm streamparm = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE};
    // The following line checks that the driver knows about framerate get/set.
    int ret = TEMP_FAILURE_RETRY(ioctl(mV4l2Fd.get(), VIDIOC_G_PARM, &streamparm));
    if (ret != 0) {
        if (errno == -EINVAL) {
            ALOGW("%s: device does not support VIDIOC_G_PARM", __FUNCTION__);
        }
        return -errno;
    }
    // Now check if the device is able to accept a capture framerate set.
    if (!(streamparm.parm.capture.capability & V4L2_CAP_TIMEPERFRAME)) {
        ALOGW("%s: device does not support V4L2_CAP_TIMEPERFRAME", __FUNCTION__);
        return -EINVAL;
    }

    // fps is float, approximate by a fraction.
    const int kFrameRatePrecision = 10000;
    streamparm.parm.capture.timeperframe.numerator = kFrameRatePrecision;
    streamparm.parm.capture.timeperframe.denominator = (fps * kFrameRatePrecision);

    if (TEMP_FAILURE_RETRY(ioctl(mV4l2Fd.get(), VIDIOC_S_PARM, &streamparm)) < 0) {
        ALOGE("%s: failed to set framerate to %f: %s", __FUNCTION__, fps, strerror(errno));
        return -1;
    }

    double retFps = streamparm.parm.capture.timeperframe.denominator /
                    static_cast<double>(streamparm.parm.capture.timeperframe.numerator);
    if (std::fabs(fps - retFps) > 1.0) {
        ALOGE("%s: expect fps %f, got %f instead", __FUNCTION__, fps, retFps);
        return -1;
    }
    mV4l2StreamingFps = fps;
    return 0;
}

void ExternalCameraDeviceSession::cleanupInflightFences(std::vector<int>& allFences,
                                                        size_t numFences) {
    for (size_t j = 0; j < numFences; j++) {
        sHandleImporter.closeFence(allFences[j]);
    }
}

void ExternalCameraDeviceSession::cleanupBuffersLocked(int id) {
    for (auto& pair : mCirculatingBuffers.at(id)) {
        sHandleImporter.freeBuffer(pair.second);
    }
    mCirculatingBuffers[id].clear();
    mCirculatingBuffers.erase(id);
}

void ExternalCameraDeviceSession::notifyShutter(int32_t frameNumber, nsecs_t shutterTs) {
    NotifyMsg msg;
    msg.set<NotifyMsg::Tag::shutter>(ShutterMsg{
            .frameNumber = frameNumber,
            .timestamp = shutterTs,
    });
    mCallback->notify({msg});
}
void ExternalCameraDeviceSession::notifyError(int32_t frameNumber, int32_t streamId, ErrorCode ec) {
    NotifyMsg msg;
    msg.set<NotifyMsg::Tag::error>(ErrorMsg{
            .frameNumber = frameNumber,
            .errorStreamId = streamId,
            .errorCode = ec,
    });
    mCallback->notify({msg});
}

void ExternalCameraDeviceSession::invokeProcessCaptureResultCallback(
        std::vector<CaptureResult>& results, bool tryWriteFmq) {
    if (mProcessCaptureResultLock.tryLock() != OK) {
        const nsecs_t NS_TO_SECOND = 1000000000;
        ALOGV("%s: previous call is not finished! waiting 1s...", __FUNCTION__);
        if (mProcessCaptureResultLock.timedLock(/* 1s */ NS_TO_SECOND) != OK) {
            ALOGE("%s: cannot acquire lock in 1s, cannot proceed", __FUNCTION__);
            return;
        }
    }
    if (tryWriteFmq && mResultMetadataQueue->availableToWrite() > 0) {
        for (CaptureResult& result : results) {
            CameraMetadata& md = result.result;
            if (!md.metadata.empty()) {
                if (mResultMetadataQueue->write(reinterpret_cast<int8_t*>(md.metadata.data()),
                                                md.metadata.size())) {
                    result.fmqResultSize = md.metadata.size();
                    md.metadata.resize(0);
                } else {
                    ALOGW("%s: couldn't utilize fmq, fall back to hwbinder", __FUNCTION__);
                    result.fmqResultSize = 0;
                }
            } else {
                result.fmqResultSize = 0;
            }
        }
    }
    auto status = mCallback->processCaptureResult(results);
    if (!status.isOk()) {
        ALOGE("%s: processCaptureResult ERROR : %d:%d", __FUNCTION__, status.getExceptionCode(),
              status.getServiceSpecificError());
    }

    mProcessCaptureResultLock.unlock();
}

int ExternalCameraDeviceSession::waitForV4L2BufferReturnLocked(std::unique_lock<std::mutex>& lk) {
    ATRACE_CALL();
    auto timeout = std::chrono::seconds(kBufferWaitTimeoutSec);
    mLock.unlock();
    auto st = mV4L2BufferReturned.wait_for(lk, timeout);
    // Here we introduce an order where mV4l2BufferLock is acquired before mLock, while
    // the normal lock acquisition order is reversed. This is fine because in most of
    // cases we are protected by mInterfaceLock. The only thread that can cause deadlock
    // is the OutputThread, where we do need to make sure we don't acquire mLock then
    // mV4l2BufferLock
    mLock.lock();
    if (st == std::cv_status::timeout) {
        ALOGE("%s: wait for V4L2 buffer return timeout!", __FUNCTION__);
        return -1;
    }
    return 0;
}

bool ExternalCameraDeviceSession::supportOfflineLocked(int32_t streamId) {
    const Stream& stream = mStreamMap[streamId];
    if (stream.format == PixelFormat::BLOB &&
        static_cast<int32_t>(stream.dataSpace) == static_cast<int32_t>(Dataspace::JFIF)) {
        return true;
    }
    // TODO: support YUV output stream?
    return false;
}

bool ExternalCameraDeviceSession::canDropRequest(const std::vector<int32_t>& offlineStreams,
                                                 std::shared_ptr<HalRequest> halReq) {
    for (const auto& buffer : halReq->buffers) {
        for (auto offlineStreamId : offlineStreams) {
            if (buffer.streamId == offlineStreamId) {
                return false;
            }
        }
    }
    // Only drop a request completely if it has no offline output
    return true;
}

void ExternalCameraDeviceSession::fillOfflineSessionInfo(
        const std::vector<int32_t>& offlineStreams,
        std::deque<std::shared_ptr<HalRequest>>& offlineReqs,
        const std::map<int, CirculatingBuffers>& circulatingBuffers,
        CameraOfflineSessionInfo* info) {
    if (info == nullptr) {
        ALOGE("%s: output info must not be null!", __FUNCTION__);
        return;
    }

    info->offlineStreams.resize(offlineStreams.size());
    info->offlineRequests.resize(offlineReqs.size());

    // Fill in offline reqs and count outstanding buffers
    for (size_t i = 0; i < offlineReqs.size(); i++) {
        info->offlineRequests[i].frameNumber = offlineReqs[i]->frameNumber;
        info->offlineRequests[i].pendingStreams.resize(offlineReqs[i]->buffers.size());
        for (size_t bIdx = 0; bIdx < offlineReqs[i]->buffers.size(); bIdx++) {
            int32_t streamId = offlineReqs[i]->buffers[bIdx].streamId;
            info->offlineRequests[i].pendingStreams[bIdx] = streamId;
        }
    }

    for (size_t i = 0; i < offlineStreams.size(); i++) {
        int32_t streamId = offlineStreams[i];
        info->offlineStreams[i].id = streamId;
        // outstanding buffers are 0 since we are doing hal buffer management and
        // offline session will ask for those buffers later
        info->offlineStreams[i].numOutstandingBuffers = 0;
        const CirculatingBuffers& bufIdMap = circulatingBuffers.at(streamId);
        info->offlineStreams[i].circulatingBufferIds.resize(bufIdMap.size());
        size_t bIdx = 0;
        for (const auto& pair : bufIdMap) {
            // Fill in bufferId
            info->offlineStreams[i].circulatingBufferIds[bIdx++] = pair.first;
        }
    }
}

Status ExternalCameraDeviceSession::isStreamCombinationSupported(
        const StreamConfiguration& config, const std::vector<SupportedV4L2Format>& supportedFormats,
        const ExternalCameraConfig& devCfg) {
    if (config.operationMode != StreamConfigurationMode::NORMAL_MODE) {
        ALOGE("%s: unsupported operation mode: %d", __FUNCTION__, config.operationMode);
        return Status::ILLEGAL_ARGUMENT;
    }

    if (config.streams.size() == 0) {
        ALOGE("%s: cannot configure zero stream", __FUNCTION__);
        return Status::ILLEGAL_ARGUMENT;
    }

    int numProcessedStream = 0;
    int numStallStream = 0;
    for (const auto& stream : config.streams) {
        // Check if the format/width/height combo is supported
        if (!isSupported(stream, supportedFormats, devCfg)) {
            return Status::ILLEGAL_ARGUMENT;
        }
        if (stream.format == PixelFormat::BLOB) {
            numStallStream++;
        } else {
            numProcessedStream++;
        }
    }

    if (numProcessedStream > kMaxProcessedStream) {
        ALOGE("%s: too many processed streams (expect <= %d, got %d)", __FUNCTION__,
              kMaxProcessedStream, numProcessedStream);
        return Status::ILLEGAL_ARGUMENT;
    }

    if (numStallStream > kMaxStallStream) {
        ALOGE("%s: too many stall streams (expect <= %d, got %d)", __FUNCTION__, kMaxStallStream,
              numStallStream);
        return Status::ILLEGAL_ARGUMENT;
    }

    return Status::OK;
}
void ExternalCameraDeviceSession::updateBufferCaches(
        const std::vector<BufferCache>& cachesToRemove) {
    Mutex::Autolock _l(mCbsLock);
    for (auto& cache : cachesToRemove) {
        auto cbsIt = mCirculatingBuffers.find(cache.streamId);
        if (cbsIt == mCirculatingBuffers.end()) {
            // The stream could have been removed
            continue;
        }
        CirculatingBuffers& cbs = cbsIt->second;
        auto it = cbs.find(cache.bufferId);
        if (it != cbs.end()) {
            sHandleImporter.freeBuffer(it->second);
            cbs.erase(it);
        } else {
            ALOGE("%s: stream %d buffer %" PRIu64 " is not cached", __FUNCTION__, cache.streamId,
                  cache.bufferId);
        }
    }
}

Status ExternalCameraDeviceSession::processCaptureRequestError(
        const std::shared_ptr<HalRequest>& req, std::vector<NotifyMsg>* outMsgs,
        std::vector<CaptureResult>* outResults) {
    ATRACE_CALL();
    // Return V4L2 buffer to V4L2 buffer queue
    std::shared_ptr<V4L2Frame> v4l2Frame = std::static_pointer_cast<V4L2Frame>(req->frameIn);
    enqueueV4l2Frame(v4l2Frame);

    if (outMsgs == nullptr) {
        notifyShutter(req->frameNumber, req->shutterTs);
        notifyError(/*frameNum*/ req->frameNumber, /*stream*/ -1, ErrorCode::ERROR_REQUEST);
    } else {
        NotifyMsg shutter;
        shutter.set<NotifyMsg::Tag::shutter>(
                ShutterMsg{.frameNumber = req->frameNumber, .timestamp = req->shutterTs});

        NotifyMsg error;
        error.set<NotifyMsg::Tag::error>(ErrorMsg{.frameNumber = req->frameNumber,
                                                  .errorStreamId = -1,
                                                  .errorCode = ErrorCode::ERROR_REQUEST});
        outMsgs->push_back(shutter);
        outMsgs->push_back(error);
    }

    // Fill output buffers
    CaptureResult result;
    result.frameNumber = req->frameNumber;
    result.partialResult = 1;
    result.inputBuffer.streamId = -1;
    result.outputBuffers.resize(req->buffers.size());
    for (size_t i = 0; i < req->buffers.size(); i++) {
        result.outputBuffers[i].streamId = req->buffers[i].streamId;
        result.outputBuffers[i].bufferId = req->buffers[i].bufferId;
        result.outputBuffers[i].status = BufferStatus::ERROR;
        if (req->buffers[i].acquireFence >= 0) {
            // numFds = 0 for error
            native_handle_t* handle = native_handle_create(/*numFds*/ 0, /*numInts*/ 0);
            result.outputBuffers[i].releaseFence = android::dupToAidl(handle);
            native_handle_delete(handle);
        }
    }

    // update inflight records
    {
        std::lock_guard<std::mutex> lk(mInflightFramesLock);
        mInflightFrames.erase(req->frameNumber);
    }

    if (outResults == nullptr) {
        // Callback into framework
        std::vector<CaptureResult> results(1);
        results[0] = std::move(result);
        invokeProcessCaptureResultCallback(results, /* tryWriteFmq */ true);
        freeReleaseFences(results);
    } else {
        outResults->push_back(std::move(result));
    }
    return Status::OK;
}

Status ExternalCameraDeviceSession::processCaptureResult(std::shared_ptr<HalRequest>& req) {
    ATRACE_CALL();
    // Return V4L2 buffer to V4L2 buffer queue
    std::shared_ptr<V4L2Frame> v4l2Frame = std::static_pointer_cast<V4L2Frame>(req->frameIn);
    enqueueV4l2Frame(v4l2Frame);

    // NotifyShutter
    notifyShutter(req->frameNumber, req->shutterTs);

    // Fill output buffers;
    std::vector<CaptureResult> results(1);
    CaptureResult& result = results[0];
    result.frameNumber = req->frameNumber;
    result.partialResult = 1;
    result.inputBuffer.streamId = -1;
    result.outputBuffers.resize(req->buffers.size());
    for (size_t i = 0; i < req->buffers.size(); i++) {
        result.outputBuffers[i].streamId = req->buffers[i].streamId;
        result.outputBuffers[i].bufferId = req->buffers[i].bufferId;
        if (req->buffers[i].fenceTimeout) {
            result.outputBuffers[i].status = BufferStatus::ERROR;
            if (req->buffers[i].acquireFence >= 0) {
                native_handle_t* handle = native_handle_create(/*numFds*/ 1, /*numInts*/ 0);
                handle->data[0] = req->buffers[i].acquireFence;
                result.outputBuffers[i].releaseFence = android::dupToAidl(handle);
                native_handle_delete(handle);
            }
            notifyError(req->frameNumber, req->buffers[i].streamId, ErrorCode::ERROR_BUFFER);
        } else {
            result.outputBuffers[i].status = BufferStatus::OK;
            // TODO: refactor
            if (req->buffers[i].acquireFence >= 0) {
                native_handle_t* handle = native_handle_create(/*numFds*/ 1, /*numInts*/ 0);
                handle->data[0] = req->buffers[i].acquireFence;
                result.outputBuffers[i].releaseFence = android::dupToAidl(handle);
                native_handle_delete(handle);
            }
        }
    }

    // Fill capture result metadata
    fillCaptureResult(req->setting, req->shutterTs);
    const camera_metadata_t* rawResult = req->setting.getAndLock();
    convertToAidl(rawResult, &result.result);
    req->setting.unlock(rawResult);

    // update inflight records
    {
        std::lock_guard<std::mutex> lk(mInflightFramesLock);
        mInflightFrames.erase(req->frameNumber);
    }

    // Callback into framework
    invokeProcessCaptureResultCallback(results, /* tryWriteFmq */ true);
    freeReleaseFences(results);
    return Status::OK;
}

ssize_t ExternalCameraDeviceSession::getJpegBufferSize(int32_t width, int32_t height) const {
    // Constant from camera3.h
    const ssize_t kMinJpegBufferSize = 256 * 1024 + sizeof(CameraBlob);
    // Get max jpeg size (area-wise).
    if (mMaxJpegResolution.width == 0) {
        ALOGE("%s: No supported JPEG stream", __FUNCTION__);
        return BAD_VALUE;
    }

    // Get max jpeg buffer size
    ssize_t maxJpegBufferSize = 0;
    camera_metadata_ro_entry jpegBufMaxSize = mCameraCharacteristics.find(ANDROID_JPEG_MAX_SIZE);
    if (jpegBufMaxSize.count == 0) {
        ALOGE("%s: Can't find maximum JPEG size in static metadata!", __FUNCTION__);
        return BAD_VALUE;
    }
    maxJpegBufferSize = jpegBufMaxSize.data.i32[0];

    if (maxJpegBufferSize <= kMinJpegBufferSize) {
        ALOGE("%s: ANDROID_JPEG_MAX_SIZE (%zd) <= kMinJpegBufferSize (%zd)", __FUNCTION__,
              maxJpegBufferSize, kMinJpegBufferSize);
        return BAD_VALUE;
    }

    // Calculate final jpeg buffer size for the given resolution.
    float scaleFactor =
            ((float)(width * height)) / (mMaxJpegResolution.width * mMaxJpegResolution.height);
    ssize_t jpegBufferSize =
            scaleFactor * (maxJpegBufferSize - kMinJpegBufferSize) + kMinJpegBufferSize;
    if (jpegBufferSize > maxJpegBufferSize) {
        jpegBufferSize = maxJpegBufferSize;
    }

    return jpegBufferSize;
}
binder_status_t ExternalCameraDeviceSession::dump(int fd, const char** /*args*/,
                                                  uint32_t /*numArgs*/) {
    bool intfLocked = tryLock(mInterfaceLock);
    if (!intfLocked) {
        dprintf(fd, "!! ExternalCameraDeviceSession interface may be deadlocked !!\n");
    }

    if (isClosed()) {
        dprintf(fd, "External camera %s is closed\n", mCameraId.c_str());
        return STATUS_OK;
    }

    bool streaming = false;
    size_t v4L2BufferCount = 0;
    SupportedV4L2Format streamingFmt;
    {
        bool sessionLocked = tryLock(mLock);
        if (!sessionLocked) {
            dprintf(fd, "!! ExternalCameraDeviceSession mLock may be deadlocked !!\n");
        }
        streaming = mV4l2Streaming;
        streamingFmt = mV4l2StreamingFmt;
        v4L2BufferCount = mV4L2BufferCount;

        if (sessionLocked) {
            mLock.unlock();
        }
    }

    std::unordered_set<uint32_t> inflightFrames;
    {
        bool iffLocked = tryLock(mInflightFramesLock);
        if (!iffLocked) {
            dprintf(fd,
                    "!! ExternalCameraDeviceSession mInflightFramesLock may be deadlocked !!\n");
        }
        inflightFrames = mInflightFrames;
        if (iffLocked) {
            mInflightFramesLock.unlock();
        }
    }

    dprintf(fd, "External camera %s V4L2 FD %d, cropping type %s, %s\n", mCameraId.c_str(),
            mV4l2Fd.get(), (mCroppingType == VERTICAL) ? "vertical" : "horizontal",
            streaming ? "streaming" : "not streaming");

    if (streaming) {
        // TODO: dump fps later
        dprintf(fd, "Current V4L2 format %c%c%c%c %dx%d @ %ffps\n", streamingFmt.fourcc & 0xFF,
                (streamingFmt.fourcc >> 8) & 0xFF, (streamingFmt.fourcc >> 16) & 0xFF,
                (streamingFmt.fourcc >> 24) & 0xFF, streamingFmt.width, streamingFmt.height,
                mV4l2StreamingFps);

        size_t numDequeuedV4l2Buffers = 0;
        {
            std::lock_guard<std::mutex> lk(mV4l2BufferLock);
            numDequeuedV4l2Buffers = mNumDequeuedV4l2Buffers;
        }
        dprintf(fd, "V4L2 buffer queue size %zu, dequeued %zu\n", v4L2BufferCount,
                numDequeuedV4l2Buffers);
    }

    dprintf(fd, "In-flight frames (not sorted):");
    for (const auto& frameNumber : inflightFrames) {
        dprintf(fd, "%d, ", frameNumber);
    }
    dprintf(fd, "\n");
    mOutputThread->dump(fd);
    dprintf(fd, "\n");

    if (intfLocked) {
        mInterfaceLock.unlock();
    }

    return STATUS_OK;
}

// Start ExternalCameraDeviceSession::BufferRequestThread functions
ExternalCameraDeviceSession::BufferRequestThread::BufferRequestThread(
        std::weak_ptr<OutputThreadInterface> parent,
        std::shared_ptr<ICameraDeviceCallback> callbacks)
    : mParent(parent), mCallbacks(callbacks) {}

int ExternalCameraDeviceSession::BufferRequestThread::requestBufferStart(
        const std::vector<HalStreamBuffer>& bufReqs) {
    if (bufReqs.empty()) {
        ALOGE("%s: bufReqs is empty!", __FUNCTION__);
        return -1;
    }

    {
        std::lock_guard<std::mutex> lk(mLock);
        if (mRequestingBuffer) {
            ALOGE("%s: BufferRequestThread does not support more than one concurrent request!",
                  __FUNCTION__);
            return -1;
        }

        mBufferReqs = bufReqs;
        mRequestingBuffer = true;
    }
    mRequestCond.notify_one();
    return 0;
}

int ExternalCameraDeviceSession::BufferRequestThread::waitForBufferRequestDone(
        std::vector<HalStreamBuffer>* outBufReqs) {
    std::unique_lock<std::mutex> lk(mLock);
    if (!mRequestingBuffer) {
        ALOGE("%s: no pending buffer request!", __FUNCTION__);
        return -1;
    }

    if (mPendingReturnBufferReqs.empty()) {
        std::chrono::milliseconds timeout = std::chrono::milliseconds(kReqProcTimeoutMs);
        auto st = mRequestDoneCond.wait_for(lk, timeout);
        if (st == std::cv_status::timeout) {
            mRequestingBuffer = false;
            ALOGE("%s: wait for buffer request finish timeout!", __FUNCTION__);
            return -1;
        }

        if (mPendingReturnBufferReqs.empty()) {
            mRequestingBuffer = false;
            ALOGE("%s: cameraservice did not return any buffers!", __FUNCTION__);
            return -1;
        }
    }
    mRequestingBuffer = false;
    *outBufReqs = std::move(mPendingReturnBufferReqs);
    mPendingReturnBufferReqs.clear();
    return 0;
}

void ExternalCameraDeviceSession::BufferRequestThread::waitForNextRequest() {
    ATRACE_CALL();
    std::unique_lock<std::mutex> lk(mLock);
    int waitTimes = 0;
    while (mBufferReqs.empty()) {
        if (exitPending()) {
            return;
        }
        auto timeout = std::chrono::milliseconds(kReqWaitTimeoutMs);
        auto st = mRequestCond.wait_for(lk, timeout);
        if (st == std::cv_status::timeout) {
            waitTimes++;
            if (waitTimes == kReqWaitTimesWarn) {
                // BufferRequestThread just wait forever for new buffer request
                // But it will print some periodic warning indicating it's waiting
                ALOGV("%s: still waiting for new buffer request", __FUNCTION__);
                waitTimes = 0;
            }
        }
    }

    // Fill in BufferRequest
    mHalBufferReqs.resize(mBufferReqs.size());
    for (size_t i = 0; i < mHalBufferReqs.size(); i++) {
        mHalBufferReqs[i].streamId = mBufferReqs[i].streamId;
        mHalBufferReqs[i].numBuffersRequested = 1;
    }
}

bool ExternalCameraDeviceSession::BufferRequestThread::threadLoop() {
    waitForNextRequest();
    if (exitPending()) {
        return false;
    }

    ATRACE_BEGIN("AIDL requestStreamBuffers");
    BufferRequestStatus status;
    std::vector<StreamBufferRet> bufRets;
    ScopedAStatus ret = mCallbacks->requestStreamBuffers(mHalBufferReqs, &bufRets, &status);
    if (!ret.isOk()) {
        ALOGE("%s: Transaction error: %d:%d", __FUNCTION__, ret.getExceptionCode(),
              ret.getServiceSpecificError());
        mBufferReqs.clear();
        mRequestDoneCond.notify_one();
        return false;
    }

    std::unique_lock<std::mutex> lk(mLock);
    if (status == BufferRequestStatus::OK || status == BufferRequestStatus::FAILED_PARTIAL) {
        if (bufRets.size() != mHalBufferReqs.size()) {
            ALOGE("%s: expect %zu buffer requests returned, only got %zu", __FUNCTION__,
                  mHalBufferReqs.size(), bufRets.size());
            mBufferReqs.clear();
            lk.unlock();
            mRequestDoneCond.notify_one();
            return false;
        }

        auto parent = mParent.lock();
        if (parent == nullptr) {
            ALOGE("%s: session has been disconnected!", __FUNCTION__);
            mBufferReqs.clear();
            lk.unlock();
            mRequestDoneCond.notify_one();
            return false;
        }

        std::vector<int> importedFences;
        importedFences.resize(bufRets.size());
        bool hasError = false;
        for (size_t i = 0; i < bufRets.size(); i++) {
            int streamId = bufRets[i].streamId;
            switch (bufRets[i].val.getTag()) {
                case StreamBuffersVal::Tag::error:
                    continue;
                case StreamBuffersVal::Tag::buffers: {
                    const std::vector<StreamBuffer>& hBufs =
                            bufRets[i].val.get<StreamBuffersVal::Tag::buffers>();
                    if (hBufs.size() != 1) {
                        ALOGE("%s: expect 1 buffer returned, got %zu!", __FUNCTION__, hBufs.size());
                        hasError = true;
                        break;
                    }
                    const StreamBuffer& hBuf = hBufs[0];

                    mBufferReqs[i].bufferId = hBuf.bufferId;
                    // TODO: create a batch import API so we don't need to lock/unlock mCbsLock
                    // repeatedly?
                    lk.unlock();
                    native_handle_t* h = makeFromAidl(hBuf.buffer);
                    Status s = parent->importBuffer(streamId, hBuf.bufferId, h,
                                                    /*out*/ &mBufferReqs[i].bufPtr);
                    native_handle_delete(h);
                    lk.lock();

                    if (s != Status::OK) {
                        ALOGE("%s: stream %d import buffer failed!", __FUNCTION__, streamId);
                        cleanupInflightFences(importedFences, i - 1);
                        hasError = true;
                        break;
                    }
                    h = makeFromAidl(hBuf.acquireFence);
                    if (!sHandleImporter.importFence(h, mBufferReqs[i].acquireFence)) {
                        ALOGE("%s: stream %d import fence failed!", __FUNCTION__, streamId);
                        cleanupInflightFences(importedFences, i - 1);
                        native_handle_delete(h);
                        hasError = true;
                        break;
                    }
                    native_handle_delete(h);
                    importedFences[i] = mBufferReqs[i].acquireFence;
                } break;
                default:
                    ALOGE("%s: Unknown StreamBuffersVal!", __FUNCTION__);
                    hasError = true;
                    break;
            }
            if (hasError) {
                mBufferReqs.clear();
                lk.unlock();
                mRequestDoneCond.notify_one();
                return true;
            }
        }
    } else {
        ALOGE("%s: requestStreamBuffers call failed!", __FUNCTION__);
        mBufferReqs.clear();
        lk.unlock();
        mRequestDoneCond.notify_one();
        return true;
    }

    mPendingReturnBufferReqs = std::move(mBufferReqs);
    mBufferReqs.clear();

    lk.unlock();
    mRequestDoneCond.notify_one();
    return true;
}

// End ExternalCameraDeviceSession::BufferRequestThread functions

// Start ExternalCameraDeviceSession::OutputThread functions

ExternalCameraDeviceSession::OutputThread::OutputThread(
        std::weak_ptr<OutputThreadInterface> parent, CroppingType ct,
        const common::V1_0::helper::CameraMetadata& chars,
        std::shared_ptr<BufferRequestThread> bufReqThread)
    : mParent(parent),
      mCroppingType(ct),
      mCameraCharacteristics(chars),
      mBufferRequestThread(bufReqThread) {}

ExternalCameraDeviceSession::OutputThread::~OutputThread() {}

Status ExternalCameraDeviceSession::OutputThread::allocateIntermediateBuffers(
        const Size& v4lSize, const Size& thumbSize, const std::vector<Stream>& streams,
        uint32_t blobBufferSize) {
    std::lock_guard<std::mutex> lk(mBufferLock);
    if (!mScaledYu12Frames.empty()) {
        ALOGE("%s: intermediate buffer pool has %zu inflight buffers! (expect 0)", __FUNCTION__,
              mScaledYu12Frames.size());
        return Status::INTERNAL_ERROR;
    }

    // Allocating intermediate YU12 frame
    if (mYu12Frame == nullptr || mYu12Frame->mWidth != v4lSize.width ||
        mYu12Frame->mHeight != v4lSize.height) {
        mYu12Frame.reset();
        mYu12Frame = std::make_shared<AllocatedFrame>(v4lSize.width, v4lSize.height);
        int ret = mYu12Frame->allocate(&mYu12FrameLayout);
        if (ret != 0) {
            ALOGE("%s: allocating YU12 frame failed!", __FUNCTION__);
            return Status::INTERNAL_ERROR;
        }
    }

    // Allocating intermediate YU12 thumbnail frame
    if (mYu12ThumbFrame == nullptr || mYu12ThumbFrame->mWidth != thumbSize.width ||
        mYu12ThumbFrame->mHeight != thumbSize.height) {
        mYu12ThumbFrame.reset();
        mYu12ThumbFrame = std::make_shared<AllocatedFrame>(thumbSize.width, thumbSize.height);
        int ret = mYu12ThumbFrame->allocate(&mYu12ThumbFrameLayout);
        if (ret != 0) {
            ALOGE("%s: allocating YU12 thumb frame failed!", __FUNCTION__);
            return Status::INTERNAL_ERROR;
        }
    }

    // Allocating scaled buffers
    for (const auto& stream : streams) {
        Size sz = {stream.width, stream.height};
        if (sz == v4lSize) {
            continue;  // Don't need an intermediate buffer same size as v4lBuffer
        }
        if (mIntermediateBuffers.count(sz) == 0) {
            // Create new intermediate buffer
            std::shared_ptr<AllocatedFrame> buf =
                    std::make_shared<AllocatedFrame>(stream.width, stream.height);
            int ret = buf->allocate();
            if (ret != 0) {
                ALOGE("%s: allocating intermediate YU12 frame %dx%d failed!", __FUNCTION__,
                      stream.width, stream.height);
                return Status::INTERNAL_ERROR;
            }
            mIntermediateBuffers[sz] = buf;
        }
    }

    // Remove unconfigured buffers
    auto it = mIntermediateBuffers.begin();
    while (it != mIntermediateBuffers.end()) {
        bool configured = false;
        auto sz = it->first;
        for (const auto& stream : streams) {
            if (stream.width == sz.width && stream.height == sz.height) {
                configured = true;
                break;
            }
        }
        if (configured) {
            it++;
        } else {
            it = mIntermediateBuffers.erase(it);
        }
    }

    // Allocate mute test pattern frame
    mMuteTestPatternFrame.resize(mYu12Frame->mWidth * mYu12Frame->mHeight * 3);

    mBlobBufferSize = blobBufferSize;
    return Status::OK;
}

Status ExternalCameraDeviceSession::OutputThread::submitRequest(
        const std::shared_ptr<HalRequest>& req) {
    std::unique_lock<std::mutex> lk(mRequestListLock);
    mRequestList.push_back(req);
    lk.unlock();
    mRequestCond.notify_one();
    return Status::OK;
}

void ExternalCameraDeviceSession::OutputThread::flush() {
    ATRACE_CALL();
    auto parent = mParent.lock();
    if (parent == nullptr) {
        ALOGE("%s: session has been disconnected!", __FUNCTION__);
        return;
    }

    std::unique_lock<std::mutex> lk(mRequestListLock);
    std::list<std::shared_ptr<HalRequest>> reqs = std::move(mRequestList);
    mRequestList.clear();
    if (mProcessingRequest) {
        auto timeout = std::chrono::seconds(kFlushWaitTimeoutSec);
        auto st = mRequestDoneCond.wait_for(lk, timeout);
        if (st == std::cv_status::timeout) {
            ALOGE("%s: wait for inflight request finish timeout!", __FUNCTION__);
        }
    }

    ALOGV("%s: flushing inflight requests", __FUNCTION__);
    lk.unlock();
    for (const auto& req : reqs) {
        parent->processCaptureRequestError(req);
    }
}

void ExternalCameraDeviceSession::OutputThread::dump(int fd) {
    std::lock_guard<std::mutex> lk(mRequestListLock);
    if (mProcessingRequest) {
        dprintf(fd, "OutputThread processing frame %d\n", mProcessingFrameNumber);
    } else {
        dprintf(fd, "OutputThread not processing any frames\n");
    }
    dprintf(fd, "OutputThread request list contains frame: ");
    for (const auto& req : mRequestList) {
        dprintf(fd, "%d, ", req->frameNumber);
    }
    dprintf(fd, "\n");
}

void ExternalCameraDeviceSession::OutputThread::setExifMakeModel(const std::string& make,
                                                                 const std::string& model) {
    mExifMake = make;
    mExifModel = model;
}

std::list<std::shared_ptr<HalRequest>>
ExternalCameraDeviceSession::OutputThread::switchToOffline() {
    ATRACE_CALL();
    auto parent = mParent.lock();
    if (parent == nullptr) {
        ALOGE("%s: session has been disconnected!", __FUNCTION__);
        return {};
    }

    std::unique_lock<std::mutex> lk(mRequestListLock);
    std::list<std::shared_ptr<HalRequest>> reqs = std::move(mRequestList);
    mRequestList.clear();
    if (mProcessingRequest) {
        auto timeout = std::chrono::seconds(kFlushWaitTimeoutSec);
        auto st = mRequestDoneCond.wait_for(lk, timeout);
        if (st == std::cv_status::timeout) {
            ALOGE("%s: wait for inflight request finish timeout!", __FUNCTION__);
        }
    }
    lk.unlock();
    clearIntermediateBuffers();
    ALOGV("%s: returning %zu request for offline processing", __FUNCTION__, reqs.size());
    return reqs;
}

int ExternalCameraDeviceSession::OutputThread::requestBufferStart(
        const std::vector<HalStreamBuffer>& bufs) {
    if (mBufferRequestThread == nullptr) {
        return 0;
    }
    return mBufferRequestThread->requestBufferStart(bufs);
}

int ExternalCameraDeviceSession::OutputThread::waitForBufferRequestDone(
        std::vector<HalStreamBuffer>* outBufs) {
    if (mBufferRequestThread == nullptr) {
        return 0;
    }
    return mBufferRequestThread->waitForBufferRequestDone(outBufs);
}

void ExternalCameraDeviceSession::OutputThread::waitForNextRequest(
        std::shared_ptr<HalRequest>* out) {
    ATRACE_CALL();
    if (out == nullptr) {
        ALOGE("%s: out is null", __FUNCTION__);
        return;
    }

    std::unique_lock<std::mutex> lk(mRequestListLock);
    int waitTimes = 0;
    while (mRequestList.empty()) {
        if (exitPending()) {
            return;
        }
        auto timeout = std::chrono::milliseconds(kReqWaitTimeoutMs);
        auto st = mRequestCond.wait_for(lk, timeout);
        if (st == std::cv_status::timeout) {
            waitTimes++;
            if (waitTimes == kReqWaitTimesMax) {
                // no new request, return
                return;
            }
        }
    }
    *out = mRequestList.front();
    mRequestList.pop_front();
    mProcessingRequest = true;
    mProcessingFrameNumber = (*out)->frameNumber;
}

void ExternalCameraDeviceSession::OutputThread::signalRequestDone() {
    std::unique_lock<std::mutex> lk(mRequestListLock);
    mProcessingRequest = false;
    mProcessingFrameNumber = 0;
    lk.unlock();
    mRequestDoneCond.notify_one();
}

int ExternalCameraDeviceSession::OutputThread::cropAndScaleLocked(
        std::shared_ptr<AllocatedFrame>& in, const Size& outSz, YCbCrLayout* out) {
    Size inSz = {in->mWidth, in->mHeight};

    int ret;
    if (inSz == outSz) {
        ret = in->getLayout(out);
        if (ret != 0) {
            ALOGE("%s: failed to get input image layout", __FUNCTION__);
            return ret;
        }
        return ret;
    }

    // Cropping to output aspect ratio
    IMapper::Rect inputCrop;
    ret = getCropRect(mCroppingType, inSz, outSz, &inputCrop);
    if (ret != 0) {
        ALOGE("%s: failed to compute crop rect for output size %dx%d", __FUNCTION__, outSz.width,
              outSz.height);
        return ret;
    }

    YCbCrLayout croppedLayout;
    ret = in->getCroppedLayout(inputCrop, &croppedLayout);
    if (ret != 0) {
        ALOGE("%s: failed to crop input image %dx%d to output size %dx%d", __FUNCTION__, inSz.width,
              inSz.height, outSz.width, outSz.height);
        return ret;
    }

    if ((mCroppingType == VERTICAL && inSz.width == outSz.width) ||
        (mCroppingType == HORIZONTAL && inSz.height == outSz.height)) {
        // No scale is needed
        *out = croppedLayout;
        return 0;
    }

    auto it = mScaledYu12Frames.find(outSz);
    std::shared_ptr<AllocatedFrame> scaledYu12Buf;
    if (it != mScaledYu12Frames.end()) {
        scaledYu12Buf = it->second;
    } else {
        it = mIntermediateBuffers.find(outSz);
        if (it == mIntermediateBuffers.end()) {
            ALOGE("%s: failed to find intermediate buffer size %dx%d", __FUNCTION__, outSz.width,
                  outSz.height);
            return -1;
        }
        scaledYu12Buf = it->second;
    }
    // Scale
    YCbCrLayout outLayout;
    ret = scaledYu12Buf->getLayout(&outLayout);
    if (ret != 0) {
        ALOGE("%s: failed to get output buffer layout", __FUNCTION__);
        return ret;
    }

    ret = libyuv::I420Scale(
            static_cast<uint8_t*>(croppedLayout.y), croppedLayout.yStride,
            static_cast<uint8_t*>(croppedLayout.cb), croppedLayout.cStride,
            static_cast<uint8_t*>(croppedLayout.cr), croppedLayout.cStride, inputCrop.width,
            inputCrop.height, static_cast<uint8_t*>(outLayout.y), outLayout.yStride,
            static_cast<uint8_t*>(outLayout.cb), outLayout.cStride,
            static_cast<uint8_t*>(outLayout.cr), outLayout.cStride, outSz.width, outSz.height,
            // TODO: b/72261744 see if we can use better filter without losing too much perf
            libyuv::FilterMode::kFilterNone);

    if (ret != 0) {
        ALOGE("%s: failed to scale buffer from %dx%d to %dx%d. Ret %d", __FUNCTION__,
              inputCrop.width, inputCrop.height, outSz.width, outSz.height, ret);
        return ret;
    }

    *out = outLayout;
    mScaledYu12Frames.insert({outSz, scaledYu12Buf});
    return 0;
}

int ExternalCameraDeviceSession::OutputThread::cropAndScaleThumbLocked(
        std::shared_ptr<AllocatedFrame>& in, const Size& outSz, YCbCrLayout* out) {
    Size inSz{in->mWidth, in->mHeight};

    if ((outSz.width * outSz.height) > (mYu12ThumbFrame->mWidth * mYu12ThumbFrame->mHeight)) {
        ALOGE("%s: Requested thumbnail size too big (%d,%d) > (%d,%d)", __FUNCTION__, outSz.width,
              outSz.height, mYu12ThumbFrame->mWidth, mYu12ThumbFrame->mHeight);
        return -1;
    }

    int ret;

    /* This will crop-and-zoom the input YUV frame to the thumbnail size
     * Based on the following logic:
     *  1) Square pixels come in, square pixels come out, therefore single
     *  scale factor is computed to either make input bigger or smaller
     *  depending on if we are upscaling or downscaling
     *  2) That single scale factor would either make height too tall or width
     *  too wide so we need to crop the input either horizontally or vertically
     *  but not both
     */

    /* Convert the input and output dimensions into floats for ease of math */
    float fWin = static_cast<float>(inSz.width);
    float fHin = static_cast<float>(inSz.height);
    float fWout = static_cast<float>(outSz.width);
    float fHout = static_cast<float>(outSz.height);

    /* Compute the one scale factor from (1) above, it will be the smaller of
     * the two possibilities. */
    float scaleFactor = std::min(fHin / fHout, fWin / fWout);

    /* Since we are crop-and-zooming (as opposed to letter/pillar boxing) we can
     * simply multiply the output by our scaleFactor to get the cropped input
     * size. Note that at least one of {fWcrop, fHcrop} is going to wind up
     * being {fWin, fHin} respectively because fHout or fWout cancels out the
     * scaleFactor calculation above.
     *
     * Specifically:
     *  if ( fHin / fHout ) < ( fWin / fWout ) we crop the sides off
     * input, in which case
     *    scaleFactor = fHin / fHout
     *    fWcrop = fHin / fHout * fWout
     *    fHcrop = fHin
     *
     * Note that fWcrop <= fWin ( because ( fHin / fHout ) * fWout < fWin, which
     * is just the inequality above with both sides multiplied by fWout
     *
     * on the other hand if ( fWin / fWout ) < ( fHin / fHout) we crop the top
     * and the bottom off of input, and
     *    scaleFactor = fWin / fWout
     *    fWcrop = fWin
     *    fHCrop = fWin / fWout * fHout
     */
    float fWcrop = scaleFactor * fWout;
    float fHcrop = scaleFactor * fHout;

    /* Convert to integer and truncate to an even number */
    Size cropSz = {.width = 2 * static_cast<int32_t>(fWcrop / 2.0f),
                   .height = 2 * static_cast<int32_t>(fHcrop / 2.0f)};

    /* Convert to a centered rectange with even top/left */
    IMapper::Rect inputCrop{.left = 2 * static_cast<int32_t>((inSz.width - cropSz.width) / 4),
                            .top = 2 * static_cast<int32_t>((inSz.height - cropSz.height) / 4),
                            .width = static_cast<int32_t>(cropSz.width),
                            .height = static_cast<int32_t>(cropSz.height)};

    if ((inputCrop.top < 0) || (inputCrop.top >= static_cast<int32_t>(inSz.height)) ||
        (inputCrop.left < 0) || (inputCrop.left >= static_cast<int32_t>(inSz.width)) ||
        (inputCrop.width <= 0) ||
        (inputCrop.width + inputCrop.left > static_cast<int32_t>(inSz.width)) ||
        (inputCrop.height <= 0) ||
        (inputCrop.height + inputCrop.top > static_cast<int32_t>(inSz.height))) {
        ALOGE("%s: came up with really wrong crop rectangle", __FUNCTION__);
        ALOGE("%s: input layout %dx%d to for output size %dx%d", __FUNCTION__, inSz.width,
              inSz.height, outSz.width, outSz.height);
        ALOGE("%s: computed input crop +%d,+%d %dx%d", __FUNCTION__, inputCrop.left, inputCrop.top,
              inputCrop.width, inputCrop.height);
        return -1;
    }

    YCbCrLayout inputLayout;
    ret = in->getCroppedLayout(inputCrop, &inputLayout);
    if (ret != 0) {
        ALOGE("%s: failed to crop input layout %dx%d to for output size %dx%d", __FUNCTION__,
              inSz.width, inSz.height, outSz.width, outSz.height);
        ALOGE("%s: computed input crop +%d,+%d %dx%d", __FUNCTION__, inputCrop.left, inputCrop.top,
              inputCrop.width, inputCrop.height);
        return ret;
    }
    ALOGV("%s: crop input layout %dx%d to for output size %dx%d", __FUNCTION__, inSz.width,
          inSz.height, outSz.width, outSz.height);
    ALOGV("%s: computed input crop +%d,+%d %dx%d", __FUNCTION__, inputCrop.left, inputCrop.top,
          inputCrop.width, inputCrop.height);

    // Scale
    YCbCrLayout outFullLayout;

    ret = mYu12ThumbFrame->getLayout(&outFullLayout);
    if (ret != 0) {
        ALOGE("%s: failed to get output buffer layout", __FUNCTION__);
        return ret;
    }

    ret = libyuv::I420Scale(static_cast<uint8_t*>(inputLayout.y), inputLayout.yStride,
                            static_cast<uint8_t*>(inputLayout.cb), inputLayout.cStride,
                            static_cast<uint8_t*>(inputLayout.cr), inputLayout.cStride,
                            inputCrop.width, inputCrop.height,
                            static_cast<uint8_t*>(outFullLayout.y), outFullLayout.yStride,
                            static_cast<uint8_t*>(outFullLayout.cb), outFullLayout.cStride,
                            static_cast<uint8_t*>(outFullLayout.cr), outFullLayout.cStride,
                            outSz.width, outSz.height, libyuv::FilterMode::kFilterNone);

    if (ret != 0) {
        ALOGE("%s: failed to scale buffer from %dx%d to %dx%d. Ret %d", __FUNCTION__,
              inputCrop.width, inputCrop.height, outSz.width, outSz.height, ret);
        return ret;
    }

    *out = outFullLayout;
    return 0;
}

int ExternalCameraDeviceSession::OutputThread::createJpegLocked(
        HalStreamBuffer& halBuf, const common::V1_0::helper::CameraMetadata& setting) {
    ATRACE_CALL();
    int ret;
    auto lfail = [&](auto... args) {
        ALOGE(args...);

        return 1;
    };
    auto parent = mParent.lock();
    if (parent == nullptr) {
        ALOGE("%s: session has been disconnected!", __FUNCTION__);
        return 1;
    }

    ALOGV("%s: HAL buffer sid: %d bid: %" PRIu64 " w: %u h: %u", __FUNCTION__, halBuf.streamId,
          static_cast<uint64_t>(halBuf.bufferId), halBuf.width, halBuf.height);
    ALOGV("%s: HAL buffer fmt: %x usage: %" PRIx64 " ptr: %p", __FUNCTION__, halBuf.format,
          static_cast<uint64_t>(halBuf.usage), halBuf.bufPtr);
    ALOGV("%s: YV12 buffer %d x %d", __FUNCTION__, mYu12Frame->mWidth, mYu12Frame->mHeight);

    int jpegQuality, thumbQuality;
    Size thumbSize;
    bool outputThumbnail = true;

    if (setting.exists(ANDROID_JPEG_QUALITY)) {
        camera_metadata_ro_entry entry = setting.find(ANDROID_JPEG_QUALITY);
        jpegQuality = entry.data.u8[0];
    } else {
        return lfail("%s: ANDROID_JPEG_QUALITY not set", __FUNCTION__);
    }

    if (setting.exists(ANDROID_JPEG_THUMBNAIL_QUALITY)) {
        camera_metadata_ro_entry entry = setting.find(ANDROID_JPEG_THUMBNAIL_QUALITY);
        thumbQuality = entry.data.u8[0];
    } else {
        return lfail("%s: ANDROID_JPEG_THUMBNAIL_QUALITY not set", __FUNCTION__);
    }

    if (setting.exists(ANDROID_JPEG_THUMBNAIL_SIZE)) {
        camera_metadata_ro_entry entry = setting.find(ANDROID_JPEG_THUMBNAIL_SIZE);
        thumbSize = Size{.width = entry.data.i32[0], .height = entry.data.i32[1]};
        if (thumbSize.width == 0 && thumbSize.height == 0) {
            outputThumbnail = false;
        }
    } else {
        return lfail("%s: ANDROID_JPEG_THUMBNAIL_SIZE not set", __FUNCTION__);
    }

    /* Cropped and scaled YU12 buffer for main and thumbnail */
    YCbCrLayout yu12Main;
    Size jpegSize{halBuf.width, halBuf.height};

    /* Compute temporary buffer sizes accounting for the following:
     * thumbnail can't exceed APP1 size of 64K
     * main image needs to hold APP1, headers, and at most a poorly
     * compressed image */
    const ssize_t maxThumbCodeSize = 64 * 1024;
    const ssize_t maxJpegCodeSize =
            mBlobBufferSize == 0 ? parent->getJpegBufferSize(jpegSize.width, jpegSize.height)
                                 : mBlobBufferSize;

    /* Check that getJpegBufferSize did not return an error */
    if (maxJpegCodeSize < 0) {
        return lfail("%s: getJpegBufferSize returned %zd", __FUNCTION__, maxJpegCodeSize);
    }

    /* Hold actual thumbnail and main image code sizes */
    size_t thumbCodeSize = 0, jpegCodeSize = 0;
    /* Temporary thumbnail code buffer */
    std::vector<uint8_t> thumbCode(outputThumbnail ? maxThumbCodeSize : 0);

    YCbCrLayout yu12Thumb;
    if (outputThumbnail) {
        ret = cropAndScaleThumbLocked(mYu12Frame, thumbSize, &yu12Thumb);

        if (ret != 0) {
            return lfail("%s: crop and scale thumbnail failed!", __FUNCTION__);
        }
    }

    /* Scale and crop main jpeg */
    ret = cropAndScaleLocked(mYu12Frame, jpegSize, &yu12Main);

    if (ret != 0) {
        return lfail("%s: crop and scale main failed!", __FUNCTION__);
    }

    /* Encode the thumbnail image */
    if (outputThumbnail) {
        ret = encodeJpegYU12(thumbSize, yu12Thumb, thumbQuality, 0, 0, &thumbCode[0],
                             maxThumbCodeSize, thumbCodeSize);

        if (ret != 0) {
            return lfail("%s: thumbnail encodeJpegYU12 failed with %d", __FUNCTION__, ret);
        }
    }

    /* Combine camera characteristics with request settings to form EXIF
     * metadata */
    common::V1_0::helper::CameraMetadata meta(mCameraCharacteristics);
    meta.append(setting);

    /* Generate EXIF object */
    std::unique_ptr<ExifUtils> utils(ExifUtils::create());
    /* Make sure it's initialized */
    utils->initialize();

    utils->setFromMetadata(meta, jpegSize.width, jpegSize.height);
    utils->setMake(mExifMake);
    utils->setModel(mExifModel);

    ret = utils->generateApp1(outputThumbnail ? &thumbCode[0] : nullptr, thumbCodeSize);

    if (!ret) {
        return lfail("%s: generating APP1 failed", __FUNCTION__);
    }

    /* Get internal buffer */
    size_t exifDataSize = utils->getApp1Length();
    const uint8_t* exifData = utils->getApp1Buffer();

    /* Lock the HAL jpeg code buffer */
    void* bufPtr = sHandleImporter.lock(*(halBuf.bufPtr), static_cast<uint64_t>(halBuf.usage),
                                        maxJpegCodeSize);

    if (!bufPtr) {
        return lfail("%s: could not lock %zu bytes", __FUNCTION__, maxJpegCodeSize);
    }

    /* Encode the main jpeg image */
    ret = encodeJpegYU12(jpegSize, yu12Main, jpegQuality, exifData, exifDataSize, bufPtr,
                         maxJpegCodeSize, jpegCodeSize);

    /* TODO: Not sure this belongs here, maybe better to pass jpegCodeSize out
     * and do this when returning buffer to parent */
    CameraBlob blob{CameraBlobId::JPEG, static_cast<int32_t>(jpegCodeSize)};
    void* blobDst = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(bufPtr) + maxJpegCodeSize -
                                            sizeof(CameraBlob));
    memcpy(blobDst, &blob, sizeof(CameraBlob));

    /* Unlock the HAL jpeg code buffer */
    int relFence = sHandleImporter.unlock(*(halBuf.bufPtr));
    if (relFence >= 0) {
        halBuf.acquireFence = relFence;
    }

    /* Check if our JPEG actually succeeded */
    if (ret != 0) {
        return lfail("%s: encodeJpegYU12 failed with %d", __FUNCTION__, ret);
    }

    ALOGV("%s: encoded JPEG (ret:%d) with Q:%d max size: %zu", __FUNCTION__, ret, jpegQuality,
          maxJpegCodeSize);

    return 0;
}

void ExternalCameraDeviceSession::OutputThread::clearIntermediateBuffers() {
    std::lock_guard<std::mutex> lk(mBufferLock);
    mYu12Frame.reset();
    mYu12ThumbFrame.reset();
    mIntermediateBuffers.clear();
    mMuteTestPatternFrame.clear();
    mBlobBufferSize = 0;
}

bool ExternalCameraDeviceSession::OutputThread::threadLoop() {
    std::shared_ptr<HalRequest> req;
    auto parent = mParent.lock();
    if (parent == nullptr) {
        ALOGE("%s: session has been disconnected!", __FUNCTION__);
        return false;
    }

    // TODO: maybe we need to setup a sensor thread to dq/enq v4l frames
    //       regularly to prevent v4l buffer queue filled with stale buffers
    //       when app doesn't program a preview request
    waitForNextRequest(&req);
    if (req == nullptr) {
        // No new request, wait again
        return true;
    }

    auto onDeviceError = [&](auto... args) {
        ALOGE(args...);
        parent->notifyError(req->frameNumber, /*stream*/ -1, ErrorCode::ERROR_DEVICE);
        signalRequestDone();
        return false;
    };

    if (req->frameIn->mFourcc != V4L2_PIX_FMT_MJPEG && req->frameIn->mFourcc != V4L2_PIX_FMT_Z16) {
        return onDeviceError("%s: do not support V4L2 format %c%c%c%c", __FUNCTION__,
                             req->frameIn->mFourcc & 0xFF, (req->frameIn->mFourcc >> 8) & 0xFF,
                             (req->frameIn->mFourcc >> 16) & 0xFF,
                             (req->frameIn->mFourcc >> 24) & 0xFF);
    }

    int res = requestBufferStart(req->buffers);
    if (res != 0) {
        ALOGE("%s: send BufferRequest failed! res %d", __FUNCTION__, res);
        return onDeviceError("%s: failed to send buffer request!", __FUNCTION__);
    }

    std::unique_lock<std::mutex> lk(mBufferLock);
    // Convert input V4L2 frame to YU12 of the same size
    // TODO: see if we can save some computation by converting to YV12 here
    uint8_t* inData;
    size_t inDataSize;
    if (req->frameIn->getData(&inData, &inDataSize) != 0) {
        lk.unlock();
        return onDeviceError("%s: V4L2 buffer map failed", __FUNCTION__);
    }

    // Process camera mute state
    auto testPatternMode = req->setting.find(ANDROID_SENSOR_TEST_PATTERN_MODE);
    if (testPatternMode.count == 1) {
        if (mCameraMuted != (testPatternMode.data.u8[0] != ANDROID_SENSOR_TEST_PATTERN_MODE_OFF)) {
            mCameraMuted = !mCameraMuted;
            // Get solid color for test pattern, if any was set
            if (testPatternMode.data.u8[0] == ANDROID_SENSOR_TEST_PATTERN_MODE_SOLID_COLOR) {
                auto entry = req->setting.find(ANDROID_SENSOR_TEST_PATTERN_DATA);
                if (entry.count == 4) {
                    // Update the mute frame if the pattern color has changed
                    if (memcmp(entry.data.i32, mTestPatternData, sizeof(mTestPatternData)) != 0) {
                        memcpy(mTestPatternData, entry.data.i32, sizeof(mTestPatternData));
                        // Fill the mute frame with the solid color, use only 8 MSB of RGGB as RGB
                        for (int i = 0; i < mMuteTestPatternFrame.size(); i += 3) {
                            mMuteTestPatternFrame[i] = entry.data.i32[0] >> 24;
                            mMuteTestPatternFrame[i + 1] = entry.data.i32[1] >> 24;
                            mMuteTestPatternFrame[i + 2] = entry.data.i32[3] >> 24;
                        }
                    }
                }
            }
        }
    }

    // TODO: in some special case maybe we can decode jpg directly to gralloc output?
    if (req->frameIn->mFourcc == V4L2_PIX_FMT_MJPEG) {
        ATRACE_BEGIN("MJPGtoI420");
        res = 0;
        if (mCameraMuted) {
            res = libyuv::ConvertToI420(
                    mMuteTestPatternFrame.data(), mMuteTestPatternFrame.size(),
                    static_cast<uint8_t*>(mYu12FrameLayout.y), mYu12FrameLayout.yStride,
                    static_cast<uint8_t*>(mYu12FrameLayout.cb), mYu12FrameLayout.cStride,
                    static_cast<uint8_t*>(mYu12FrameLayout.cr), mYu12FrameLayout.cStride, 0, 0,
                    mYu12Frame->mWidth, mYu12Frame->mHeight, mYu12Frame->mWidth,
                    mYu12Frame->mHeight, libyuv::kRotate0, libyuv::FOURCC_RAW);
        } else {
            res = libyuv::MJPGToI420(
                    inData, inDataSize, static_cast<uint8_t*>(mYu12FrameLayout.y),
                    mYu12FrameLayout.yStride, static_cast<uint8_t*>(mYu12FrameLayout.cb),
                    mYu12FrameLayout.cStride, static_cast<uint8_t*>(mYu12FrameLayout.cr),
                    mYu12FrameLayout.cStride, mYu12Frame->mWidth, mYu12Frame->mHeight,
                    mYu12Frame->mWidth, mYu12Frame->mHeight);
        }
        ATRACE_END();

        if (res != 0) {
            // For some webcam, the first few V4L2 frames might be malformed...
            ALOGE("%s: Convert V4L2 frame to YU12 failed! res %d", __FUNCTION__, res);

            ATRACE_BEGIN("Wait for BufferRequest done");
            res = waitForBufferRequestDone(&req->buffers);
            ATRACE_END();

            lk.unlock();
            Status st = parent->processCaptureRequestError(req);
            if (st != Status::OK) {
                return onDeviceError("%s: failed to process capture request error!", __FUNCTION__);
            }
            signalRequestDone();
            return true;
        }
    }

    ATRACE_BEGIN("Wait for BufferRequest done");
    res = waitForBufferRequestDone(&req->buffers);
    ATRACE_END();

    if (res != 0) {
        // HAL buffer management buffer request can fail
        ALOGE("%s: wait for BufferRequest done failed! res %d", __FUNCTION__, res);
        lk.unlock();
        Status st = parent->processCaptureRequestError(req);
        if (st != Status::OK) {
            return onDeviceError("%s: failed to process capture request error!", __FUNCTION__);
        }
        signalRequestDone();
        return true;
    }

    ALOGV("%s processing new request", __FUNCTION__);
    const int kSyncWaitTimeoutMs = 500;
    for (auto& halBuf : req->buffers) {
        if (*(halBuf.bufPtr) == nullptr) {
            ALOGW("%s: buffer for stream %d missing", __FUNCTION__, halBuf.streamId);
            halBuf.fenceTimeout = true;
        } else if (halBuf.acquireFence >= 0) {
            int ret = sync_wait(halBuf.acquireFence, kSyncWaitTimeoutMs);
            if (ret) {
                halBuf.fenceTimeout = true;
            } else {
                ::close(halBuf.acquireFence);
                halBuf.acquireFence = -1;
            }
        }

        if (halBuf.fenceTimeout) {
            continue;
        }

        // Gralloc lockYCbCr the buffer
        switch (halBuf.format) {
            case PixelFormat::BLOB: {
                int ret = createJpegLocked(halBuf, req->setting);

                if (ret != 0) {
                    lk.unlock();
                    return onDeviceError("%s: createJpegLocked failed with %d", __FUNCTION__, ret);
                }
            } break;
            case PixelFormat::Y16: {
                void* outLayout = sHandleImporter.lock(
                        *(halBuf.bufPtr), static_cast<uint64_t>(halBuf.usage), inDataSize);

                std::memcpy(outLayout, inData, inDataSize);

                int relFence = sHandleImporter.unlock(*(halBuf.bufPtr));
                if (relFence >= 0) {
                    halBuf.acquireFence = relFence;
                }
            } break;
            case PixelFormat::YCBCR_420_888:
            case PixelFormat::YV12: {
                android::Rect outRect{0, 0, static_cast<int32_t>(halBuf.width),
                                      static_cast<int32_t>(halBuf.height)};
                android_ycbcr result = sHandleImporter.lockYCbCr(
                        *(halBuf.bufPtr), static_cast<uint64_t>(halBuf.usage), outRect);
                ALOGV("%s: outLayout y %p cb %p cr %p y_str %zu c_str %zu c_step %zu", __FUNCTION__,
                      result.y, result.cb, result.cr, result.ystride, result.cstride,
                      result.chroma_step);
                if (result.ystride > UINT32_MAX || result.cstride > UINT32_MAX ||
                    result.chroma_step > UINT32_MAX) {
                    return onDeviceError("%s: lockYCbCr failed. Unexpected values!", __FUNCTION__);
                }
                YCbCrLayout outLayout = {.y = result.y,
                                         .cb = result.cb,
                                         .cr = result.cr,
                                         .yStride = static_cast<uint32_t>(result.ystride),
                                         .cStride = static_cast<uint32_t>(result.cstride),
                                         .chromaStep = static_cast<uint32_t>(result.chroma_step)};

                // Convert to output buffer size/format
                uint32_t outputFourcc = getFourCcFromLayout(outLayout);
                ALOGV("%s: converting to format %c%c%c%c", __FUNCTION__, outputFourcc & 0xFF,
                      (outputFourcc >> 8) & 0xFF, (outputFourcc >> 16) & 0xFF,
                      (outputFourcc >> 24) & 0xFF);

                YCbCrLayout cropAndScaled;
                ATRACE_BEGIN("cropAndScaleLocked");
                int ret = cropAndScaleLocked(mYu12Frame, Size{halBuf.width, halBuf.height},
                                             &cropAndScaled);
                ATRACE_END();
                if (ret != 0) {
                    lk.unlock();
                    return onDeviceError("%s: crop and scale failed!", __FUNCTION__);
                }

                Size sz{halBuf.width, halBuf.height};
                ATRACE_BEGIN("formatConvert");
                ret = formatConvert(cropAndScaled, outLayout, sz, outputFourcc);
                ATRACE_END();
                if (ret != 0) {
                    lk.unlock();
                    return onDeviceError("%s: format conversion failed!", __FUNCTION__);
                }
                int relFence = sHandleImporter.unlock(*(halBuf.bufPtr));
                if (relFence >= 0) {
                    halBuf.acquireFence = relFence;
                }
            } break;
            default:
                lk.unlock();
                return onDeviceError("%s: unknown output format %x", __FUNCTION__, halBuf.format);
        }
    }  // for each buffer
    mScaledYu12Frames.clear();

    // Don't hold the lock while calling back to parent
    lk.unlock();
    Status st = parent->processCaptureResult(req);
    if (st != Status::OK) {
        return onDeviceError("%s: failed to process capture result!", __FUNCTION__);
    }
    signalRequestDone();
    return true;
}

// End ExternalCameraDeviceSession::OutputThread functions

}  // namespace implementation
}  // namespace device
}  // namespace camera
}  // namespace hardware
}  // namespace android
