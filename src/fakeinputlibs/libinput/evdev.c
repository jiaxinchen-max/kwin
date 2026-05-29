/*
 * SPDX-FileCopyrightText: 2024 Termux LibInput Implementation
 * SPDX-License-Identifier: MIT
 */

#include "libinput.h"
#include "libinput-private.h"
#include "evdev.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

/* Event creation and management - Termux-specific implementations */

struct libinput_event *
libinput_event_create(struct libinput_device *device,
                      enum libinput_event_type type,
                      uint64_t time_usec)
{
    struct libinput_event *event;
    
    event = calloc(1, sizeof(*event));
    if (!event)
        return NULL;
    
    event->type = type;
    event->device = device;
    event->time_usec = time_usec;
    event->next = NULL;
    
    return event;
}

/* Helper function to get current time in microseconds */
uint64_t
libinput_now_usec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000ULL;
}

/* Event creation functions for termux-input-bridge */
struct libinput_event *
create_keyboard_event(struct libinput *libinput,
                     struct libinput_device *device,
                     enum libinput_event_type type,
                     uint32_t key,
                     enum libinput_key_state state)
{
    struct libinput_event *event;
    
    event = libinput_event_create(device, type, libinput_now_usec());
    if (!event)
        return NULL;
    
    event->event.keyboard.key = key;
    event->event.keyboard.state = state;
    
    return event;
}

struct libinput_event *
create_pointer_event(struct libinput *libinput,
                    struct libinput_device *device,
                    enum libinput_event_type type,
                    double x, double y)
{
    struct libinput_event *event;
    
    event = libinput_event_create(device, type, libinput_now_usec());
    if (!event)
        return NULL;
    
    if (type == LIBINPUT_EVENT_POINTER_MOTION) {
        event->event.pointer.dx = x;
        event->event.pointer.dy = y;
        event->event.pointer.dx_unaccel = x;
        event->event.pointer.dy_unaccel = y;
    } else if (type == LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE) {
        event->event.pointer.absolute_x = x;
        event->event.pointer.absolute_y = y;
    }
    
    return event;
}

struct libinput_event *
create_pointer_button_event(struct libinput *libinput,
                           struct libinput_device *device,
                           uint32_t button,
                           enum libinput_button_state state)
{
    struct libinput_event *event;
    
    event = libinput_event_create(device, LIBINPUT_EVENT_POINTER_BUTTON, libinput_now_usec());
    if (!event)
        return NULL;
    
    event->event.pointer.button = button;
    event->event.pointer.button_state = state;
    
    return event;
}

struct libinput_event *
create_touch_event(struct libinput *libinput,
                  struct libinput_device *device,
                  enum libinput_event_type type,
                  int32_t slot,
                  double x, double y)
{
    struct libinput_event *event;
    
    event = libinput_event_create(device, type, libinput_now_usec());
    if (!event)
        return NULL;
    
    event->event.touch.slot = slot;
    event->event.touch.x = x;
    event->event.touch.y = y;
    
    return event;
}

/* Event queue management */
void
queue_event(struct libinput *libinput, struct libinput_event *event)
{
    if (!libinput || !event)
        return;
    
    pthread_mutex_lock(&libinput->event_mutex);
    
    if (!libinput->event_queue_head) {
        libinput->event_queue_head = event;
        libinput->event_queue_tail = event;
    } else {
        libinput->event_queue_tail->next = event;
        libinput->event_queue_tail = event;
    }
    
    pthread_mutex_unlock(&libinput->event_mutex);
}

/* Device creation and management */
struct libinput_device *
libinput_device_create(struct libinput *libinput,
                      const char *name,
                      const char *sysname,
                      unsigned int vendor_id,
                      unsigned int product_id,
                      unsigned int bus_type)
{
    struct libinput_device *device;
    
    device = calloc(1, sizeof(*device));
    if (!device)
        return NULL;
    
    device->refcount = 1;
    device->name = strdup(name ? name : "Unknown Device");
    device->sysname = strdup(sysname ? sysname : "unknown");
    device->vendor_id = vendor_id;
    device->product_id = product_id;
    device->bus_type = bus_type;
    device->next = NULL;
    
    return device;
}

void
libinput_device_set_capabilities(struct libinput_device *device,
                                int has_keyboard,
                                int has_pointer,
                                int has_touch,
                                int has_tablet_tool,
                                int has_tablet_pad,
                                int has_gesture,
                                int has_switch)
{
    if (!device)
        return;
    
    device->has_keyboard = has_keyboard;
    device->has_pointer = has_pointer;
    device->has_touch = has_touch;
    device->has_tablet_tool = has_tablet_tool;
    device->has_tablet_pad = has_tablet_pad;
    device->has_gesture = has_gesture;
    device->has_switch = has_switch;
}

void
libinput_post_event(struct libinput *libinput,
                   struct libinput_event *event)
{
    queue_event(libinput, event);
}
