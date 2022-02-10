/*
 * Copyright (C) 2013 The Android Open Source Project
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

#include <hardware/vibrator.h>

#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <stddef.h>
#include <linux/input.h>

#include <log/log.h>

#define TIMEOUT_STR_LEN         20
#define VIBRATOR_FILE_LEN       64

enum {
    VIBRATOR_NONE,
    VIBRATOR_FF,
    VIBRATOR_LED,
    VIBRATOR_TIMED,
    VIBRATOR_INVLD,
};

static const char THE_DEVICE[] = "/sys/class/timed_output/vibrator/enable";

static bool device_exists(const char *file) {
    int fd;

    fd = TEMP_FAILURE_RETRY(open(file, O_RDWR));
    if (fd < 0) {
        ALOGD("open %s failed, errno=%d(%s)\n", file, errno, strerror(errno));
        return false;
    }

    close(fd);
    return true;
}

static bool vibra_exists() {
    return device_exists(THE_DEVICE);
}

static int write_value(const char *file, const char *value)
{
    int to_write, written, ret, fd;

    fd = TEMP_FAILURE_RETRY(open(file, O_WRONLY));
    if (fd < 0) {
        return -errno;
    }

    to_write = strlen(value) + 1;
    written = TEMP_FAILURE_RETRY(write(fd, value, to_write));
    if (written == -1) {
        ret = -errno;
    } else if (written != to_write) {
        /* even though EAGAIN is an errno value that could be set
           by write() in some cases, none of them apply here.  So, this return
           value can be clearly identified when debugging and suggests the
           caller that it may try to call vibrator_on() again */
        ret = -EAGAIN;
    } else {
        ret = 0;
    }

    errno = 0;
    close(fd);

    return ret;
}

static int sendit(unsigned int timeout_ms)
{
    char value[TIMEOUT_STR_LEN]; /* large enough for millions of years */

    snprintf(value, sizeof(value), "%u", timeout_ms);
    return write_value(THE_DEVICE, value);
}

static int vibra_on(vibrator_device_t* vibradev __unused, unsigned int timeout_ms)
{
    /* constant on, up to maximum allowed time */
    return sendit(timeout_ms);
}

static int vibra_off(vibrator_device_t* vibradev __unused)
{
    return sendit(0);
}

static const char LED_DEVICE[] = "/sys/class/leds/vibrator";

static int write_led_file(const char *file, const char *value)
{
    char file_str[VIBRATOR_FILE_LEN] = {0};

    snprintf(file_str, sizeof(file_str), "%s/%s", LED_DEVICE, file);
    return write_value(file_str, value);
}

static bool vibra_led_exists()
{
    char file_str[VIBRATOR_FILE_LEN] = {0};

    snprintf(file_str, sizeof(file_str), "%s/%s", LED_DEVICE, "activate");
    return device_exists(file_str);
}

static int vibra_led_on(vibrator_device_t* vibradev __unused, unsigned int timeout_ms)
{
    int ret;
    char value[TIMEOUT_STR_LEN]; /* large enough for millions of years */

    ret = write_led_file("state", "1");
    if (ret)
        return ret;

    snprintf(value, sizeof(value), "%u\n", timeout_ms);
    ret = write_led_file("duration", value);
    if (ret)
        return ret;

    return write_led_file("activate", "1");
}

static int vibra_led_off(vibrator_device_t* vibradev __unused)
{
    return write_led_file("activate", "0");
}

static int vibra_ff_id = -1;
static int vibra_ff_fd = -1;
static int vibra_event_id = 0;
static const char *INPUT_DEVICE = "/dev/input";
static const char *ff_vibrators[] = {
    "sc27xx:vibrator",
};

static bool is_vibra_ff(const char *name)
{
    unsigned int i = 0;

    for (i = 0; i < sizeof(ff_vibrators)/sizeof(ff_vibrators[0]); i++) {
        if (!strncmp(ff_vibrators[i], name, strlen(ff_vibrators[i])))
            return true;
    }

    return false;
}

static int vibra_ff_lookup()
{
    int found = -1;
    int fd = -1;
    DIR *dir = NULL;
    struct dirent *result = NULL;
    char file_name[256] = {0};
    char buf[64] = {0};

    dir = opendir(INPUT_DEVICE);
    if (!dir) {
        ALOGE("open %s failed, errno=%d(%s)\n", INPUT_DEVICE, errno, strerror(errno));
        return -1;
    }

    while ((result = readdir(dir)) != NULL) {
        if (strncmp(result->d_name, "event", strlen("event")))
            continue;
        memset(file_name, 0, sizeof(file_name));
        snprintf(file_name, sizeof(file_name) - 1, "%s/%s", INPUT_DEVICE, result->d_name);
        fd = open(file_name, O_RDWR);
        if (fd < 0)
            continue;
        memset(buf, 0, sizeof(buf));
        ioctl(fd, EVIOCGNAME(sizeof(buf) - 1), buf);
        close(fd);
        fd = -1;
        if (is_vibra_ff(buf)) {
            sscanf(result->d_name, "event%d", &found);
            break;
        }
    }

    closedir(dir);
    dir = NULL;

    return found;
}

static bool vibra_ff_exists()
{
    char file_str[VIBRATOR_FILE_LEN] = {0};

    vibra_event_id = vibra_ff_lookup();
    if (vibra_event_id < 0)
        return false;

    snprintf(file_str, sizeof(file_str), "%s/event%d", INPUT_DEVICE, vibra_event_id);

    return device_exists(file_str);
}

static int vibra_ff_upload(unsigned int timeout_ms)
{
    int ret = 0;
    struct ff_effect effect;

    if (vibra_ff_fd < 0)
        return -1;

    memset(&effect, 0, sizeof(effect));
    effect.type = FF_RUMBLE;
    effect.id = (vibra_ff_id >= 0) ? vibra_ff_id : -1;
    effect.replay.delay = 0;
    effect.replay.length = timeout_ms;
    effect.u.rumble.weak_magnitude = 1;

    ret = ioctl(vibra_ff_fd, EVIOCSFF, &effect);
    if (ret) {
        ALOGE("upload ff effect failed, errno=%d(%s)\n", errno, strerror(errno));
        return ret;
    }

    if (vibra_ff_id < 0)
        vibra_ff_id = effect.id;

    return ret;
}

static int vibra_ff_erase()
{
    int ret = 0;

    if (vibra_ff_fd < 0)
        return -1;

    ret = ioctl(vibra_ff_fd, EVIOCRMFF, (void *)((unsigned long)vibra_ff_id));
    if (0 != ret)
        ALOGE("erase ff effect failed, errno=%d(%s)\n", errno, strerror(errno));
    vibra_ff_id = -1;

    return 0;
}

static int vibra_ff_write(unsigned int count)
{
    int ret = 0;
    struct input_event event;

    if (vibra_ff_fd < 0)
        return -1;

    memset(&event, 0, sizeof(event));
    event.type = EV_FF;
    event.code = vibra_ff_id;
    event.value = count;

    ret = write(vibra_ff_fd, (const void *)&event, sizeof(event));
    if (ret != sizeof(event))
        ret = -EAGAIN;
    else
        ret = 0;

    return ret;
}

static int vibra_ff_on(vibrator_device_t* vibradev __unused, unsigned int timeout_ms)
{
    char file_str[VIBRATOR_FILE_LEN] = {0};

    snprintf(file_str, sizeof(file_str), "%s/event%d", INPUT_DEVICE, vibra_event_id);

    if (vibra_ff_fd < 0) {
        vibra_ff_fd = open(file_str, O_RDWR);
        if (vibra_ff_fd < 0)
            return -errno;
    }

    vibra_ff_upload(timeout_ms);
    vibra_ff_write(1);

    return 0;
}

static int vibra_ff_off(vibrator_device_t* vibradev __unused)
{
    if (vibra_ff_fd < 0)
        return 0;

    vibra_ff_write(0);
    vibra_ff_erase();

    close(vibra_ff_fd);
    vibra_ff_fd = -1;

    return 0;
}

static int vibra_close(hw_device_t *device)
{
    free(device);
    return 0;
}

static int vibra_open(const hw_module_t* module, const char* id __unused,
                      hw_device_t** device __unused) {
    int vibra_type = 0;

    if (vibra_ff_exists()) {
        ALOGD("Vibrator using force feedback");
        vibra_type = VIBRATOR_FF;
    } else if (vibra_exists()) {
        ALOGD("Vibrator using timed_output");
        vibra_type = VIBRATOR_TIMED;
    } else if (vibra_led_exists()) {
        ALOGD("Vibrator using LED trigger");
        vibra_type = VIBRATOR_LED;
    } else {
        ALOGE("Vibrator device does not exist. Cannot start vibrator");
        return -ENODEV;
    }

    vibrator_device_t *vibradev = calloc(1, sizeof(vibrator_device_t));

    if (!vibradev) {
        ALOGE("Can not allocate memory for the vibrator device");
        return -ENOMEM;
    }

    vibradev->common.tag = HARDWARE_DEVICE_TAG;
    vibradev->common.module = (hw_module_t *) module;
    vibradev->common.version = HARDWARE_DEVICE_API_VERSION(1,0);
    vibradev->common.close = vibra_close;

    switch (vibra_type) {
    case VIBRATOR_FF:
        vibradev->vibrator_on = vibra_ff_on;
        vibradev->vibrator_off = vibra_ff_off;
        break;
    case VIBRATOR_LED:
        vibradev->vibrator_on = vibra_led_on;
        vibradev->vibrator_off = vibra_led_off;
        break;
    case VIBRATOR_TIMED:
    default:
        vibradev->vibrator_on = vibra_on;
        vibradev->vibrator_off = vibra_off;
        break;
    }

    *device = (hw_device_t *) vibradev;

    return 0;
}

/*===========================================================================*/
/* Default vibrator HW module interface definition                           */
/*===========================================================================*/

static struct hw_module_methods_t vibrator_module_methods = {
    .open = vibra_open,
};

struct hw_module_t HAL_MODULE_INFO_SYM = {
    .tag = HARDWARE_MODULE_TAG,
    .module_api_version = VIBRATOR_API_VERSION,
    .hal_api_version = HARDWARE_HAL_API_VERSION,
    .id = VIBRATOR_HARDWARE_MODULE_ID,
    .name = "Default vibrator HAL",
    .author = "The Android Open Source Project",
    .methods = &vibrator_module_methods,
};
