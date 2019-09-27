/*
 * Copyright (C) 2016 STMicroelectronics
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

#ifndef LIGHTS_CONFIG_H
#define LIGHTS_CONFIG_H

#define COLOR_MONO					0
#define COLOR_RGB					1

#define LIGHT_DEVICE_MAX_SIZE	30

typedef enum {
  LIGHT_SUCCESS = 0,
  LIGHT_ERROR_NONE = 0,
  LIGHT_ERROR_UNKNOWN = -1,
  LIGHT_ERROR_NOT_SUPPORTED = -2,
  LIGHT_ERROR_NOT_AVAILABLE = -3,
  LIGHT_ERROR_INVALID_ARGS = -4,
  LIGHT_ERROR_TIMED_OUT = -5,
} light_error;

/* Light configuration device */
struct lights_config_t {
    char       		*light_device;
    uint16_t		light_color;
    uint16_t		light_device_status;
};

int parse_config_file(struct lights_config_t *config, char const *name);
int reset_config(char *device);

#endif
