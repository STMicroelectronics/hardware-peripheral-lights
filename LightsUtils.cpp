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

char const* const LED_TRIGGER = "/sys/class/leds/%s/trigger";
char const* const LED_BRIGHTNESS = "/sys/class/leds/%s/brightness";
char const* const LED_MAX_BRIGHTNESS = "/sys/class/leds/%s/max_brightness";

// char const* const LED_RED_NAME = "red";
char const* const LED_BLUE_NAME = "blue:heartbeat";

char const* const LED_HW_TRIGGER_ON = "heartbeat";
char const* const LED_HW_TRIGGER_OFF = "none";

char const* const BACKLIGHT_BRIGHTNESS = "/sys/class/backlight/panel-lvds-backlight/brightness";
char const* const BACKLIGHT_MAX_BRIGHTNESS = "/sys/class/backlight/panel-lvds-backlight/max_brightness";


/**
 * Get led name associated to required light type
 * @param type
 * @return name
 */
const char* LightsUtils::getLedName(LightType type)
{
    switch (type) {
        case LightType::NOTIFICATIONS:
            return LED_BLUE_NAME;
        case LightType::ATTENTION:
            return LED_BLUE_NAME;
        default:
            return nullptr;
    }
}

/**
 * Set the color value
 * 
 * @param led = name of the led in path
 * @param color = RGB color value
 * @param trigger = HW flash mode required ?
 * @return 0 if success, error code otherwise
 */
int LightsUtils::setColorValue(const char* led, int color, bool trigger)
{
    int fd = 0;
    long int max_brightness = 255;
    long int brightness = 0;
    char path[64];
    char buf[64] = {0};

    /* get max led brightness */
    snprintf(path, sizeof(path), LED_MAX_BRIGHTNESS, led);
    // LOG(INFO) << "set color value " << std::hex << color << " for device path " << path;
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        PLOG(ERROR) << "Failed to open max brightness for device path " << path;
    }

    /* max brightness size fixed to 8 bytes */
    ssize_t rb = read(fd, buf, 8);
    close(fd);
    if (rb < 0) {
        PLOG(ERROR) << "Failed to read light max brightness " << path;
    } else {
        char* endptr;
        long int ret = strtol(buf, &endptr, 10);
        if ('\0' != *endptr && '\n' != *endptr) {
            LOG(ERROR) << "max brightness: Error in string conversion";
        } else {
            max_brightness = ret;
        }
    }
    // LOG(INFO) << "Max brightness read: " << max_brightness;

    /* calculate brightness depending on color level requested (RGB) */
    color = color & 0x00FFFFFF;
    if (color > 1) {
        brightness = ((77*((color>>16)&0x00ff)) + (150*((color>>8)&0x00ff)) + (29*(color&0x00ff))) >> 8;
        if (brightness > max_brightness)
            brightness = max_brightness;
    } else {
        brightness = (color==1) ? max_brightness : 0;
    }
    // LOG(INFO) << "Brightness set: " << brightness;

    /* set led trigger */
    int size_w;
    ssize_t wb;
    snprintf(path, sizeof(path), LED_TRIGGER, led);
    fd = open(path, O_RDWR);
    if (fd < 0) {
        PLOG(ERROR) << "Failed to open light trigger " << path;
    } else {
        if (trigger) {
            size_w = snprintf(buf, 64, "%s", LED_HW_TRIGGER_ON);
        } else {
            size_w = snprintf(buf, 64, "%s", LED_HW_TRIGGER_OFF);
        }
        wb = write(fd, &buf, size_w*sizeof(char));
        close(fd);
        if (wb == -1) {
            PLOG(ERROR) << "Failed to write light trigger " << path;
        }
    }

    /* set led brightness */
    snprintf(path, sizeof(path), LED_BRIGHTNESS, led);
    fd = open(path, O_RDWR);
    if (fd < 0) {
        PLOG(ERROR) << "Failed to open light brightness " << path;
        return -1;
    }

    size_w = snprintf(buf, 64, "%d", (int)brightness);
    wb = write(fd, &buf, size_w*sizeof(char));
    close(fd);
    if (wb == -1) {
        PLOG(ERROR) << "Failed to write light brightness " << path;
        return -1;
    }

    return 0;
}

/**
 * Check if the backlight is available
 * @return true if available, false otherwise
 */
bool LightsUtils::isBacklightAvailable()
{
    int fd = 0;
    fd = open(BACKLIGHT_BRIGHTNESS, O_RDONLY);
    if (fd < 0) {
        return false;
    }
    close(fd);
    return true;
}

/**
 * Set the color value
 * 
 * @param color = RGB color value
 * @return 0 if success, error code otherwise
 */
int LightsUtils::setBacklightValue(int color)
{
    int fd = 0;
    long int max_brightness = 1;
    long int brightness = 0;
    char buf[64] = {0};

    /* get max backlight brightness */
    // LOG(INFO) << "set color value " << std::hex << color << " for backlight";
    fd = open(BACKLIGHT_MAX_BRIGHTNESS, O_RDONLY);
    if (fd < 0) {
        PLOG(ERROR) << "Failed to open max brightness for backlight";
    }

    /* max brightness size fixed to 8 bytes */
    ssize_t rb = read(fd, buf, 8);
    close(fd);
    if (rb < 0) {
        PLOG(ERROR) << "Failed to read max brightness for backlight";
    } else {
        char* endptr;
        long int ret = strtol(buf, &endptr, 10);
        if ('\0' != *endptr && '\n' != *endptr) {
            LOG(ERROR) << "Failed to to convert max brightness string to int value";
        } else {
            max_brightness = ret;
        }
    }
    // LOG(INFO) << "Max brightness read: " << max_brightness;

    /* calculate brightness depending on color level requested (RGB) */
    color = color & 0x00FFFFFF;
    if (color > 1) {
        brightness = ((77*((color>>16)&0x00ff)) + (150*((color>>8)&0x00ff)) + (29*(color&0x00ff))) >> 8;
        if (brightness > max_brightness)
            brightness = max_brightness;
    } else {
        brightness = (color==1) ? max_brightness : 0;
    }
    // LOG(INFO) << "Brightness set: " << brightness;

    /* set backlight brightness */
    fd = open(BACKLIGHT_BRIGHTNESS, O_RDWR);
    if (fd < 0) {
        PLOG(ERROR) << "Failed to open backlight brightness";
        return -1;
    }

    int size_w = snprintf(buf, 64, "%d", (int)brightness);
    ssize_t wb = write(fd, &buf, size_w*sizeof(char));
    close(fd);
    if (wb == -1) {
        PLOG(ERROR) << "Failed to write backlight brightness";
        return -1;
    }

    return 0;
}

/**
 * Get back light type name for trace purpose
 * @param type = light type
 * @return name
 */
const char* LightsUtils::getLightTypeName(LightType type)
{
    char* ch = new char[16];
    switch (type) {
        case LightType::BACKLIGHT:
            strcpy(ch, "BACKLIGHT");
            break;
        case LightType::KEYBOARD:
            strcpy(ch, "KEYBOARD");
            break;
        case LightType::BUTTONS:
            strcpy(ch, "BUTTONS");
            break;
        case LightType::BATTERY:
            strcpy(ch, "BATTERY");
            break;
        case LightType::NOTIFICATIONS:
            strcpy(ch, "NOTIFICATIONS");
            break;
        case LightType::ATTENTION:
            strcpy(ch, "ATTENTION");
            break;
        case LightType::BLUETOOTH:
            strcpy(ch, "BLUETOOTH");
            break;
        case LightType::WIFI:
            strcpy(ch, "WIFI");
            break;
        case LightType::MICROPHONE:
            strcpy(ch, "MICROPHONE");
            break;
        default:
            strcpy(ch, "UNKNOWN");
    }
    return ch;
}

/**
 * Get back flash mode name for trace purpose
 * @param mode = flash mode
 * @return name
 */
const char* LightsUtils::getFlashModeName(FlashMode mode)
{
    char* ch = new char[16];
    switch (mode) {
        case FlashMode::NONE:
            strcpy(ch, "NONE");
            break;
        case FlashMode::TIMED:
            strcpy(ch, "TIMED");
            break;
        case FlashMode::HARDWARE:
            strcpy(ch, "HARDWARE");
            break;
        default:
            strcpy(ch, "UNKNOWN");
    }
    return ch;
}

}  // namespace light
}  // namespace hardware
}  // namespace android
}  // namespace aidl
