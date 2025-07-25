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

#include "util/CancellationSignal.h"

#include <android-base/logging.h>
#include <chrono>

namespace aidl::android::hardware::biometrics {

CancellationSignal::CancellationSignal(std::promise<void>&& cancellationPromise)
    : mCancellationPromise(std::move(cancellationPromise)), isSet(false) {}

ndk::ScopedAStatus CancellationSignal::cancel() {
    if (!isSet) {
        mCancellationPromise.set_value();
        isSet = true;
    }
    return ndk::ScopedAStatus::ok();
}

bool shouldCancel(const std::future<void>& f) {
    CHECK(f.valid());
    return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

}  // namespace aidl::android::hardware::biometrics
