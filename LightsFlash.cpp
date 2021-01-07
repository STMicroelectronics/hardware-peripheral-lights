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

#include "LightsFlash.h"

#include <android-base/logging.h>

namespace aidl {
namespace android {
namespace hardware {
namespace light {

static int64_t const ONE_MS_IN_NS = 1000000LL;
static int64_t const ONE_S_IN_NS = 1000000000LL;

LightsFlash::LightsFlash(HwLight light) : mHwLight{light}
{
    if (initLightSyncResources() != 0) {
        LOG(ERROR) << "Cannot initialize the pthread";
    }
}

LightsFlash::~LightsFlash()
{
    stop();
}

/**
 * Set light state
 * @param state
 */
void LightsFlash::setLightState(HwLightState state)
{
    mHwLightState = state;
    mState = LightsFlashState::INITIALIZED;
}

/**
 * Initialize light synchronization resources
 * @param cond what condition variable to initialize
 * @param signal_mutex what mutex (associated with the condvar) to initialize
 * @return 0 if success, error code otherwise
 */
int LightsFlash::initLightSyncResources()
{
    int ret = 0;
    pthread_condattr_t condattr;

    ret = pthread_condattr_init(&condattr);
    if (ret != 0) {
        LOG(ERROR) << "Cannot initialize the pthread condattr";
        return ret;
    }

    ret = pthread_condattr_setclock(&condattr, CLOCK_MONOTONIC);
    if (ret != 0) {
        LOG(ERROR) << "Cannot set the clock of condattr to monotonic";
        goto destroy_condattr;
    }

    ret = pthread_cond_init(&mFlashCond, &condattr);
    if (ret != 0) {
        LOG(ERROR) << "Cannot intialize the pthread structure";
        goto destroy_condattr;
    }

    ret = pthread_mutex_init(&mFlashSignalMutex, nullptr);
    if (ret != 0) {
        LOG(ERROR) << "Cannot initialize the mutex associated with the pthread cond";
        goto destroy_cond;
    }

    pthread_condattr_destroy(&condattr);
    return ret;

destroy_cond:
    pthread_cond_destroy(&mFlashCond);
destroy_condattr:
    pthread_condattr_destroy(&condattr);
    return ret;
}

static void* execRoutine(void *arg) {
    LightsFlash* _this=static_cast<LightsFlash*>(arg);
    _this->flashRoutine();
    return nullptr;
}

int LightsFlash::start() {
    int ret = 0;
    if ((mState == LightsFlashState::INITIALIZED) || (mState == LightsFlashState::STOPPED)) {
        ret = pthread_create(&mFlashThread, nullptr, execRoutine, this);
        if (ret == 0) {
            mState = LightsFlashState::STARTED;
        }
    }
    return ret;
}

void LightsFlash::stop() {
    if (mState == LightsFlashState::STARTED) {
        LOG(INFO) << "Stop flash routine for light type "
                  << LightsUtils::getLightTypeName(mHwLight.type);

        pthread_mutex_lock(&mFlashSignalMutex);
        mHwLightState.flashMode = FlashMode::NONE;
        pthread_cond_signal(&mFlashCond);
        pthread_mutex_unlock(&mFlashSignalMutex);
        pthread_join(mFlashThread, nullptr);
    }
}

/**
 * Get current timestamp in nanoseconds
 * @return time in nanoseconds
 */
int64_t LightsFlash::getTimestampMonotonic()
{
    struct timespec ts = {0, 0};

    if (!clock_gettime(CLOCK_MONOTONIC, &ts)) {
        return ONE_S_IN_NS * ts.tv_sec + ts.tv_nsec;
    }

    return -1;
}

/**
 * Populates a timespec data structure from a int64_t timestamp
 * @param out what timespec to populate
 * @param target_ns timestamp in nanoseconds
 */
void LightsFlash::setTimestamp(struct timespec *out, int64_t target_ns)
{
    out->tv_sec  = target_ns / ONE_S_IN_NS;
    out->tv_nsec = target_ns % ONE_S_IN_NS;
}

void LightsFlash::flashRoutine() {
    int color = 0, ret = 0, reqColor = 0;
    struct timespec targetTime;
    int64_t timestamp, period;

    if (mState != LightsFlashState::STARTED) {
        LOG(ERROR) << "start flash routing while in bad state";
        return;
    }

    LOG(INFO) << "Start flash routine for light type "
              << LightsUtils::getLightTypeName(mHwLight.type);

    const char* name = LightsUtils::getLedName(mHwLight.type);
    if (name == nullptr) {
        LOG(ERROR) << "Light type unknown"; 
        return;
    }

    pthread_mutex_lock(&mFlashSignalMutex);

    reqColor = mHwLightState.color;
    color = reqColor;

    /* Light flashing loop */
    while (mHwLightState.flashMode == FlashMode::TIMED) {
        ret = LightsUtils::setColorValue(name, color, false);
        if (ret != 0) {
            LOG(ERROR) << "Cannot set light color";
            goto mutex_unlock;
        }

        timestamp = getTimestampMonotonic();
        if (timestamp < 0) {
            LOG(ERROR) << "Cannot get time from monotonic clock";
            goto mutex_unlock;
        }

        if (color) {
            color = 0;
            period = mHwLightState.flashOnMs * ONE_MS_IN_NS;
        } else {
            color = reqColor;
            period = mHwLightState.flashOffMs * ONE_MS_IN_NS;
        }

        /* check for overflow */
        if (timestamp > LLONG_MAX - period) {
            LOG(ERROR) << "Timestamp overflow";
            goto mutex_unlock;
        }

        timestamp += period;

        /* sleep until target_time or the cond var is signaled */
        setTimestamp(&targetTime, timestamp);
        ret = pthread_cond_timedwait(&mFlashCond, &mFlashSignalMutex, &targetTime);
        if ((ret != 0) && (ret != ETIMEDOUT)) {
            LOG(ERROR) << "pthread_cond_timedwait returned an error";
            goto mutex_unlock;
        }
    }

mutex_unlock:
    pthread_mutex_unlock(&mFlashSignalMutex);
}

}  // namespace light
}  // namespace hardware
}  // namespace android
}  // namespace aidl
