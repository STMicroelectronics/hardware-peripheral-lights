/*
 * Copyright (C) 2020 The Android Open Source Project
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

#pragma once

#include "LightsUtils.h"

#include <aidl/android/hardware/light/BnLights.h>

namespace aidl {
namespace android {
namespace hardware {
namespace light {

using ::ndk::ScopedAStatus;
using ::aidl::android::hardware::light::HwLight;
using ::aidl::android::hardware::light::HwLightState;
using ::aidl::android::hardware::light::LightType;

using ::aidl::android::hardware::light::LightsUtils;

enum LightsFlashState { UNKNOWN, INITIALIZED, STARTED, STOPPED };

class LightsFlash {
    private:
        LightsFlashState mState = LightsFlashState::UNKNOWN;
        HwLight mHwLight;
        HwLightState mHwLightState;
        pthread_t mFlashThread;
        pthread_cond_t mFlashCond;
        pthread_mutex_t mFlashSignalMutex;

        int initLightSyncResources();
        void setTimestamp(struct timespec *out, int64_t target_ns);
        int64_t getTimestampMonotonic();
    public:
        LightsFlash(HwLight light);
        ~LightsFlash();
        void setLightState(HwLightState state);
        int start();
        void stop();
        void flashRoutine();
};

}  // namespace light
}  // namespace hardware
}  // namespace android
}  // namespace aidl
