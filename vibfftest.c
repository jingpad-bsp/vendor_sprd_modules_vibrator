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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/input.h>

#define FF_NEED_WRITE

int main(int argc, char **argv)
{
    int id = 0;
    int time_ms = 0;
    struct ff_effect effect;
#ifdef FF_NEED_WRITE
    struct input_event event;
#endif
    char file_str[64] = {0};
    char buf[64] = {0};
    int fd = -1;

    if (argc < 3) {
        printf("[usage]:vibtest event_id time_ms\n");
        return 0;
    }

    memset(&effect, 0, sizeof(effect));
    id = atoi(argv[1]);
    time_ms = atoi(argv[2]);

    effect.type = FF_RUMBLE;
    effect.id = -1;
    effect.replay.delay = 0;
    effect.replay.length = time_ms;
    effect.u.rumble.weak_magnitude = 1;

    snprintf(file_str, sizeof(file_str) - 1, "/dev/input/event%d", id);
    fd = open(file_str, O_RDWR);
    if (fd < 0) {
        printf("open %s failed, errno=%d(%s)\n", file_str, errno, strerror(errno));
        return -1;
    }

    ioctl(fd, EVIOCGNAME(sizeof(buf) - 1), buf);
    printf("ff name: %s\n", buf);

    if (ioctl(fd, EVIOCSFF, &effect) != 0)
        printf("upload ff effect failed, errno=%d(%s)\n", errno, strerror(errno));

#ifdef FF_NEED_WRITE
    memset(&event, 0, sizeof(event));
    event.type = EV_FF;
    event.code = effect.id;
    event.value = 1;
    if (write(fd, &event, sizeof(event)) != sizeof(event))
        printf("write %s failed, errno=%d(%s)\n", __func__, errno, strerror(errno));
#endif

    sleep(time_ms/1000 + 1);

    if (ioctl(fd, EVIOCRMFF, (void *)((long)(effect.id))) != 0)
        printf("erase ff effect failed, errno=%d(%s)\n", errno, strerror(errno));

    close(fd);
    fd = -1;

    return 0;
}
