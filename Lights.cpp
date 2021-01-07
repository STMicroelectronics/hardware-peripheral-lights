/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vector>

#include "Lights.h"

#include <android-base/logging.h>

namespace aidl {
namespace android {
namespace hardware {
namespace light {

static int64_t const ONE_MS_IN_NS = 1000000LL;

Lights::Lights() {
    // Add one light by type in list
    addLight(LightType::BACKLIGHT, 0);
    addLight(LightType::KEYBOARD, 0);
    addLight(LightType::BUTTONS, 0);
    addLight(LightType::BATTERY, 0);
    addLight(LightType::NOTIFICATIONS, 0);
    addLight(LightType::ATTENTION, 0);
    addLight(LightType::BLUETOOTH, 0);
    addLight(LightType::WIFI, 0);
    addLight(LightType::MICROPHONE, 0);
}

ScopedAStatus Lights::setLightState(int id, const HwLightState& state) {

    LOG(INFO) << "Lights setting state for id=" << id
              << " to color " << std::hex << state.color
              << " with flash mode " << LightsUtils::getFlashModeName(state.flashMode);

    if (!(0 <= id && id < availableLights.size())) {
        LOG(ERROR) << "Light id " << (int32_t)id << " does not exist.";
        return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }

    if (state.brightnessMode == BrightnessMode::LOW_PERSISTENCE) {
        LOG(ERROR) << "Light brightness mode LOW PERSISTENCE not managed";
        return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }

    HwLightConfig* config = &availableLights[id];

    pthread_mutex_lock(&config->writeMutex);

    // Manage backlight specific case
    if (config->hwLight.type == LightType::BACKLIGHT) {
        int ret = LightsUtils::setBacklightValue(state.color);
        pthread_mutex_unlock(&config->writeMutex);
        if (ret < 0) {
            return ScopedAStatus::fromExceptionCode(EX_TRANSACTION_FAILED);
        } else {
            return ScopedAStatus::ok();
        }
    }

    const char* name = LightsUtils::getLedName(config->hwLight.type);
    if (name == nullptr) {
        // no led associated to required type, stub
        pthread_mutex_unlock(&config->writeMutex);
        return ScopedAStatus::ok();
    }

    if (config->flashMode == FlashMode::TIMED) {
        /* destroy flashing thread */
        config->flashMode = FlashMode::NONE;
        if (config->lightsFlash != nullptr) {
            config->lightsFlash->stop();
        }
    }

    int ret = 0;

    if (state.flashMode != FlashMode::TIMED) {
        ret = LightsUtils::setColorValue(name, state.color, state.flashMode == FlashMode::HARDWARE);
        if (ret < 0) {
            pthread_mutex_unlock(&config->writeMutex);
            return ScopedAStatus::fromExceptionCode(EX_TRANSACTION_FAILED);
        }
    } else {
        /* start flashing thread */
        if (checkFlashParams(state) == 0) {
            if (config->lightsFlash == nullptr) {
                config->lightsFlash = new LightsFlash(config->hwLight);
            }
            config->lightsFlash->setLightState(state);
            ret = config->lightsFlash->start();
            if (ret != 0) {
                LOG(ERROR) << "Cannot create flashing thread";
                config->flashMode = FlashMode::NONE;
                pthread_mutex_unlock(&config->writeMutex);
                return ScopedAStatus::fromExceptionCode(EX_TRANSACTION_FAILED);
            }
            config->flashMode = FlashMode::TIMED;
        } else {
            LOG(ERROR) << "Flash state is invalid";
            config->flashMode = FlashMode::NONE;
            pthread_mutex_unlock(&config->writeMutex);
            return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
        }
    }

    pthread_mutex_unlock(&config->writeMutex);
    return ScopedAStatus::ok();
}

ScopedAStatus Lights::getLights(std::vector<HwLight>* lights) {
    LOG(INFO) << "Lights reporting supported lights";

    for (auto i = availableLights.begin(); i != availableLights.end(); i++) {
        lights->push_back(i->hwLight);
    }

    return ScopedAStatus::ok();
}

/**
 * Check lights flash parameters
 * @param state pointer to the state to check
 * @return 0 if success, error code otherwise
 */
int Lights::checkFlashParams(const HwLightState& state)
{
    int64_t ns = 0;

    if ((state.flashOffMs < 0) || (state.flashOnMs < 0)) {
        return -1;
    }

    if ((state.flashOffMs == 0) && (state.flashOnMs) == 0) {
        return -1;
    }

    /* check for overflow in ns */
    ns = state.flashOffMs * ONE_MS_IN_NS;
    if (ns / ONE_MS_IN_NS != state.flashOffMs) {
        return -1;
    }
    ns = state.flashOnMs * ONE_MS_IN_NS;
    if (ns / ONE_MS_IN_NS != state.flashOnMs) {
        return -1;
    }

    return 0;
}

/**
 * Add light in list
 * @param type
 * @param ordinal
 */
void Lights::addLight(LightType const type, int const ordinal) {
    HwLightConfig config{};

    config.hwLight.id = availableLights.size();
    config.hwLight.type = type;
    config.hwLight.ordinal = ordinal;

    config.writeMutex = PTHREAD_MUTEX_INITIALIZER;
    config.flashMode = FlashMode::NONE;
    config.lightsFlash = nullptr;

    availableLights.emplace_back(config);
}

}  // namespace light
}  // namespace hardware
}  // namespace android
}  // namespace aidl
