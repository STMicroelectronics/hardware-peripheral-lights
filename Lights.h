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
#include "LightsFlash.h"

#include <aidl/android/hardware/light/BnLights.h>

namespace aidl {
namespace android {
namespace hardware {
namespace light {

using ::ndk::ScopedAStatus;
using ::aidl::android::hardware::light::BnLights;
using ::aidl::android::hardware::light::HwLight;
using ::aidl::android::hardware::light::HwLightState;
using ::aidl::android::hardware::light::LightType;
using ::aidl::android::hardware::light::FlashMode;

using ::aidl::android::hardware::light::LightsFlash;
using ::aidl::android::hardware::light::LightsUtils;

struct HwLightConfig {
  HwLight hwLight;
  FlashMode flashMode;
  LightsFlash* lightsFlash;
  pthread_mutex_t writeMutex;
};

class Lights : public BnLights {
    private:
        std::vector<HwLightConfig> availableLights;
        int checkFlashParams(const HwLightState& state);
        void addLight(LightType const type, int const ordinal);
    public:
        Lights();
        ScopedAStatus setLightState(int id, const HwLightState& state) override;
        ScopedAStatus getLights(std::vector<HwLight>* types) override;
};

}  // namespace light
}  // namespace hardware
}  // namespace android
}  // namespace aidl
