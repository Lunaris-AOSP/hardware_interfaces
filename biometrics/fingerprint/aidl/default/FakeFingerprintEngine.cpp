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

#include "FakeFingerprintEngine.h"
#include <regex>
#include "Fingerprint.h"

#include <android-base/logging.h>
#include <android-base/parseint.h>

#include <fingerprint.sysprop.h>

#include "util/CancellationSignal.h"
#include "util/Util.h"

using namespace ::android::fingerprint::virt;
using ::android::base::ParseInt;

namespace aidl::android::hardware::biometrics::fingerprint {

FakeFingerprintEngine::FakeFingerprintEngine()
    : mRandom(std::mt19937::default_seed),
      mWorkMode(WorkMode::kIdle),
      isLockoutTimerSupported(true) {}

void FakeFingerprintEngine::generateChallengeImpl(ISessionCallback* cb) {
    BEGIN_OP(0);
    std::uniform_int_distribution<int64_t> dist;
    auto challenge = dist(mRandom);
    Fingerprint::cfg().set<std::int64_t>("challenge", challenge);
    cb->onChallengeGenerated(challenge);
}

void FakeFingerprintEngine::revokeChallengeImpl(ISessionCallback* cb, int64_t challenge) {
    BEGIN_OP(0);
    Fingerprint::cfg().setopt<OptInt64>("challenge", std::nullopt);
    cb->onChallengeRevoked(challenge);
}

void FakeFingerprintEngine::enrollImpl(ISessionCallback* cb,
                                       const keymaster::HardwareAuthToken& hat,
                                       const std::future<void>& cancel) {
    BEGIN_OP(0);

    // Do proper HAT verification in the real implementation.
    if (hat.mac.empty()) {
        LOG(ERROR) << "Fail: hat";
        cb->onError(Error::UNABLE_TO_PROCESS, 0 /* vendorError */);
        return;
    }

    waitForFingerDown(cb, cancel);

    updateContext(WorkMode::kEnroll, cb, const_cast<std::future<void>&>(cancel), 0, hat);
}

void FakeFingerprintEngine::authenticateImpl(ISessionCallback* cb, int64_t operationId,
                                             const std::future<void>& cancel) {
    BEGIN_OP(0);

    waitForFingerDown(cb, cancel);

    updateContext(WorkMode::kAuthenticate, cb, const_cast<std::future<void>&>(cancel), operationId,
                  keymaster::HardwareAuthToken());
}

void FakeFingerprintEngine::detectInteractionImpl(ISessionCallback* cb,
                                                  const std::future<void>& cancel) {
    BEGIN_OP(0);

    auto detectInteractionSupported = Fingerprint::cfg().get<bool>("detect_interaction");
    if (!detectInteractionSupported) {
        LOG(ERROR) << "Detect interaction is not supported";
        cb->onError(Error::UNABLE_TO_PROCESS, 0 /* vendorError */);
        return;
    }

    waitForFingerDown(cb, cancel);

    updateContext(WorkMode::kDetectInteract, cb, const_cast<std::future<void>&>(cancel), 0,
                  keymaster::HardwareAuthToken());
}

void FakeFingerprintEngine::updateContext(WorkMode mode, ISessionCallback* cb,
                                          std::future<void>& cancel, int64_t operationId,
                                          const keymaster::HardwareAuthToken& hat) {
    mCancel = std::move(cancel);
    mWorkMode = mode;
    mCb = cb;
    mOperationId = operationId;
    mHat = hat;
}

void FakeFingerprintEngine::fingerDownAction() {
    bool isTerminal = false;
    LOG(INFO) << __func__;
    switch (mWorkMode) {
        case WorkMode::kAuthenticate:
            isTerminal = onAuthenticateFingerDown(mCb, mOperationId, mCancel);
            break;
        case WorkMode::kEnroll:
            isTerminal = onEnrollFingerDown(mCb, mHat, mCancel);
            break;
        case WorkMode::kDetectInteract:
            isTerminal = onDetectInteractFingerDown(mCb, mCancel);
            break;
        default:
            LOG(WARNING) << "unexpected mode: on fingerDownAction(), " << (int)mWorkMode;
            break;
    }

    if (isTerminal) {
        mWorkMode = WorkMode::kIdle;
    }
}

bool FakeFingerprintEngine::onEnrollFingerDown(ISessionCallback* cb,
                                               const keymaster::HardwareAuthToken&,
                                               const std::future<void>& cancel) {
    BEGIN_OP(getLatency(Fingerprint::cfg().getopt<OptIntVec>("operation_enroll_latency")));

    // Force error-out
    auto err = Fingerprint::cfg().get<std::int32_t>("operation_enroll_error");
    if (err != 0) {
        LOG(ERROR) << "Fail: operation_enroll_error";
        auto ec = convertError(err);
        cb->onError(ec.first, ec.second);
        return true;
    }

    // Format is "<id>:<progress_ms-[acquiredInfo..]>,...:<result>
    auto nextEnroll = Fingerprint::cfg().get<std::string>("next_enrollment");
    auto parts = Util::split(nextEnroll, ":");
    if (parts.size() != 3) {
        LOG(ERROR) << "Fail: invalid next_enrollment:" << nextEnroll;
        cb->onError(Error::VENDOR, 0 /* vendorError */);
        return true;
    }
    auto enrollmentId = std::stoi(parts[0]);
    auto progress = Util::parseEnrollmentCapture(parts[1]);
    for (size_t i = 0; i < progress.size(); i += 2) {
        auto left = (progress.size() - i) / 2 - 1;
        auto duration = progress[i][0];
        auto acquired = progress[i + 1];
        auto N = acquired.size();

        for (int j = 0; j < N; j++) {
            SLEEP_MS(duration / N);

            if (shouldCancel(cancel)) {
                LOG(ERROR) << "Fail: cancel";
                cb->onError(Error::CANCELED, 0 /* vendorCode */);
                return true;
            }
            auto ac = convertAcquiredInfo(acquired[j]);
            cb->onAcquired(ac.first, ac.second);
        }

        if (left == 0 && !IS_TRUE(parts[2])) {  // end and failed
            LOG(ERROR) << "Fail: requested by caller: " << nextEnroll;
            Fingerprint::cfg().set<std::string>("next_enrollment", "");
            cb->onError(Error::UNABLE_TO_PROCESS, 0 /* vendorCode */);
        } else {  // progress and update props if last time
            LOG(INFO) << "onEnroll: " << enrollmentId << " left: " << left;
            if (left == 0) {
                auto enrollments = Fingerprint::cfg().getopt<OptIntVec>("enrollments");
                enrollments.emplace_back(enrollmentId);
                Fingerprint::cfg().setopt<OptIntVec>("enrollments", enrollments);
                Fingerprint::cfg().setopt<OptString>("next_enrollment", std::nullopt);
                // change authenticatorId after new enrollment
                auto id = Fingerprint::cfg().get<std::int64_t>("authenticator_id");
                auto newId = id + 1;
                Fingerprint::cfg().set<std::int64_t>("authenticator_id", newId);
                LOG(INFO) << "Enrolled: " << enrollmentId;
            }
            cb->onEnrollmentProgress(enrollmentId, left);
        }
    }

    return true;
}

bool FakeFingerprintEngine::onAuthenticateFingerDown(ISessionCallback* cb,
                                                     int64_t /* operationId */,
                                                     const std::future<void>& cancel) {
    BEGIN_OP(getLatency(Fingerprint::cfg().getopt<OptIntVec>("operation_authenticate_latency")));

    int64_t now = Util::getSystemNanoTime();
    int64_t duration = Fingerprint::cfg().get<std::int32_t>("operation_authenticate_duration");
    auto acquired = Fingerprint::cfg().get<std::string>("operation_authenticate_acquired");
    auto acquiredInfos = Util::parseIntSequence(acquired);
    int N = acquiredInfos.size();

    if (N == 0) {
        LOG(ERROR) << "Fail to parse authentiate acquired info: " + acquired;
        cb->onError(Error::UNABLE_TO_PROCESS, 0 /* vendorError */);
        return true;
    }

    // got lockout?
    if (checkSensorLockout(cb)) {
        return FakeLockoutTracker::LockoutMode::kPermanent == mLockoutTracker.getMode();
    }

    int i = 0;
    do {
        if (Fingerprint::cfg().get<bool>("operation_authenticate_fails")) {
            LOG(ERROR) << "Fail: operation_authenticate_fails";
            mLockoutTracker.addFailedAttempt();
            cb->onAuthenticationFailed();
            return false;
        }

        auto err = Fingerprint::cfg().get<std::int32_t>("operation_authenticate_error");
        if (err != 0) {
            LOG(ERROR) << "Fail: operation_authenticate_error";
            auto ec = convertError(err);
            cb->onError(ec.first, ec.second);
            return true; /* simply terminating current operation for any user inserted error,
                            revisit if tests need*/
        }

        if (Fingerprint::cfg().get<bool>("lockout")) {
            LOG(ERROR) << "Fail: lockout";
            cb->onLockoutPermanent();
            cb->onError(Error::HW_UNAVAILABLE, 0 /* vendorError */);
            return true;
        }

        if (shouldCancel(cancel)) {
            LOG(ERROR) << "Fail: cancel";
            cb->onError(Error::CANCELED, 0 /* vendorCode */);
            return true;
        }

        if (i < N) {
            auto ac = convertAcquiredInfo(acquiredInfos[i]);
            cb->onAcquired(ac.first, ac.second);
            i++;
        }

        SLEEP_MS(duration / N);
    } while (!Util::hasElapsed(now, duration));

    auto id = Fingerprint::cfg().get<std::int32_t>("enrollment_hit");
    auto enrolls = Fingerprint::cfg().getopt<OptIntVec>("enrollments");
    auto isEnrolled = std::find(enrolls.begin(), enrolls.end(), id) != enrolls.end();
    if (id > 0 && isEnrolled) {
        cb->onAuthenticationSucceeded(id, {} /* hat */);
        mLockoutTracker.reset();
        return true;
    } else {
        LOG(ERROR) << "Fail: fingerprint not enrolled";
        cb->onAuthenticationFailed();
        mLockoutTracker.addFailedAttempt();
        checkSensorLockout(cb);
        return false;
    }
}

bool FakeFingerprintEngine::onDetectInteractFingerDown(ISessionCallback* cb,
                                                       const std::future<void>& cancel) {
    BEGIN_OP(getLatency(
            Fingerprint::cfg().getopt<OptIntVec>("operation_detect_interaction_latency")));

    int32_t duration =
            Fingerprint::cfg().get<std::int32_t>("operation_detect_interaction_duration");

    auto acquired = Fingerprint::cfg().get<std::string>("operation_detect_interaction_acquired");
    auto acquiredInfos = Util::parseIntSequence(acquired);
    int N = acquiredInfos.size();
    int64_t now = Util::getSystemNanoTime();

    if (N == 0) {
        LOG(ERROR) << "Fail to parse detect interaction acquired info: " + acquired;
        cb->onError(Error::UNABLE_TO_PROCESS, 0 /* vendorError */);
        return true;
    }

    int i = 0;
    do {
        auto err = Fingerprint::cfg().get<std::int32_t>("operation_detect_interaction_error");
        if (err != 0) {
            LOG(ERROR) << "Fail: operation_detect_interaction_error";
            auto ec = convertError(err);
            cb->onError(ec.first, ec.second);
            return true;
        }

        if (shouldCancel(cancel)) {
            LOG(ERROR) << "Fail: cancel";
            cb->onError(Error::CANCELED, 0 /* vendorCode */);
            return true;
        }

        if (i < N) {
            auto ac = convertAcquiredInfo(acquiredInfos[i]);
            cb->onAcquired(ac.first, ac.second);
            i++;
        }
        SLEEP_MS(duration / N);
    } while (!Util::hasElapsed(now, duration));

    cb->onInteractionDetected();

    return true;
}

void FakeFingerprintEngine::enumerateEnrollmentsImpl(ISessionCallback* cb) {
    BEGIN_OP(0);

    std::vector<int32_t> ids;
    for (auto& enrollment : Fingerprint::cfg().getopt<OptIntVec>("enrollments")) {
        auto id = enrollment.value_or(0);
        if (id > 0) {
            ids.push_back(id);
        }
    }

    cb->onEnrollmentsEnumerated(ids);
}

void FakeFingerprintEngine::removeEnrollmentsImpl(ISessionCallback* cb,
                                                  const std::vector<int32_t>& enrollmentIds) {
    BEGIN_OP(0);

    std::vector<std::optional<int32_t>> newEnrollments;
    std::vector<int32_t> removed;
    for (auto& enrollment : Fingerprint::cfg().getopt<OptIntVec>("enrollments")) {
        auto id = enrollment.value_or(0);
        if (std::find(enrollmentIds.begin(), enrollmentIds.end(), id) != enrollmentIds.end()) {
            removed.push_back(id);
        } else if (id > 0) {
            newEnrollments.emplace_back(id);
        }
    }
    Fingerprint::cfg().setopt<OptIntVec>("enrollments", newEnrollments);

    cb->onEnrollmentsRemoved(enrollmentIds);
}

void FakeFingerprintEngine::getAuthenticatorIdImpl(ISessionCallback* cb) {
    BEGIN_OP(0);
    int64_t authenticatorId;
    if (Fingerprint::cfg().getopt<OptIntVec>("enrollments").size() == 0) {
        authenticatorId = 0;
    } else {
        authenticatorId = Fingerprint::cfg().get<std::int64_t>("authenticator_id");
        if (authenticatorId == 0) authenticatorId = 1;
    }
    cb->onAuthenticatorIdRetrieved(authenticatorId);
}

void FakeFingerprintEngine::invalidateAuthenticatorIdImpl(ISessionCallback* cb) {
    BEGIN_OP(0);
    int64_t newId;
    if (Fingerprint::cfg().getopt<OptIntVec>("enrollments").size() == 0) {
        newId = 0;
    } else {
        auto id = Fingerprint::cfg().get<std::int64_t>("authenticator_id");
        newId = id + 1;
    }
    Fingerprint::cfg().set<std::int64_t>("authenticator_id", newId);
    cb->onAuthenticatorIdInvalidated(newId);
}

void FakeFingerprintEngine::resetLockoutImpl(ISessionCallback* cb,
                                             const keymaster::HardwareAuthToken& hat) {
    BEGIN_OP(0);
    if (hat.mac.empty()) {
        LOG(ERROR) << "Fail: hat in resetLockout()";
        cb->onError(Error::UNABLE_TO_PROCESS, 0 /* vendorError */);
        return;
    }
    clearLockout(cb);
    if (isLockoutTimerStarted) isLockoutTimerAborted = true;
}

void FakeFingerprintEngine::clearLockout(ISessionCallback* cb, bool dueToTimeout) {
    Fingerprint::cfg().set<bool>("lockout", false);
    cb->onLockoutCleared();
    mLockoutTracker.reset(dueToTimeout);
}

ndk::ScopedAStatus FakeFingerprintEngine::onPointerDownImpl(int32_t /*pointerId*/, int32_t /*x*/,
                                                            int32_t /*y*/, float /*minor*/,
                                                            float /*major*/) {
    BEGIN_OP(0);
    fingerDownAction();
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus FakeFingerprintEngine::onPointerUpImpl(int32_t /*pointerId*/) {
    BEGIN_OP(0);
    mFingerIsDown = false;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus FakeFingerprintEngine::onUiReadyImpl() {
    BEGIN_OP(0);
    return ndk::ScopedAStatus::ok();
}

bool FakeFingerprintEngine::getSensorLocationConfig(std::vector<SensorLocation>& out) {
    auto locStr = Fingerprint::cfg().get<std::string>("sensor_location");
    auto isValidStr = false;

    // sensor_location format: x:y:r:d,x:y:r:d,...
    //   x: x location in pixel, y: y location in pixel, r: radus in pixel, d:display in string
    auto locations = Util::split(locStr, ",");
    for (int i = 0; i < locations.size(); i++) {
        auto loc = locations[i];

        // expect loc in the format: x:y:r  or x:y:d:r
        auto dim = Util::split(loc, ":");

        if (dim.size() < 3) {
            if (!loc.empty()) LOG(WARNING) << "Invalid sensor location input (x:y:radius):" + loc;
            out.clear();
            return false;
        } else {
            int32_t x, y, r;
            std::string d = "";
            if (dim.size() >= 3) {
                isValidStr = ParseInt(dim[0], &x) && ParseInt(dim[1], &y) && ParseInt(dim[2], &r);
            }
            if (dim.size() >= 4) {
                for (int i = 3; i < dim.size(); i++) {
                    if (i > 3) d += ':';
                    d += dim[i];
                }
            }
            if (isValidStr) {
                out.push_back(SensorLocation{.sensorLocationX = x,
                                             .sensorLocationY = y,
                                             .sensorRadius = r,
                                             .display = d});
            }
        }
    }

    LOG(INFO) << "getSensorLocationConfig: isValidStr=" << isValidStr << " locStr=" << locStr;
    if (isValidStr) {
        for (auto loc : out) {
            LOG(INFO) << loc.toString();
        }
    } else {
        out.clear();
    }

    return isValidStr;
}

void FakeFingerprintEngine::getSensorLocation(std::vector<SensorLocation>& location) {
    if (!getSensorLocationConfig(location)) {
        getDefaultSensorLocation(location);
    }
}

std::pair<AcquiredInfo, int32_t> FakeFingerprintEngine::convertAcquiredInfo(int32_t code) {
    std::pair<AcquiredInfo, int32_t> res;
    if (code > FINGERPRINT_ACQUIRED_VENDOR_BASE) {
        res.first = AcquiredInfo::VENDOR;
        res.second = code - FINGERPRINT_ACQUIRED_VENDOR_BASE;
    } else {
        res.first = (AcquiredInfo)code;
        res.second = 0;
    }
    return res;
}

std::pair<Error, int32_t> FakeFingerprintEngine::convertError(int32_t code) {
    std::pair<Error, int32_t> res;
    if (code > FINGERPRINT_ERROR_VENDOR_BASE) {
        res.first = Error::VENDOR;
        res.second = code - FINGERPRINT_ERROR_VENDOR_BASE;
    } else {
        res.first = (Error)code;
        res.second = 0;
    }
    return res;
}

int32_t FakeFingerprintEngine::getLatency(
        const std::vector<std::optional<std::int32_t>>& latencyIn) {
    int32_t res = DEFAULT_LATENCY;

    std::vector<int32_t> latency;
    for (auto x : latencyIn)
        if (x.has_value()) latency.push_back(*x);

    switch (latency.size()) {
        case 0:
            break;
        case 1:
            res = latency[0];
            break;
        case 2:
            res = getRandomInRange(latency[0], latency[1]);
            break;
        default:
            LOG(ERROR) << "ERROR: unexpected input of size " << latency.size();
            break;
    }

    return res;
}

int32_t FakeFingerprintEngine::getRandomInRange(int32_t bound1, int32_t bound2) {
    std::uniform_int_distribution<int32_t> dist(std::min(bound1, bound2), std::max(bound1, bound2));
    return dist(mRandom);
}

bool FakeFingerprintEngine::checkSensorLockout(ISessionCallback* cb) {
    FakeLockoutTracker::LockoutMode lockoutMode = mLockoutTracker.getMode();
    if (lockoutMode == FakeLockoutTracker::LockoutMode::kPermanent) {
        LOG(ERROR) << "Fail: lockout permanent";
        cb->onLockoutPermanent();
        isLockoutTimerAborted = true;
        return true;
    } else if (lockoutMode == FakeLockoutTracker::LockoutMode::kTimed) {
        int64_t timeLeft = mLockoutTracker.getLockoutTimeLeft();
        LOG(ERROR) << "Fail: lockout timed " << timeLeft;
        cb->onLockoutTimed(timeLeft);
        if (isLockoutTimerSupported && !isLockoutTimerStarted) startLockoutTimer(timeLeft, cb);
        return true;
    }
    return false;
}

void FakeFingerprintEngine::startLockoutTimer(int64_t timeout, ISessionCallback* cb) {
    BEGIN_OP(0);
    std::function<void(ISessionCallback*)> action =
            std::bind(&FakeFingerprintEngine::lockoutTimerExpired, this, std::placeholders::_1);
    std::thread([timeout, action, cb]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(timeout));
        action(cb);
    }).detach();

    isLockoutTimerStarted = true;
}
void FakeFingerprintEngine::lockoutTimerExpired(ISessionCallback* cb) {
    BEGIN_OP(0);
    if (!isLockoutTimerAborted) {
        clearLockout(cb, true);
    }
    isLockoutTimerStarted = false;
    isLockoutTimerAborted = false;
}

void FakeFingerprintEngine::waitForFingerDown(ISessionCallback* cb,
                                              const std::future<void>& cancel) {
    if (mFingerIsDown) {
        LOG(WARNING) << "waitForFingerDown: mFingerIsDown==true already!";
    }

    while (!mFingerIsDown) {
        if (shouldCancel(cancel)) {
            LOG(ERROR) << "waitForFingerDown, Fail: cancel";
            cb->onError(Error::CANCELED, 0 /* vendorCode */);
            return;
        }
        SLEEP_MS(10);
    }
}

}  // namespace aidl::android::hardware::biometrics::fingerprint
