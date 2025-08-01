/*
 * Copyright 2022, The Android Open Source Project
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

#include "CanController.h"

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

using namespace std::string_literals;
using namespace android;
using ::aidl::android::hardware::automotive::can::CanController;

int main() {
    base::SetDefaultTag("CanController");
    base::SetMinimumLogSeverity(base::VERBOSE);

    LOG(VERBOSE) << "Starting up...";
    auto service = ndk::SharedRefBase::make<CanController>();
    const auto instance = CanController::descriptor + "/default"s;
    const auto status = AServiceManager_addService(service->asBinder().get(), instance.c_str());
    CHECK_EQ(status, STATUS_OK) << "Failed to add service " << instance;
    LOG(VERBOSE) << "Started successfully!";

    ABinderProcess_joinThreadPool();
    LOG(FATAL) << "CanController exited unexpectedly!";
    return EXIT_FAILURE;
}
