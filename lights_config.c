/*
 * Copyright (C) 2012-2013 Wolfson Microelectronics plc
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

#define LOG_TAG "lights_config"

// #define LOG_NDEBUG 0

#include <expat.h>
#include <errno.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stddef.h>
#include <sys/cdefs.h>
#include <sys/types.h>
#include <cutils/log.h>
#include <cutils/properties.h>
#include <cutils/compiler.h>

#include "lights_config.h"

#define BIT(x)			(1<<(x))
#define MAX_PARSE_DEPTH     6

/* Possible string values for the color attribute in xml file */
#define COLOR_RGB_STR 	"rgb"
#define COLOR_MONO_STR 	"mono"

/* For faster parsing put more commonly-used elements first */
enum element_index {
    e_elem_device = 0,
    e_elem_lightshal,

    e_elem_count
};

/* For faster parsing put more commonly-used attribs first */
enum attrib_index {
    e_attrib_name = 0,
    e_attrib_device,
    e_attrib_color,

    e_attrib_count
};

struct parse_state;
typedef int(*elem_fn)(struct parse_state *state);

struct parse_element {
    const char      *name;
    uint16_t        valid_attribs;  /* bitflags of valid attribs for this element */
    uint16_t        required_attribs;   /* bitflags of attribs that must be present */
    uint16_t        valid_subelem;  /* bitflags of valid sub-elements */
    elem_fn         start_fn;
    elem_fn         end_fn;
};

struct parse_attrib {
    const char      *name;
};

static int parse_device_start(struct parse_state *state);
static int parse_device_end(struct parse_state *state);

static const struct parse_element elem_table[e_elem_count] = {
    [e_elem_device] =    {
        .name = "device",
        .valid_attribs = BIT(e_attrib_name) | BIT(e_attrib_device) | BIT(e_attrib_color),
        .required_attribs = BIT(e_attrib_name) | BIT(e_attrib_device),
        .valid_subelem = 0,
        .start_fn = parse_device_start,
        .end_fn = parse_device_end
        },

    [e_elem_lightshal] =    {
        .name = "lightshal",
        .valid_attribs = 0,
        .required_attribs = 0,
        .valid_subelem = BIT(e_elem_device),
        .start_fn = NULL,
        .end_fn = NULL
        }
};

static const struct parse_attrib attrib_table[e_attrib_count] = {
    [e_attrib_name] =       {"name"},
    [e_attrib_device] =     {"device"},
    [e_attrib_color] =  	{"color"}
 };

struct parse_stack_entry {
    uint16_t            elem_index;
    uint16_t            valid_subelem;
};

/* Temporary state info for config file parser */
struct parse_state {
    char                	name[32];
    struct lights_config_t	*config;
    FILE                	*file;
    XML_Parser          	parser;
    char                	read_buf[256];
    int                 	parse_error; /* value > 0 aborts without error */
    int                 	error_line;

    struct {
        const char      *value[e_attrib_count];
        const XML_Char  **all;
    } attribs;

    struct {
        int             index;
        struct parse_stack_entry entry[MAX_PARSE_DEPTH];
    } stack;
};

/*
 * Set parser error
 * @param state what parser state shall be used for parsing task
 * @param error what error shall be set
 */
static int parse_set_error(struct parse_state *state, int error)
{
    state->parse_error = error;
    state->error_line = XML_GetCurrentLineNumber(state->parser);
    return error;
}

/*
 * Log parser error
 * @param state what parser state shall be used for parsing task
 */
static int parse_log_error(struct parse_state *state)
{
    int err = state->parse_error;
    int xml_err = XML_GetErrorCode(state->parser);

    if((err < 0) || (xml_err != XML_ERROR_NONE)) {
        ALOGE_IF(err < 0, "%s: Error in config file at line %d", __func__, state->error_line);
        ALOGE_IF(xml_err != XML_ERROR_NONE,
                            "%s: Parse error '%s' in config file at line %u",
                            __func__,
                            XML_ErrorString(xml_err),
                            (uint)XML_GetCurrentLineNumber(state->parser));
        return -EINVAL;
    } else {
        return 0;
    }
}

/*
 * Extract attibutes
 * @param state what parser state shall be used for parsing task
 * @param elem_index what section is concerned by the attributes extraction
 */
static int extract_attribs(struct parse_state *state, int elem_index)
{
    const uint32_t valid_attribs = elem_table[elem_index].valid_attribs;
    uint32_t required_attribs = elem_table[elem_index].required_attribs;
    const XML_Char **attribs = state->attribs.all;
    int i;

    memset(&state->attribs.value, 0, sizeof(state->attribs.value));

    while (attribs[0] != NULL) {
        for (i = 0; i < e_attrib_count; ++i ) {
            if ((BIT(i) & valid_attribs) != 0) {
                if (0 == strcmp(attribs[0], attrib_table[i].name)) {
                    state->attribs.value[i] = attribs[1];
                    required_attribs &= ~BIT(i);
                    break;
                }
            }
        }
        if (i >= e_attrib_count) {
            ALOGE("%s: Attribute '%s' not allowed here", __func__, attribs[0]);
            return -EINVAL;
        }

        attribs += 2;
    }

    if (required_attribs != 0) {
        for (i = 0; i < e_attrib_count; ++i ) {
            if ((required_attribs & BIT(i)) != 0) {
                ALOGE("%s: Attribute '%s' required", __func__, attrib_table[i].name);
            }
        }
        return -EINVAL;
    }

    return 0;
}

/*
 * Store required data in memory
 * @param fmt what data shall be stored in memory (char)
 */
char *make_message(const char *fmt, ...) {
    char *p = NULL;
    size_t size = LIGHT_DEVICE_MAX_SIZE;
    int n = 0;
    va_list ap;

    if((p = malloc(size)) == NULL)
        return NULL;

    while(1) {
        va_start(ap, fmt);
        n = vsnprintf(p, size, fmt, ap);
        va_end(ap);

        if(n > -1 && n < (int)size)
            return p;

        /* failed: have to try again, alloc more mem. */
        if(n > -1)      /* glibc 2.1 */
            size = n + 1;
        else            /* glibc 2.0 */
            size *= 2;     /* twice the old size */

        if((p = realloc (p, size)) == NULL)
            return NULL;
    }
}

/*
 * Parser device section start = callback
 * @param state what parser state shall be used for parsing task
 */
static int parse_device_start(struct parse_state *state)
{
    const char *dev_name = state->attribs.value[e_attrib_name];
    const char *dev_color = state->attribs.value[e_attrib_color];
    const char *dev = state->attribs.value[e_attrib_device];

    char *dev_cfg;

    ALOGV("%s: Parse device start check name <%s>", __func__, dev_name);

	if (0 == strcmp(dev_name, state->name)) {

		if (state->attribs.value[e_attrib_device] != NULL) {
			dev_cfg = make_message("%s", state->attribs.value[e_attrib_device]);
			state->config->light_device = dev_cfg;
//			ALOGV("%s: Light device %s selected for name <%s>", __func__, dev_cfg, dev_name);
		} else {
		    return -EINVAL;
		}

		if (dev_color != NULL) {
			if (0 == strcmp(dev_name, COLOR_RGB_STR)) {
				state->config->light_color = COLOR_RGB;
			} else {
				state->config->light_color = COLOR_MONO;
			}
		} else {
			state->config->light_color = COLOR_MONO;
		}
		state->config->light_device_status = 1;
	}
    return 0;
}

/*
 * Parser device section end = callback
 * @param state what parser state shall be used for parsing task
 */
static int parse_device_end(struct parse_state *state)
{
	if (state->config->light_device_status) {
		ALOGV("%s: Required device %s found in configuration file, stop parser", __func__, state->name);
//		XML_StopParser(state->parser, XML_FALSE);
	}
    return 0;
}

/*
 * Parser section start (whatever the section) = callback
 * @param data what parser state shall be used for parsing task
 * @param name what section name is started
 * @param attribs what section attributes has been read
 */
static void parse_section_start(void *data, const XML_Char *name,
                                const XML_Char **attribs)
{
    struct parse_state *state = (struct parse_state *)data;
    int stack_index = state->stack.index;
    const uint32_t valid_elems =
                        state->stack.entry[stack_index].valid_subelem;
    int i;

    if ((state->parse_error != 0) || (state->config->light_device_status)) {
        return;
    }

    ALOGV("%s: Parse start <%s>", __func__, name);

    /* Find element in list of elements currently valid */
    for (i = 0; i < e_elem_count; ++i) {
        if ((BIT(i) & valid_elems) != 0) {
            if (0 == strcmp(name, elem_table[i].name)) {
                break;
            }
        }
    }

    if ((i >= e_elem_count) || (stack_index >= MAX_PARSE_DEPTH)) {
        ALOGE("%s: Element '%s' not allowed here", __func__, name);
        parse_set_error(state, -EINVAL);
    } else {
        /* Element ok - push onto stack */
        ++stack_index;
        state->stack.entry[stack_index].elem_index = i;
        state->stack.entry[stack_index].valid_subelem
                                                = elem_table[i].valid_subelem;
        state->stack.index = stack_index;

        /* Extract attributes and call handler */
        state->attribs.all = attribs;
        if (extract_attribs(state, i) != 0) {
            parse_set_error(state, -EINVAL);
        } else {
			if (elem_table[i].start_fn) {
				parse_set_error(state, (*elem_table[i].start_fn)(state));
			}
        }
    }
}

/*
 * Parser section end (whatever the section) = callback
 * @param data what parser state shall be used for parsing task
 * @param name what section name is ended
 */
static void parse_section_end(void *data, const XML_Char *name)
{
    struct parse_state *state = (struct parse_state *)data;
    const int i = state->stack.entry[state->stack.index].elem_index;

    if (state->parse_error != 0) {
        return;
    }

    ALOGV("%s: Parse end <%s>", __func__, name );

    if (elem_table[i].end_fn) {
        state->parse_error = (*elem_table[i].end_fn)(state);
    }

    --state->stack.index;
}

/*
 * Open configuration file
 * @param state what parser state shall be used for parsing task
 */
static int open_config_file(struct parse_state *state)
{
    char name[80];
    char property[PROPERTY_VALUE_MAX];

    property_get("ro.product.device", property, "generic");
    snprintf(name, sizeof(name), "/vendor/etc/lights.%s.xml", property);

    ALOGV("%s: Reading configuration from %s\n", __func__, name);
    state->file = fopen(name, "r");
    if (state->file) {
        return 0;
    } else {
        snprintf(name, sizeof(name), "/system/etc/lights.%s.xml", property);
        ALOGV("%s: Reading configuration from %s\n", __func__, name);
        state->file = fopen(name, "r");
        if (state->file) {
            return 0;
        } else {
            ALOGE_IF(!state->file, "%s: Failed to open config file %s", __func__, name);
            return -ENOSYS;
        }
    }
}

/*
 * Start parsing
 * @param state what parser state shall be used for parsing task
 */
static int do_parse(struct parse_state *state)
{
    bool eof = false;
    int len;
    int ret = 0;

    state->parse_error = 0;
    state->stack.index = 0;
    /* First element must be <lightshal> */
    state->stack.entry[0].valid_subelem = BIT(e_elem_lightshal);

    while (!eof && (state->parse_error == 0)) {
        len = fread(state->read_buf, 1, sizeof(state->read_buf), state->file);
        if (ferror(state->file)) {
            ALOGE("%s: I/O error reading config file", __func__);
            ret = -EIO;
            break;
        }

        eof = feof(state->file);

        XML_Parse(state->parser, state->read_buf, len, eof);
        if (parse_log_error(state) < 0) {
            ret = -EINVAL;
            break;
        }
    }

    return ret;
}

/*
 * Cleanup parser
 * @param state what parser state structure shall be freed
 */
static void cleanup_parser(struct parse_state *state)
{
    if (state) {

        if (state->parser) {
            XML_ParserFree(state->parser);
        }

        if (state->file) {
            fclose(state->file);
        }

        free(state);
    }
}

/*
 * Free configuration file
 * @param device what device memory area shall be freed
 */
int reset_config(char *device)
{
	int ret = LIGHT_SUCCESS;

	if (device != NULL)
		free(device);

	return ret;
}

/*
 * Parse configuration file to required device
 * @param config what configuration shall be completed (out)
 * @param name what light device type shall be found in configuration file
 */
int parse_config_file(struct lights_config_t *config, char const *name)
{
    struct parse_state *state;
    int ret = 0;

    state = calloc(1, sizeof(struct parse_state));
    if (!state) {
        return -ENOMEM;
    }

    state->config = config;
    state->config->light_device_status = 0;
    memset(state->name, '\0',  sizeof(state->name));
    strncpy(state->name, name, sizeof(state->name) - 1);

    ALOGV("%s: Check availability of %s in configuration file", __func__, state->name);

    ret = open_config_file(state);
    if (ret == 0) {
        ret = -ENOMEM;
        state->parser = XML_ParserCreate(NULL);
        if (state->parser) {
            XML_SetUserData(state->parser, state);
            XML_SetElementHandler(state->parser, parse_section_start, parse_section_end);
            ret = do_parse(state);
        }
    }

    ALOGV("%s: Device %s selected", __func__, state->config->light_device);

    if (state->config->light_device == NULL)
		ret = LIGHT_ERROR_NOT_SUPPORTED;

fail:
    cleanup_parser(state);
    return ret;
}

