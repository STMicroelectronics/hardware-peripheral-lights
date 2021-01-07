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

#include <aidl/android/hardware/light/BnLights.h>

namespace aidl {
namespace android {
namespace hardware {
namespace light {

using ::ndk::ScopedAStatus;
using ::aidl::android::hardware::light::LightType;
using ::aidl::android::hardware::light::FlashMode;

class LightsUtils {
	private:
		LightsUtils() {}	// forbid instance creation
	public:
		static const char* getLedName(LightType type);
		static int setColorValue(const char* led, int color, bool trigger);
		static int setBacklightValue(int color);
		static const char* getFlashModeName(FlashMode mode);
		static const char* getLightTypeName(LightType type);
};

}  // namespace light
}  // namespace hardware
}  // namespace android
}  // namespace aidl
