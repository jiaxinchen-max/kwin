/*
 * SPDX-FileCopyrightText: 2024 Termux LibInput Implementation
 * SPDX-License-Identifier: MIT
 */

#ifndef EVDEV_H
#define EVDEV_H

#include "libinput.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Internal event creation functions */
struct libinput_event *
create_keyboard_event(struct libinput *libinput,
                      struct libinput_device *device,
                      enum libinput_event_type type,
                      uint32_t key,
                      enum libinput_key_state state);

struct libinput_event *
create_pointer_event(struct libinput *libinput,
                     struct libinput_device *device,
                     enum libinput_event_type type,
                     double x, double y);

struct libinput_event *
create_pointer_button_event(struct libinput *libinput,
                           struct libinput_device *device,
                           uint32_t button,
                           enum libinput_button_state state);

struct libinput_event *
create_touch_event(struct libinput *libinput,
                  struct libinput_device *device,
                  enum libinput_event_type type,
                  int32_t slot,
                  double x, double y);

void queue_event(struct libinput *libinput, struct libinput_event *event);

/* Device creation and management */
struct libinput_device *
libinput_device_create(struct libinput *libinput,
                      const char *name,
                      const char *sysname,
                      unsigned int vendor_id,
                      unsigned int product_id,
                      unsigned int bus_type);

void libinput_device_set_capabilities(struct libinput_device *device,
                                    int has_keyboard,
                                    int has_pointer,
                                    int has_touch,
                                    int has_tablet_tool,
                                    int has_tablet_pad,
                                    int has_gesture,
                                    int has_switch);

void libinput_post_event(struct libinput *libinput,
                        struct libinput_event *event);

uint64_t libinput_now_usec(void);

#ifdef __cplusplus
}
#endif

#endif /* EVDEV_H */