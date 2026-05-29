/*
 * SPDX-FileCopyrightText: 2024 Termux LibInput Implementation
 * SPDX-License-Identifier: MIT
 */

#ifndef LIBINPUT_PRIVATE_H
#define LIBINPUT_PRIVATE_H

#include "libinput.h"
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
struct libinput_seat;

/* Internal libinput context structure */
struct libinput {
    int refcount;
    void *user_data;
    
    const struct libinput_interface *interface;
    struct udev *udev;
    
    /* Event queue */
    struct libinput_event *event_queue_head;
    struct libinput_event *event_queue_tail;
    pthread_mutex_t event_mutex;
    
    /* File descriptor for epoll/select - from termux-display-client */
    int fd;  /* The eventfd from termux-display-client that KWin monitors */
    int termux_event_fd;
    
    /* Devices */
    struct libinput_device *devices;
    int num_devices;
    
    /* Logging */
    enum libinput_log_priority log_priority;
    libinput_log_handler log_handler;
    
    /* State */
    int suspended;
    char *seat_id;
};

/* Internal device structure */
struct libinput_device {
    struct libinput_seat *seat;
    int refcount;
    void *user_data;
    
    char *name;
    char *sysname;
    char *output_name;
    void *udev_device;
    unsigned int vendor_id;
    unsigned int product_id;
    unsigned int bus_type;
    
    /* Capabilities */
    int has_keyboard;
    int has_pointer;
    int has_touch;
    int has_tablet_tool;
    int has_tablet_pad;
    int has_gesture;
    int has_switch;
    
    /* Next device in list */
    struct libinput_device *next;
};

/* Internal seat structure */
struct libinput_seat {
    struct libinput *libinput;
    char *name;
    int refcount;
    void *user_data;
};

/* Internal event structures */
struct libinput_event_keyboard {
    uint32_t key;
    enum libinput_key_state state;
};

struct libinput_event_pointer {
    double dx, dy;
    double dx_unaccel, dy_unaccel;
    double absolute_x, absolute_y;
    uint32_t button;
    enum libinput_button_state button_state;
    enum libinput_pointer_axis axis;
    double axis_value;
    int32_t axis_value_discrete;
    enum libinput_pointer_axis_source axis_source;
    int has_scroll_h;
    int has_scroll_v;
    double scroll_h_value;
    double scroll_v_value;
    double scroll_h_value_v120;
    double scroll_v_value_v120;
};

struct libinput_event_touch {
    int32_t slot;
    double x, y;
    double major, minor;
    double pressure;
    double orientation;
};

struct libinput_event_gesture {
    int finger_count;
    double dx, dy;
    double dx_unaccel, dy_unaccel;
    double scale;
    double angle;
    double angle_delta;
    int cancelled;
};

struct libinput_event_tablet_tool {
    struct libinput_tablet_tool *tool;
    double x, y;
    double dx, dy;
    double pressure;
    double distance;
    double tilt_x, tilt_y;
    double rotation;
    double slider;
    double wheel;
    uint32_t button;
    enum libinput_button_state button_state;
    enum libinput_tablet_tool_tip_state tip_state;
};

struct libinput_event_tablet_pad {
    uint32_t button;
    enum libinput_button_state button_state;
    uint32_t ring_number;
    double ring_position;
    enum libinput_tablet_pad_ring_axis_source ring_source;
    uint32_t strip_number;
    double strip_position;
    enum libinput_tablet_pad_strip_axis_source strip_source;
    int mode;
    uint32_t mode_group;
};

struct libinput_event_switch {
    enum libinput_switch sw;
    enum libinput_switch_state state;
};

/* Main event structure */
struct libinput_event {
    enum libinput_event_type type;
    struct libinput_device *device;
    struct libinput *libinput;
    uint64_t time_usec;
    
    union {
        struct libinput_event_keyboard keyboard;
        struct libinput_event_pointer pointer;
        struct libinput_event_touch touch;
        struct libinput_event_gesture gesture;
        struct libinput_event_tablet_tool tablet_tool;
        struct libinput_event_tablet_pad tablet_pad;
        struct libinput_event_switch switch_event;
    } event;
    
    struct libinput_event *next;
};

/* Internal function declarations */
struct libinput_event *
libinput_event_create(struct libinput_device *device,
                      enum libinput_event_type type,
                      uint64_t time_usec);

struct libinput_device *
libinput_device_create(struct libinput *libinput,
                      const char *name,
                      const char *sysname,
                      unsigned int vendor_id,
                      unsigned int product_id,
                      unsigned int bus_type);

void
libinput_device_set_capabilities(struct libinput_device *device,
                                int has_keyboard,
                                int has_pointer,
                                int has_touch,
                                int has_tablet_tool,
                                int has_tablet_pad,
                                int has_gesture,
                                int has_switch);

uint64_t
libinput_now_usec(void);

void
libinput_post_event(struct libinput *libinput,
                   struct libinput_event *event);

#ifdef __cplusplus
}
#endif

#endif /* LIBINPUT_PRIVATE_H */