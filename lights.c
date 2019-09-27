/*
 * Copyright (C) 2015 The Android Open Source Project
 * Copyright (C) 2016 STMicroelectronics
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

#define LOG_TAG "lights"

// #define LOG_NDEBUG 0

#include <malloc.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <log/log.h>
#include <hardware/lights.h>
#include <hardware/hardware.h>
#include <pthread.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "lights_config.h"

#define LIGHT_MAX_BRIGHTNESS	"/sys/class/leds/%s/max_brightness"
#define LIGHT_BRIGHTNESS		"/sys/class/leds/%s/brightness"
#define LIGHT_TRIGGER    		"/sys/class/leds/%s/trigger"
#define LIGHT_PATH_MAX_SIZE		64

#define COLOR_RED    				0xFF0000
#define COLOR_GREEN    				0x00FF00
#define COLOR_BLUE    				0x0000FF

#define LIGHT_BRIGHTNESS_OFF 		"0"

#define LIGHT_DEVICE_STUB_NAME 			"stub"
#define LIGHT_DEVICE_STUB_MAX_BRIGHTNESS 255

/* List of possible supported lights */
typedef enum {
	BACKLIGHT_TYPE,
	KEYBOARD_TYPE,
	BUTTONS_TYPE,
	BATTERY_TYPE,
	NOTIFICATIONS_TYPE,
	ATTENTION_TYPE,
	BLUETOOTH_TYPE,
	WIFI_TYPE,
	LIGHTS_TYPE_NUM
} light_type_t;

/* Light device data structure */
struct light_device_ext_t {
	/* Base device */
	struct light_device_t	base_dev;
	/* Current state of the light device */
	struct light_state_t	state;
	/* lights configuration */
	struct lights_config_t	*config;
	/* Number of device references */
	int						refs;
	/* Synchronization attributes */
	pthread_t				flash_thread;
	pthread_cond_t			flash_cond;
	pthread_mutex_t			flash_signal_mutex;
	pthread_mutex_t			write_mutex;
};

static int64_t const ONE_MS_IN_NS = 1000000LL;
static int64_t const ONE_S_IN_NS = 1000000000LL;


/*
 * Array of light devices with write_mutex statically initialized
 * to be able to synchronize the open_lights & close_lights functions
 */
struct light_device_ext_t light_devices[] = {
	[ 0 ... (LIGHTS_TYPE_NUM - 1) ] = { .write_mutex = PTHREAD_MUTEX_INITIALIZER }
};

/*
 * Set the color value (impact only brightness)
 * @param color color value (RGB)
 * @return 0 if success, error code otherwise
 */
static light_error set_color_value(struct lights_config_t *config, int color)
{
	int fd = 0;
	int ret = LIGHT_SUCCESS;
	long int max_brightness = 200;
	long int brightness = 0;
	char path[LIGHT_PATH_MAX_SIZE];
	char buf[64] = {0};

	if (strcmp(config->light_device, LIGHT_DEVICE_STUB_NAME) == 0)
		return LIGHT_SUCCESS;

	snprintf(path, sizeof(path), LIGHT_MAX_BRIGHTNESS, config->light_device);
	ALOGV("%s: Get max brightness for device path %s", __func__, path);

	fd = open(path, O_RDONLY);

	if (fd < 0) {
		ALOGE("%s: Failed to open light max brightness (%s): %s", __func__, strerror(errno), path);
		return LIGHT_ERROR_UNKNOWN;
	}

	/* Get back size of the driver */
/*
	off_t size = lseek(fd, 0, SEEK_END);
	lseek(fd, 0, SEEK_SET);
*/

	/* max brightness size fixed to 8 bytes */
    ssize_t rb = read(fd, buf, 8);
	close(fd);

    if (rb < 0) {
		ALOGE("%s: Failed to read green light max brightness (%s)", __func__, strerror(errno));
        ret = LIGHT_ERROR_UNKNOWN;
    }
    else {

		char* endptr;
    
		long int ret = strtol(buf, &endptr, 10);

		if ('\0' != *endptr && '\n' != *endptr) {
			ALOGE("%s: max brightness: Error in string conversion", __func__);
			return LIGHT_ERROR_UNKNOWN;
		}

		max_brightness = ret;
	}

	ALOGV("%s: Max brightness read: %ld", __func__, max_brightness);

	/* calculate brightness depending on color level requested (RGB) */
	
	color = color & 0x00FFFFFF;
	
	if (color > 1) {
		brightness = ((77*((color>>16)&0x00ff)) + (150*((color>>8)&0x00ff)) + (29*(color&0x00ff))) >> 8;
		if (brightness > max_brightness)
			brightness = max_brightness;
	} else {
		brightness = (color==1) ? max_brightness : 0;
	}

	ALOGV("%s: Brightness set: %ld", __func__, brightness);

    snprintf(path, sizeof(path), LIGHT_BRIGHTNESS, config->light_device);
	/* set green led brithness */
	fd = open(path, O_RDWR);

	if (fd < 0) {
		ALOGE("%s: Failed to open green light brightness (%s): %s", __func__, strerror(errno), path);
		return LIGHT_ERROR_UNKNOWN;
	}

	int size_w = snprintf(buf, 64, "%d", (int)brightness);

	ssize_t wb = write(fd, &buf, size_w*sizeof(char));
	close(fd);

	if (wb == -1) {
		ALOGE("%s: Failed to write green light brightness (%s)", __func__, strerror(errno));
		return LIGHT_ERROR_UNKNOWN;
	}

	return ret;
}

/*
 * Get current timestamp in nanoseconds
 * @return time in nanoseconds
 */
int64_t get_timestamp_monotonic()
{
	struct timespec ts = {0, 0};

	if (!clock_gettime(CLOCK_MONOTONIC, &ts)) {
		return ONE_S_IN_NS * ts.tv_sec + ts.tv_nsec;
	}

	return -1;
}

/*
 * Populates a timespec data structure from a int64_t timestamp
 * @param out what timespec to populate
 * @param target_ns timestamp in nanoseconds
 */
void set_timestamp(struct timespec *out, int64_t target_ns)
{
	out->tv_sec  = target_ns / ONE_S_IN_NS;
	out->tv_nsec = target_ns % ONE_S_IN_NS;
}

/*
 * pthread routine which flashes an LED
 * @param flash_param light device pointer
 */
static void * flash_routine (void *flash_param)
{
	struct light_device_ext_t *dev = (struct light_device_ext_t *)flash_param;
	struct light_state_t *flash_state;
	int color = 0, ret = 0, reqcolor = 0;
	struct timespec target_time;
	int64_t timestamp, period;

	if (dev == NULL) {
		ALOGE("%s: Cannot flash a NULL light device", __func__);
		return NULL;
	}

	flash_state = &dev->state;

	pthread_mutex_lock(&dev->flash_signal_mutex);

	reqcolor = flash_state->color;
	color = reqcolor;

	/* Light flashing loop */
	while (flash_state->flashMode) {
		ret = set_color_value(dev->config, color);
		if (ret != 0) {
			ALOGE("%s: Cannot set light color", __func__);
			goto mutex_unlock;
		}

		timestamp = get_timestamp_monotonic();
		if (timestamp < 0) {
			ALOGE("%s: Cannot get time from monotonic clock", __func__);
			goto mutex_unlock;
		}

		if (color) {
			color = 0;
			period = flash_state->flashOnMS * ONE_MS_IN_NS;
		} else {
			color = reqcolor;
			period = flash_state->flashOffMS * ONE_MS_IN_NS;
		}

		/* check for overflow */
		if (timestamp > LLONG_MAX - period) {
			ALOGE("%s: Timestamp overflow", __func__);
			goto mutex_unlock;
		}

		timestamp += period;

		/* sleep until target_time or the cond var is signaled */
		set_timestamp(&target_time, timestamp);
		ret = pthread_cond_timedwait(&dev->flash_cond, &dev->flash_signal_mutex, &target_time);
		if ((ret != 0) && (ret != ETIMEDOUT)) {
			ALOGE("%s: pthread_cond_timedwait returned an error", __func__);
			goto mutex_unlock;
		}
	}

mutex_unlock:
	pthread_mutex_unlock(&dev->flash_signal_mutex);

	return NULL;
}

/*
 * Check lights flash state
 * @param state pointer to the state to check
 * @return 0 if success, error code otherwise
 */
static int check_flash_state(struct light_state_t const *state)
{
	int64_t ns = 0;

	if ((state->flashOffMS < 0) || (state->flashOnMS < 0)) {
		return LIGHT_ERROR_INVALID_ARGS;
	}

	if ((state->flashOffMS == 0) && (state->flashOnMS) == 0) {
		return LIGHT_ERROR_INVALID_ARGS;
	}

	/* check for overflow in ns */
	ns = state->flashOffMS * ONE_MS_IN_NS;
	if (ns / ONE_MS_IN_NS != state->flashOffMS) {
		return LIGHT_ERROR_INVALID_ARGS;
	}
	ns = state->flashOnMS * ONE_MS_IN_NS;
	if (ns / ONE_MS_IN_NS != state->flashOnMS) {
		return LIGHT_ERROR_INVALID_ARGS;
	}

	return 0;
}

/*
 * Generic function for setting the state of the light
 * @param base_dev light device data structure
 * @param state what state to set
 * @return 0 if success, error code otherwise
 */
static int set_light_generic(struct light_device_t *base_dev,
		struct light_state_t const *state)
{
	struct light_device_ext_t *dev = (struct light_device_ext_t *)base_dev;
	struct light_state_t *current_state;
	int ret = 0;

	if (dev == NULL) {
		ALOGE("%s: Cannot set state for NULL device", __func__);
		return LIGHT_ERROR_INVALID_ARGS;
	}

	current_state = &dev->state;

	pthread_mutex_lock(&dev->write_mutex);

	ALOGV("%s: flashMode:%x, color:%x", __func__, state->flashMode, state->color);

	if (current_state->flashMode) {
		/* destroy flashing thread */
		pthread_mutex_lock(&dev->flash_signal_mutex);
		current_state->flashMode = LIGHT_FLASH_NONE;
		pthread_cond_signal(&dev->flash_cond);
		pthread_mutex_unlock(&dev->flash_signal_mutex);
		pthread_join(dev->flash_thread, NULL);
	}

	*current_state = *state;

	if (state->flashMode) {
		/* start flashing thread */
		if (check_flash_state(current_state) == 0) {
			ret = pthread_create(&dev->flash_thread, NULL,
					flash_routine, (void *)dev);
			if (ret != 0) {
				ALOGE("%s: Cannot create flashing thread", __func__);
				current_state->flashMode = LIGHT_FLASH_NONE;
			}
		} else {
			ALOGE("%s: Flash state is invalid", __func__);
			current_state->flashMode = LIGHT_FLASH_NONE;
		}
	} else {
		ret = set_color_value(dev->config, state->color);
		if (ret != 0) {
			ALOGE("%s: Cannot set light color.", __func__);
		}
	}

	pthread_mutex_unlock(&dev->write_mutex);

	return ret;
}

/*
 * Initialize light synchronization resources
 * @param cond what condition variable to initialize
 * @param signal_mutex what mutex (associated with the condvar) to initialize
 * @return 0 if success, error code otherwise
 */
static int init_light_sync_resources(pthread_cond_t *cond,
		pthread_mutex_t *signal_mutex)
{
	int ret = LIGHT_SUCCESS;
	pthread_condattr_t condattr;

	ret = pthread_condattr_init(&condattr);
	if (ret != 0) {
		ALOGE("%s: Cannot initialize the pthread condattr", __func__);
		return LIGHT_ERROR_UNKNOWN;
	}

	ret = pthread_condattr_setclock(&condattr, CLOCK_MONOTONIC);
	if (ret != 0) {
		ALOGE("%s: Cannot set the clock of condattr to monotonic", __func__);
		ret = LIGHT_ERROR_UNKNOWN;
		goto destroy_condattr;
	}

	ret = pthread_cond_init(cond, &condattr);
	if (ret != 0) {
		ALOGE("%s: Cannot intialize the pthread structure", __func__);
		ret = LIGHT_ERROR_UNKNOWN;
		goto destroy_condattr;
	}

	ret = pthread_mutex_init(signal_mutex, NULL);
	if (ret != 0) {
		ALOGE("%s: Cannot initialize the mutex associated with the pthread cond", __func__);
		ret = LIGHT_ERROR_UNKNOWN;
		goto destroy_cond;
	}

	pthread_condattr_destroy(&condattr);
	return ret;

destroy_cond:
	pthread_cond_destroy(cond);
destroy_condattr:
	pthread_condattr_destroy(&condattr);
	return ret;
}

/*
 * Free light synchronization resources
 * @param cond what condition variable to free
 * @param signal_mutex what mutex (associated with the condvar) to free
 */
static void free_light_sync_resources(pthread_cond_t *cond,
		pthread_mutex_t *signal_mutex)
{
	pthread_mutex_destroy(signal_mutex);
	pthread_cond_destroy(cond);
}

/*
 * Close the lights module
 * @param base_dev light device data structure
 * @return 0 if success, error code otherwise
 */
static int close_lights(struct light_device_t *base_dev)
{
	struct light_device_ext_t *dev = (struct light_device_ext_t *)base_dev;
	int ret = LIGHT_SUCCESS;

	if (dev == NULL) {
		ALOGE("%s: Cannot deallocate a NULL light device", __func__);
		return LIGHT_ERROR_INVALID_ARGS;
	}

	pthread_mutex_lock(&dev->write_mutex);

	reset_config(dev->config->light_device);

	if (dev->config != NULL) {
		free(dev->config);
	}

	if (dev->refs == 0) {
		/* the light device is not open */
		ret = EINVAL;
		goto mutex_unlock;
	} else if (dev->refs > 1) {
		goto dec_refs;
	}

	if (dev->state.flashMode) {
		/* destroy flashing thread */
		pthread_mutex_lock(&dev->flash_signal_mutex);
		dev->state.flashMode = LIGHT_FLASH_NONE;
		pthread_cond_signal(&dev->flash_cond);
		pthread_mutex_unlock(&dev->flash_signal_mutex);
		pthread_join(dev->flash_thread, NULL);
	}

	free_light_sync_resources(&dev->flash_cond,
			&dev->flash_signal_mutex);

dec_refs:
	dev->refs--;

mutex_unlock:
	pthread_mutex_unlock(&dev->write_mutex);

	return ret;
}

/*
 * Module initialization routine which detects the LEDs' GPIOs
 * @param type light device type
 * @return 0 if success, error code otherwise
 */
static int stm_clear_lights(struct lights_config_t *config)
{
	int fd;
	int ret = LIGHT_SUCCESS;
    char path[LIGHT_PATH_MAX_SIZE];

	if (strcmp(config->light_device, LIGHT_DEVICE_STUB_NAME) == 0)
		return LIGHT_SUCCESS;

    snprintf(path, sizeof(path), LIGHT_BRIGHTNESS, config->light_device);
    ALOGV("%s: Clear led device path %s", __func__, path);

	/* Clear green led */
	fd = TEMP_FAILURE_RETRY(open(path, O_RDWR));
	if (fd < 0) {
		ALOGE("%s: Failed to open green light brightness (%s): %s", __func__, strerror(errno), path);
		return LIGHT_ERROR_UNKNOWN;
	}
	if (TEMP_FAILURE_RETRY(write(fd, LIGHT_BRIGHTNESS_OFF, 1)) != 1) {
		ALOGE("%s: Failed to write green light brightness (%s)", __func__, strerror(errno));
		ret = LIGHT_ERROR_UNKNOWN;
	}

	close(fd);
	
	return ret;
}

/*
 * Open a new lights device instance by name
 * @param module associated hw module data structure
 * @param name lights device name
 * @param device where to store the pointer of the allocated device
 * @return 0 if success, error code otherwise
 */
static int open_lights(const struct hw_module_t *module, char const *name,
		struct hw_device_t **device)
{
	struct light_device_ext_t *dev;
	int ret = LIGHT_SUCCESS, type = -1;

	if (0 == strcmp(LIGHT_ID_BACKLIGHT, name)) {
		type = BACKLIGHT_TYPE;
	} else if (0 == strcmp(LIGHT_ID_KEYBOARD, name)) {
		type = KEYBOARD_TYPE;
	} else if (0 == strcmp(LIGHT_ID_BATTERY, name)) {
		type = BATTERY_TYPE;
	} else if (0 == strcmp(LIGHT_ID_BUTTONS, name)) {
		type = BUTTONS_TYPE;
	} else if (0 == strcmp(LIGHT_ID_NOTIFICATIONS, name)) {
		type = NOTIFICATIONS_TYPE;
	} else if (0 == strcmp(LIGHT_ID_ATTENTION, name)) {
		type = ATTENTION_TYPE;
	} else if (0 == strcmp(LIGHT_ID_BLUETOOTH, name)) {
		type = BLUETOOTH_TYPE;
	} else if (0 == strcmp(LIGHT_ID_WIFI, name)) {
		type = WIFI_TYPE;
	} else {
		ALOGW("%s: Unknown light ID received = %s", __func__, name);
		return LIGHT_ERROR_INVALID_ARGS;
	}

	dev = (struct light_device_ext_t *)(light_devices + type);

	pthread_mutex_lock(&dev->write_mutex);

	struct lights_config_t* config = calloc(1, sizeof(struct lights_config_t));
	dev->config = config;

	/* Get light device configuration for required light name (if exist) */
	ret = parse_config_file(dev->config, name);
	if (0 != ret) {
		ALOGW("%s: %s lights module not available", __func__, name);
		goto mutex_unlock;
	}

	ALOGV("%s: Opening %s lights module with device %s", __func__, name, dev->config->light_device);

	if (dev->refs != 0) {
		/* already opened; nothing to do */
		goto inc_refs;
	}

	ret = stm_clear_lights(dev->config);
	if (ret != 0) {
		ALOGE("%s: Failed to initialize lights module", __func__);
		goto mutex_unlock;
	}

	ret = init_light_sync_resources(&dev->flash_cond,
				&dev->flash_signal_mutex);

	if (ret != 0) {
		goto mutex_unlock;
	}

	dev->base_dev.common.tag = HARDWARE_DEVICE_TAG;
	dev->base_dev.common.version = 0;
	dev->base_dev.common.module = (struct hw_module_t *)module;
	dev->base_dev.common.close = (int (*)(struct hw_device_t *))close_lights;
	dev->base_dev.set_light = set_light_generic;

inc_refs:
	dev->refs++;
	*device = (struct hw_device_t *)dev;

mutex_unlock:
	pthread_mutex_unlock(&dev->write_mutex);
	return ret;
}

static struct hw_module_methods_t lights_methods =
{
	.open =  open_lights,
};

struct hw_module_t HAL_MODULE_INFO_SYM =
{
	.tag = HARDWARE_MODULE_TAG,
	.version_major = 1,
	.version_minor = 0,
	.id = LIGHTS_HARDWARE_MODULE_ID,
	.name = "STM lights module",
	.author = "STM",
	.methods = &lights_methods,
};
