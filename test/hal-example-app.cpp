/*
 * Copyright (C) 2017 The Android Open Source Project
 * Copyright (C) 2019 STMicroelectronics
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

#include <iostream>
#include <string>

#include <android-base/logging.h>
#include <android/hardware/light/2.0/ILight.h>

void logInfo(const std::string& msg) {
    LOG(INFO) << msg;
    std::cout << msg << std::endl;
}

void logError(const std::string& msg) {
    LOG(ERROR) << msg;
    std::cout << msg << std::endl;
}

int main() {
    using ::android::hardware::hidl_vec;
    using ::android::hardware::light::V2_0::Brightness;
    using ::android::hardware::light::V2_0::Flash;
    using ::android::hardware::light::V2_0::ILight;
    using ::android::hardware::light::V2_0::LightState;
    using ::android::hardware::light::V2_0::Status;
    using ::android::hardware::light::V2_0::Type;
    using ::android::sp;

    sp<ILight> service = ILight::getService();
    if (service == nullptr) {
        logError("Could not retrieve light service.");
        return -1;
    }

    const static LightState onOrange = {
        .color = 0xFFFFA500, .flashMode = Flash::NONE, .brightnessMode = Brightness::USER,
    };

    const static LightState onHeartbeat = {
        .color = 0xFFFFB500, .flashMode = Flash::TIMED, .brightnessMode = Brightness::USER, .flashOnMs = 2000, .flashOffMs = 2000,
    };

    const static LightState offOrange = {
        .color = 0u, .flashMode = Flash::NONE, .brightnessMode = Brightness::USER,
    };

    const static LightState offHeartbeat = {
        .color = 0u, .flashMode = Flash::NONE, .brightnessMode = Brightness::USER,
    };

    logInfo("Turn ON Orange light");
    service->setLight(Type::NOTIFICATIONS, onOrange);
    sleep(1);
    logInfo("Turn ON Blink Blue light");
    service->setLight(Type::ATTENTION, onHeartbeat);
    sleep(6);

    logInfo("Turn OFF both light");
    service->setLight(Type::NOTIFICATIONS, offOrange);
    service->setLight(Type::ATTENTION, offHeartbeat);

    return 0;
}
