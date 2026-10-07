#include "libinput.h"
#include "libinput-private.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>
#include <linux/input-event-codes.h>

/* Mock implementations for all libinput functions */

static struct libinput_event *keyboard_base_event(struct libinput_event_keyboard *event)
{
    return (struct libinput_event *)((char *)event - offsetof(struct libinput_event, event.keyboard));
}

static struct libinput_event *pointer_base_event(struct libinput_event_pointer *event)
{
    return (struct libinput_event *)((char *)event - offsetof(struct libinput_event, event.pointer));
}

static struct libinput_event *touch_base_event(struct libinput_event_touch *event)
{
    return (struct libinput_event *)((char *)event - offsetof(struct libinput_event, event.touch));
}

/* Context management */
struct libinput *libinput_udev_create_context(const struct libinput_interface *interface, void *user_data, struct udev *udev)
{
    (void)interface; (void)user_data; (void)udev;
    return NULL; /* Use libinput_termux_create_context instead */
}

int libinput_udev_assign_seat(struct libinput *libinput, const char *seat_id)
{
    (void)libinput; (void)seat_id;
    return 0;
}

struct libinput *libinput_path_create_context(const struct libinput_interface *interface, void *user_data)
{
    (void)interface; (void)user_data;
    return NULL; /* Use libinput_termux_create_context instead */
}

struct libinput_device *libinput_path_add_device(struct libinput *libinput, const char *path)
{
    (void)libinput; (void)path;
    return NULL;
}

void libinput_path_remove_device(struct libinput_device *device)
{
    (void)device;
}

int libinput_get_fd(struct libinput *libinput)
{
    return libinput ? libinput->fd : -1;
}

struct libinput_event *libinput_get_event(struct libinput *libinput)
{
    struct libinput_event *event;

    if (!libinput)
        return NULL;

    pthread_mutex_lock(&libinput->event_mutex);
    event = libinput->event_queue_head;
    if (event) {
        libinput->event_queue_head = event->next;
        if (!libinput->event_queue_head)
            libinput->event_queue_tail = NULL;
        event->next = NULL;
    }
    pthread_mutex_unlock(&libinput->event_mutex);

    return event;
}

enum libinput_event_type libinput_next_event_type(struct libinput *libinput)
{
    enum libinput_event_type type = LIBINPUT_EVENT_NONE;

    if (!libinput)
        return LIBINPUT_EVENT_NONE;

    pthread_mutex_lock(&libinput->event_mutex);
    if (libinput->event_queue_head)
        type = libinput->event_queue_head->type;
    pthread_mutex_unlock(&libinput->event_mutex);

    return type;
}

void libinput_set_user_data(struct libinput *libinput, void *user_data)
{
    if (libinput) libinput->user_data = user_data;
}

void *libinput_get_user_data(struct libinput *libinput)
{
    return libinput ? libinput->user_data : NULL;
}

int libinput_resume(struct libinput *libinput)
{
    (void)libinput;
    return 0;
}

void libinput_suspend(struct libinput *libinput)
{
    (void)libinput;
}

struct libinput *libinput_ref(struct libinput *libinput)
{
    if (libinput)
        libinput->refcount++;
    return libinput;
}

struct libinput *libinput_unref(struct libinput *libinput)
{
    if (libinput && --libinput->refcount == 0) {
        libinput_destroy(libinput);
        return NULL;
    }
    return libinput;
}

void libinput_log_set_priority(struct libinput *libinput, enum libinput_log_priority priority)
{
    if (libinput)
        libinput->log_priority = priority;
}

enum libinput_log_priority libinput_log_get_priority(const struct libinput *libinput)
{
    return libinput ? libinput->log_priority : LIBINPUT_LOG_PRIORITY_ERROR;
}

void libinput_log_set_handler(struct libinput *libinput, libinput_log_handler log_handler)
{
    if (libinput)
        libinput->log_handler = log_handler;
}

/* Seat management */
struct libinput_seat *libinput_seat_ref(struct libinput_seat *seat)
{
    return seat;
}

struct libinput_seat *libinput_seat_unref(struct libinput_seat *seat)
{
    return seat;
}

void libinput_seat_set_user_data(struct libinput_seat *seat, void *user_data)
{
    (void)seat; (void)user_data;
}

void *libinput_seat_get_user_data(struct libinput_seat *seat)
{
    (void)seat;
    return NULL;
}

struct libinput *libinput_seat_get_context(struct libinput_seat *seat)
{
    (void)seat;
    return NULL;
}

const char *libinput_seat_get_physical_name(struct libinput_seat *seat)
{
    (void)seat;
    return "seat0";
}

const char *libinput_seat_get_logical_name(struct libinput_seat *seat)
{
    (void)seat;
    return "seat0";
}

/* Device management */
struct libinput_device *libinput_device_ref(struct libinput_device *device)
{
    return device;
}

struct libinput_device *libinput_device_unref(struct libinput_device *device)
{
    return device;
}

void libinput_device_set_user_data(struct libinput_device *device, void *user_data)
{
    if (device)
        device->user_data = user_data;
}

void *libinput_device_get_user_data(struct libinput_device *device)
{
    return device ? device->user_data : NULL;
}

struct libinput *libinput_device_get_context(struct libinput_device *device)
{
    (void)device;
    return NULL;
}

const char *libinput_device_get_sysname(struct libinput_device *device)
{
    (void)device;
    return "termux-input";
}

const char *libinput_device_get_name(struct libinput_device *device)
{
    (void)device;
    return "Termux Virtual Input Device";
}

unsigned int libinput_device_get_id_bustype(struct libinput_device *device)
{
    (void)device;
    return 0x06; /* BUS_VIRTUAL */
}

unsigned int libinput_device_get_id_product(struct libinput_device *device)
{
    (void)device;
    return 0x0001;
}

unsigned int libinput_device_get_id_vendor(struct libinput_device *device)
{
    (void)device;
    return 0x0001;
}

const char *libinput_device_get_output_name(struct libinput_device *device)
{
    (void)device;
    return NULL;
}

struct libinput_seat *libinput_device_get_seat(struct libinput_device *device)
{
    (void)device;
    return NULL;
}

int libinput_device_set_seat_logical_name(struct libinput_device *device, const char *name)
{
    (void)device; (void)name;
    return 0;
}

struct udev_device *libinput_device_get_udev_device(struct libinput_device *device)
{
    (void)device;
    return NULL;
}

int libinput_device_has_capability(struct libinput_device *device, enum libinput_device_capability capability)
{
    if (!device)
        return 0;

    switch (capability) {
    case LIBINPUT_DEVICE_CAP_KEYBOARD:
        return device->has_keyboard;
    case LIBINPUT_DEVICE_CAP_POINTER:
        return device->has_pointer;
    case LIBINPUT_DEVICE_CAP_TOUCH:
        return device->has_touch;
    case LIBINPUT_DEVICE_CAP_TABLET_TOOL:
        return device->has_tablet_tool;
    case LIBINPUT_DEVICE_CAP_TABLET_PAD:
        return device->has_tablet_pad;
    case LIBINPUT_DEVICE_CAP_GESTURE:
        return device->has_gesture;
    case LIBINPUT_DEVICE_CAP_SWITCH:
        return device->has_switch;
    default:
        return 0;
    }
}

int libinput_device_get_size(struct libinput_device *device, double *width, double *height)
{
    (void)device;
    if (width) *width = 100.0;
    if (height) *height = 100.0;
    return 0;
}

int libinput_device_pointer_has_button(struct libinput_device *device, uint32_t code)
{
    if (!device || !device->has_pointer)
        return 0;
    return code >= BTN_LEFT && code <= BTN_TASK;
}

int libinput_device_keyboard_has_key(struct libinput_device *device, uint32_t code)
{
    (void)device; (void)code;
    return 1;
}

int libinput_device_touch_get_touch_count(struct libinput_device *device)
{
    (void)device;
    return 10; /* Mock: support 10 touch points */
}

int libinput_device_switch_has_switch(struct libinput_device *device, enum libinput_switch sw)
{
    (void)device; (void)sw;
    return 0;
}

/* Tablet pad functions */
int libinput_device_tablet_pad_get_num_buttons(struct libinput_device *device)
{
    (void)device;
    return 0;
}

int libinput_device_tablet_pad_get_num_dials(struct libinput_device *device)
{
    (void)device;
    return 0;
}

int libinput_device_tablet_pad_get_num_rings(struct libinput_device *device)
{
    (void)device;
    return 0;
}

int libinput_device_tablet_pad_get_num_strips(struct libinput_device *device)
{
    (void)device;
    return 0;
}

int libinput_device_tablet_pad_get_num_mode_groups(struct libinput_device *device)
{
    (void)device;
    return 0;
}

struct libinput_tablet_pad_mode_group *libinput_device_tablet_pad_get_mode_group(struct libinput_device *device, unsigned int index)
{
    (void)device; (void)index;
    return NULL;
}

int libinput_device_tablet_pad_has_key(struct libinput_device *device, uint32_t code)
{
    (void)device; (void)code;
    return 0;
}

/* Device group functions */
struct libinput_device_group *libinput_device_get_device_group(struct libinput_device *device)
{
    (void)device;
    static int dummy_group;
    return (struct libinput_device_group *)&dummy_group;
}

/* Tablet pad mode group functions */
unsigned int libinput_tablet_pad_mode_group_get_index(struct libinput_tablet_pad_mode_group *group)
{
    (void)group;
    return 0;
}

unsigned int libinput_tablet_pad_mode_group_get_num_modes(struct libinput_tablet_pad_mode_group *group)
{
    (void)group;
    return 1;
}

unsigned int libinput_tablet_pad_mode_group_get_mode(struct libinput_tablet_pad_mode_group *group)
{
    (void)group;
    return 0;
}

int libinput_tablet_pad_mode_group_has_button(struct libinput_tablet_pad_mode_group *group, unsigned int button)
{
    (void)group; (void)button;
    return 0;
}

int libinput_tablet_pad_mode_group_has_dial(struct libinput_tablet_pad_mode_group *group, unsigned int dial)
{
    (void)group; (void)dial;
    return 0;
}

int libinput_tablet_pad_mode_group_has_ring(struct libinput_tablet_pad_mode_group *group, unsigned int ring)
{
    (void)group; (void)ring;
    return 0;
}

int libinput_tablet_pad_mode_group_has_strip(struct libinput_tablet_pad_mode_group *group, unsigned int strip)
{
    (void)group; (void)strip;
    return 0;
}

/* Device LED functions */
void libinput_device_led_update(struct libinput_device *device, enum libinput_led leds)
{
    (void)device; (void)leds;
}

/* Event management */
void libinput_event_destroy(struct libinput_event *event)
{
    free(event);
}

enum libinput_event_type libinput_event_get_type(struct libinput_event *event)
{
    return event ? event->type : LIBINPUT_EVENT_NONE;
}

struct libinput_device *libinput_event_get_device(struct libinput_event *event)
{
    return event ? event->device : NULL;
}

struct libinput_event_pointer *libinput_event_get_pointer_event(struct libinput_event *event)
{
    return event ? &event->event.pointer : NULL;
}

struct libinput_event_keyboard *libinput_event_get_keyboard_event(struct libinput_event *event)
{
    return event ? &event->event.keyboard : NULL;
}

struct libinput_event_touch *libinput_event_get_touch_event(struct libinput_event *event)
{
    return event ? &event->event.touch : NULL;
}

struct libinput_event_gesture *libinput_event_get_gesture_event(struct libinput_event *event)
{
    (void)event;
    return NULL;
}

struct libinput_event_tablet_tool *libinput_event_get_tablet_tool_event(struct libinput_event *event)
{
    (void)event;
    return NULL;
}

struct libinput_event_tablet_pad *libinput_event_get_tablet_pad_event(struct libinput_event *event)
{
    (void)event;
    return NULL;
}

struct libinput_event_switch *libinput_event_get_switch_event(struct libinput_event *event)
{
    (void)event;
    return NULL;
}

struct libinput_event_device_notify *libinput_event_get_device_notify_event(struct libinput_event *event)
{
    (void)event;
    return NULL;
}

struct libinput_event *libinput_event_device_notify_get_base_event(struct libinput_event_device_notify *event)
{
    (void)event;
    return NULL;
}

/* Keyboard events */
uint32_t libinput_event_keyboard_get_time(struct libinput_event_keyboard *event)
{
    return (uint32_t)(libinput_event_keyboard_get_time_usec(event) / 1000);
}

uint64_t libinput_event_keyboard_get_time_usec(struct libinput_event_keyboard *event)
{
    return event ? keyboard_base_event(event)->time_usec : 0;
}

uint32_t libinput_event_keyboard_get_key(struct libinput_event_keyboard *event)
{
    return event ? event->key : 0;
}

enum libinput_key_state libinput_event_keyboard_get_key_state(struct libinput_event_keyboard *event)
{
    return event ? event->state : LIBINPUT_KEY_STATE_RELEASED;
}

struct libinput_event *libinput_event_keyboard_get_base_event(struct libinput_event_keyboard *event)
{
    return event ? keyboard_base_event(event) : NULL;
}

uint32_t libinput_event_keyboard_get_seat_key_count(struct libinput_event_keyboard *event)
{
    return event && event->state == LIBINPUT_KEY_STATE_PRESSED ? 1 : 0;
}

/* Pointer events */
uint32_t libinput_event_pointer_get_time(struct libinput_event_pointer *event)
{
    return (uint32_t)(libinput_event_pointer_get_time_usec(event) / 1000);
}

uint64_t libinput_event_pointer_get_time_usec(struct libinput_event_pointer *event)
{
    return event ? pointer_base_event(event)->time_usec : 0;
}

double libinput_event_pointer_get_dx(struct libinput_event_pointer *event)
{
    return event ? event->dx : 0.0;
}

double libinput_event_pointer_get_dy(struct libinput_event_pointer *event)
{
    return event ? event->dy : 0.0;
}

double libinput_event_pointer_get_dx_unaccelerated(struct libinput_event_pointer *event)
{
    return event ? event->dx_unaccel : 0.0;
}

double libinput_event_pointer_get_dy_unaccelerated(struct libinput_event_pointer *event)
{
    return event ? event->dy_unaccel : 0.0;
}

double libinput_event_pointer_get_absolute_x(struct libinput_event_pointer *event)
{
    return event ? event->absolute_x : 0.0;
}

double libinput_event_pointer_get_absolute_y(struct libinput_event_pointer *event)
{
    return event ? event->absolute_y : 0.0;
}

double libinput_event_pointer_get_absolute_x_transformed(struct libinput_event_pointer *event, uint32_t width)
{
    (void)width;
    return event ? event->absolute_x : 0.0;
}

double libinput_event_pointer_get_absolute_y_transformed(struct libinput_event_pointer *event, uint32_t height)
{
    (void)height;
    return event ? event->absolute_y : 0.0;
}

uint32_t libinput_event_pointer_get_button(struct libinput_event_pointer *event)
{
    return event ? event->button : 0;
}

enum libinput_button_state libinput_event_pointer_get_button_state(struct libinput_event_pointer *event)
{
    return event ? event->button_state : LIBINPUT_BUTTON_STATE_RELEASED;
}

uint32_t libinput_event_pointer_get_seat_button_count(struct libinput_event_pointer *event)
{
    return event && event->button_state == LIBINPUT_BUTTON_STATE_PRESSED ? 1 : 0;
}

int libinput_event_pointer_has_axis(struct libinput_event_pointer *event, enum libinput_pointer_axis axis)
{
    (void)event; (void)axis;
    return 0;
}

double libinput_event_pointer_get_axis_value(struct libinput_event_pointer *event, enum libinput_pointer_axis axis)
{
    (void)event; (void)axis;
    return 0.0;
}

enum libinput_pointer_axis_source libinput_event_pointer_get_axis_source(struct libinput_event_pointer *event)
{
    (void)event;
    return LIBINPUT_POINTER_AXIS_SOURCE_WHEEL;
}

double libinput_event_pointer_get_axis_value_discrete(struct libinput_event_pointer *event, enum libinput_pointer_axis axis)
{
    (void)event; (void)axis;
    return 0.0;
}

double libinput_event_pointer_get_scroll_value(struct libinput_event_pointer *event, enum libinput_pointer_axis axis)
{
    (void)event; (void)axis;
    return 0.0;
}

int32_t libinput_event_pointer_get_scroll_value_v120(struct libinput_event_pointer *event, enum libinput_pointer_axis axis)
{
    (void)event; (void)axis;
    return 0;
}

struct libinput_event *libinput_event_pointer_get_base_event(struct libinput_event_pointer *event)
{
    return event ? pointer_base_event(event) : NULL;
}

/* Touch events */
uint32_t libinput_event_touch_get_time(struct libinput_event_touch *event)
{
    return (uint32_t)(libinput_event_touch_get_time_usec(event) / 1000);
}

uint64_t libinput_event_touch_get_time_usec(struct libinput_event_touch *event)
{
    return event ? touch_base_event(event)->time_usec : 0;
}

int32_t libinput_event_touch_get_slot(struct libinput_event_touch *event)
{
    return event ? event->slot : 0;
}

int32_t libinput_event_touch_get_seat_slot(struct libinput_event_touch *event)
{
    return event ? event->slot : 0;
}

double libinput_event_touch_get_x(struct libinput_event_touch *event)
{
    return event ? event->x : 0.0;
}

double libinput_event_touch_get_y(struct libinput_event_touch *event)
{
    return event ? event->y : 0.0;
}

double libinput_event_touch_get_x_transformed(struct libinput_event_touch *event, uint32_t width)
{
    (void)width;
    return event ? event->x : 0.0;
}

double libinput_event_touch_get_y_transformed(struct libinput_event_touch *event, uint32_t height)
{
    (void)height;
    return event ? event->y : 0.0;
}

struct libinput_event *libinput_event_touch_get_base_event(struct libinput_event_touch *event)
{
    return event ? touch_base_event(event) : NULL;
}

/* Gesture events */
uint32_t libinput_event_gesture_get_time(struct libinput_event_gesture *event)
{
    (void)event;
    return 0;
}

uint64_t libinput_event_gesture_get_time_usec(struct libinput_event_gesture *event)
{
    (void)event;
    return 0;
}

struct libinput_event *libinput_event_gesture_get_base_event(struct libinput_event_gesture *event)
{
    (void)event;
    return NULL;
}

uint32_t libinput_event_gesture_get_finger_count(struct libinput_event_gesture *event)
{
    (void)event;
    return 2;
}

int libinput_event_gesture_get_cancelled(struct libinput_event_gesture *event)
{
    (void)event;
    return 0;
}

double libinput_event_gesture_get_dx(struct libinput_event_gesture *event)
{
    (void)event;
    return 0.0;
}

double libinput_event_gesture_get_dy(struct libinput_event_gesture *event)
{
    (void)event;
    return 0.0;
}

double libinput_event_gesture_get_dx_unaccelerated(struct libinput_event_gesture *event)
{
    (void)event;
    return 0.0;
}

double libinput_event_gesture_get_dy_unaccelerated(struct libinput_event_gesture *event)
{
    (void)event;
    return 0.0;
}

double libinput_event_gesture_get_scale(struct libinput_event_gesture *event)
{
    (void)event;
    return 1.0;
}

double libinput_event_gesture_get_angle_delta(struct libinput_event_gesture *event)
{
    (void)event;
    return 0.0;
}

/* Switch events */
enum libinput_switch libinput_event_switch_get_switch(struct libinput_event_switch *event)
{
    (void)event;
    return LIBINPUT_SWITCH_LID;
}

enum libinput_switch_state libinput_event_switch_get_switch_state(struct libinput_event_switch *event)
{
    (void)event;
    return LIBINPUT_SWITCH_STATE_OFF;
}

struct libinput_event *libinput_event_switch_get_base_event(struct libinput_event_switch *event)
{
    (void)event;
    return NULL;
}

uint32_t libinput_event_switch_get_time(struct libinput_event_switch *event)
{
    (void)event;
    return 0;
}

uint64_t libinput_event_switch_get_time_usec(struct libinput_event_switch *event)
{
    (void)event;
    return 0;
}

/* Tablet tool events */
struct libinput_event *libinput_event_tablet_tool_get_base_event(struct libinput_event_tablet_tool *event)
{
    (void)event;
    return NULL;
}

uint32_t libinput_event_tablet_tool_get_time(struct libinput_event_tablet_tool *event)
{
    (void)event;
    return 0;
}

uint64_t libinput_event_tablet_tool_get_time_usec(struct libinput_event_tablet_tool *event)
{
    (void)event;
    return 0;
}

/* Configuration functions */
const char *libinput_config_status_to_str(enum libinput_config_status status)
{
    switch (status) {
    case LIBINPUT_CONFIG_STATUS_SUCCESS:
        return "Success";
    case LIBINPUT_CONFIG_STATUS_UNSUPPORTED:
        return "Unsupported";
    case LIBINPUT_CONFIG_STATUS_INVALID:
        return "Invalid";
    default:
        return "Unknown";
    }
}

/* All configuration functions - comprehensive implementation */
int libinput_device_config_tap_get_finger_count(struct libinput_device *device)
{
    (void)device;
    return 3;
}

enum libinput_config_status libinput_device_config_tap_set_enabled(struct libinput_device *device, enum libinput_config_tap_state enable)
{
    (void)device; (void)enable;
    return LIBINPUT_CONFIG_STATUS_SUCCESS;
}

enum libinput_config_tap_state libinput_device_config_tap_get_enabled(struct libinput_device *device)
{
    (void)device;
    return LIBINPUT_CONFIG_TAP_DISABLED;
}

enum libinput_config_tap_state libinput_device_config_tap_get_default_enabled(struct libinput_device *device)
{
    (void)device;
    return LIBINPUT_CONFIG_TAP_DISABLED;
}

enum libinput_config_status libinput_device_config_tap_set_button_map(struct libinput_device *device, enum libinput_config_tap_button_map map)
{
    (void)device; (void)map;
    return LIBINPUT_CONFIG_STATUS_SUCCESS;
}

enum libinput_config_tap_button_map libinput_device_config_tap_get_button_map(struct libinput_device *device)
{
    (void)device;
    return LIBINPUT_CONFIG_TAP_MAP_LRM;
}

enum libinput_config_tap_button_map libinput_device_config_tap_get_default_button_map(struct libinput_device *device)
{
    (void)device;
    return LIBINPUT_CONFIG_TAP_MAP_LRM;
}

int libinput_device_config_calibration_has_matrix(struct libinput_device *device)
{
    (void)device;
    return 0;
}

enum libinput_config_status libinput_device_config_calibration_set_matrix(struct libinput_device *device, const float matrix[6])
{
    (void)device; (void)matrix;
    return LIBINPUT_CONFIG_STATUS_SUCCESS;
}

int libinput_device_config_calibration_get_matrix(struct libinput_device *device, float matrix[6])
{
    (void)device;
    if (matrix) {
        matrix[0] = 1.0f; matrix[1] = 0.0f; matrix[2] = 0.0f;
        matrix[3] = 0.0f; matrix[4] = 1.0f; matrix[5] = 0.0f;
    }
    return 0;
}

int libinput_device_config_calibration_get_default_matrix(struct libinput_device *device, float matrix[6])
{
    (void)device;
    if (matrix) {
        matrix[0] = 1.0f; matrix[1] = 0.0f; matrix[2] = 0.0f;
        matrix[3] = 0.0f; matrix[4] = 1.0f; matrix[5] = 0.0f;
    }
    return 0;
}

int libinput_device_config_area_has_rectangle(struct libinput_device *device)
{
    (void)device;
    return 0;
}

enum libinput_config_status libinput_device_config_area_set_rectangle(struct libinput_device *device, const struct libinput_config_area_rectangle *rect)
{
    (void)device; (void)rect;
    return LIBINPUT_CONFIG_STATUS_SUCCESS;
}

struct libinput_config_area_rectangle libinput_device_config_area_get_rectangle(struct libinput_device *device)
{
    (void)device;
    struct libinput_config_area_rectangle rect = {0.0, 0.0, 1.0, 1.0};
    return rect;
}

struct libinput_config_area_rectangle libinput_device_config_area_get_default_rectangle(struct libinput_device *device)
{
    (void)device;
    struct libinput_config_area_rectangle rect = {0.0, 0.0, 1.0, 1.0};
    return rect;
}

uint32_t libinput_device_config_send_events_get_modes(struct libinput_device *device)
{
    (void)device;
    return LIBINPUT_CONFIG_SEND_EVENTS_ENABLED;
}

enum libinput_config_status libinput_device_config_send_events_set_mode(struct libinput_device *device, enum libinput_config_send_events_mode mode)
{
    (void)device; (void)mode;
    return LIBINPUT_CONFIG_STATUS_SUCCESS;
}

enum libinput_config_send_events_mode libinput_device_config_send_events_get_mode(struct libinput_device *device)
{
    (void)device;
    return LIBINPUT_CONFIG_SEND_EVENTS_ENABLED;
}

enum libinput_config_send_events_mode libinput_device_config_send_events_get_default_mode(struct libinput_device *device)
{
    (void)device;
    return LIBINPUT_CONFIG_SEND_EVENTS_ENABLED;
}

int libinput_device_config_accel_is_available(struct libinput_device *device)
{
    (void)device;
    return 0;
}

enum libinput_config_status libinput_device_config_accel_set_speed(struct libinput_device *device, double speed)
{
    (void)device; (void)speed;
    return LIBINPUT_CONFIG_STATUS_SUCCESS;
}

double libinput_device_config_accel_get_speed(struct libinput_device *device)
{
    (void)device;
    return 0.0;
}

double libinput_device_config_accel_get_default_speed(struct libinput_device *device)
{
    (void)device;
    return 0.0;
}

uint32_t libinput_device_config_accel_get_profiles(struct libinput_device *device)
{
    (void)device;
    return LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT;
}

enum libinput_config_status libinput_device_config_accel_set_profile(struct libinput_device *device, enum libinput_config_accel_profile profile)
{
    (void)device; (void)profile;
    return LIBINPUT_CONFIG_STATUS_SUCCESS;
}

enum libinput_config_accel_profile libinput_device_config_accel_get_profile(struct libinput_device *device)
{
    (void)device;
    return LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT;
}

enum libinput_config_accel_profile libinput_device_config_accel_get_default_profile(struct libinput_device *device)
{
    (void)device;
    return LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT;
}

int libinput_device_config_scroll_has_natural_scroll(struct libinput_device *device)
{
    (void)device;
    return 0;
}

enum libinput_config_status libinput_device_config_scroll_set_natural_scroll_enabled(struct libinput_device *device, int enable)
{
    (void)device; (void)enable;
    return LIBINPUT_CONFIG_STATUS_SUCCESS;
}

int libinput_device_config_scroll_get_natural_scroll_enabled(struct libinput_device *device)
{
    (void)device;
    return 0;
}

int libinput_device_config_scroll_get_default_natural_scroll_enabled(struct libinput_device *device)
{
    (void)device;
    return 0;
}

uint32_t libinput_device_config_scroll_get_methods(struct libinput_device *device)
{
    (void)device;
    return LIBINPUT_CONFIG_SCROLL_NO_SCROLL;
}

enum libinput_config_status libinput_device_config_scroll_set_method(struct libinput_device *device, enum libinput_config_scroll_method method)
{
    (void)device; (void)method;
    return LIBINPUT_CONFIG_STATUS_SUCCESS;
}

enum libinput_config_scroll_method libinput_device_config_scroll_get_method(struct libinput_device *device)
{
    (void)device;
    return LIBINPUT_CONFIG_SCROLL_NO_SCROLL;
}

enum libinput_config_scroll_method libinput_device_config_scroll_get_default_method(struct libinput_device *device)
{
    (void)device;
    return LIBINPUT_CONFIG_SCROLL_NO_SCROLL;
}

enum libinput_config_status libinput_device_config_scroll_set_button(struct libinput_device *device, uint32_t button)
{
    (void)device; (void)button;
    return LIBINPUT_CONFIG_STATUS_SUCCESS;
}

uint32_t libinput_device_config_scroll_get_button(struct libinput_device *device)
{
    (void)device;
    return 0;
}

uint32_t libinput_device_config_scroll_get_default_button(struct libinput_device *device)
{
    (void)device;
    return 0;
}

int libinput_device_config_left_handed_is_available(struct libinput_device *device)
{
    (void)device;
    return 0;
}

enum libinput_config_status libinput_device_config_left_handed_set(struct libinput_device *device, int left_handed)
{
    (void)device; (void)left_handed;
    return LIBINPUT_CONFIG_STATUS_SUCCESS;
}

int libinput_device_config_left_handed_get(struct libinput_device *device)
{
    (void)device;
    return 0;
}

int libinput_device_config_left_handed_get_default(struct libinput_device *device)
{
    (void)device;
    return 0;
}

uint32_t libinput_device_config_click_get_methods(struct libinput_device *device)
{
    (void)device;
    return LIBINPUT_CONFIG_CLICK_METHOD_NONE;
}

enum libinput_config_status libinput_device_config_click_set_method(struct libinput_device *device, enum libinput_config_click_method method)
{
    (void)device; (void)method;
    return LIBINPUT_CONFIG_STATUS_SUCCESS;
}

enum libinput_config_click_method libinput_device_config_click_get_method(struct libinput_device *device)
{
    (void)device;
    return LIBINPUT_CONFIG_CLICK_METHOD_NONE;
}

enum libinput_config_click_method libinput_device_config_click_get_default_method(struct libinput_device *device)
{
    (void)device;
    return LIBINPUT_CONFIG_CLICK_METHOD_NONE;
}

int libinput_device_config_middle_emulation_is_available(struct libinput_device *device)
{
    (void)device;
    return 0;
}

enum libinput_config_status libinput_device_config_middle_emulation_set_enabled(struct libinput_device *device, enum libinput_config_middle_emulation_state enable)
{
    (void)device; (void)enable;
    return LIBINPUT_CONFIG_STATUS_SUCCESS;
}

enum libinput_config_middle_emulation_state libinput_device_config_middle_emulation_get_enabled(struct libinput_device *device)
{
    (void)device;
    return LIBINPUT_CONFIG_MIDDLE_EMULATION_DISABLED;
}

enum libinput_config_middle_emulation_state libinput_device_config_middle_emulation_get_default_enabled(struct libinput_device *device)
{
    (void)device;
    return LIBINPUT_CONFIG_MIDDLE_EMULATION_DISABLED;
}

int libinput_device_config_dwt_is_available(struct libinput_device *device)
{
    (void)device;
    return 0;
}

enum libinput_config_status libinput_device_config_dwt_set_enabled(struct libinput_device *device, enum libinput_config_dwt_state enable)
{
    (void)device; (void)enable;
    return LIBINPUT_CONFIG_STATUS_SUCCESS;
}

enum libinput_config_dwt_state libinput_device_config_dwt_get_enabled(struct libinput_device *device)
{
    (void)device;
    return LIBINPUT_CONFIG_DWT_DISABLED;
}

enum libinput_config_dwt_state libinput_device_config_dwt_get_default_enabled(struct libinput_device *device)
{
    (void)device;
    return LIBINPUT_CONFIG_DWT_DISABLED;
}

int libinput_device_config_rotation_is_available(struct libinput_device *device)
{
    (void)device;
    return 0;
}

unsigned int libinput_device_config_rotation_get_angle(struct libinput_device *device)
{
    (void)device;
    return 0;
}

enum libinput_config_status libinput_device_config_rotation_set_angle(struct libinput_device *device, unsigned int degrees_cw)
{
    (void)device; (void)degrees_cw;
    return LIBINPUT_CONFIG_STATUS_SUCCESS;
}

unsigned int libinput_device_config_rotation_get_default_angle(struct libinput_device *device)
{
    (void)device;
    return 0;
}

/* Additional tablet pad event functions */
uint32_t libinput_event_tablet_pad_get_time(struct libinput_event_tablet_pad *event)
{
    (void)event;
    return 0;
}

uint64_t libinput_event_tablet_pad_get_time_usec(struct libinput_event_tablet_pad *event)
{
    (void)event;
    return 0;
}

double libinput_event_tablet_pad_get_dial_delta_v120(struct libinput_event_tablet_pad *event)
{
    (void)event;
    return 0.0;
}

unsigned int libinput_event_tablet_pad_get_dial_number(struct libinput_event_tablet_pad *event)
{
    (void)event;
    return 0;
}

struct libinput_event *libinput_event_tablet_pad_get_base_event(struct libinput_event_tablet_pad *event)
{
    (void)event;
    return NULL;
}

double libinput_event_tablet_pad_get_ring_position(struct libinput_event_tablet_pad *event)
{
    (void)event;
    return 0.0;
}

unsigned int libinput_event_tablet_pad_get_ring_number(struct libinput_event_tablet_pad *event)
{
    (void)event;
    return 0;
}

enum libinput_tablet_pad_ring_axis_source libinput_event_tablet_pad_get_ring_source(struct libinput_event_tablet_pad *event)
{
    (void)event;
    return LIBINPUT_TABLET_PAD_RING_SOURCE_UNKNOWN;
}

double libinput_event_tablet_pad_get_strip_position(struct libinput_event_tablet_pad *event)
{
    (void)event;
    return 0.0;
}

unsigned int libinput_event_tablet_pad_get_strip_number(struct libinput_event_tablet_pad *event)
{
    (void)event;
    return 0;
}

enum libinput_tablet_pad_strip_axis_source libinput_event_tablet_pad_get_strip_source(struct libinput_event_tablet_pad *event)
{
    (void)event;
    return LIBINPUT_TABLET_PAD_STRIP_SOURCE_UNKNOWN;
}

uint32_t libinput_event_tablet_pad_get_button_number(struct libinput_event_tablet_pad *event)
{
    (void)event;
    return 0;
}

enum libinput_button_state libinput_event_tablet_pad_get_button_state(struct libinput_event_tablet_pad *event)
{
    (void)event;
    return LIBINPUT_BUTTON_STATE_RELEASED;
}

uint32_t libinput_event_tablet_pad_get_key(struct libinput_event_tablet_pad *event)
{
    (void)event;
    return 0;
}

enum libinput_key_state libinput_event_tablet_pad_get_key_state(struct libinput_event_tablet_pad *event)
{
    (void)event;
    return LIBINPUT_KEY_STATE_RELEASED;
}

unsigned int libinput_event_tablet_pad_get_mode(struct libinput_event_tablet_pad *event)
{
    (void)event;
    return 0;
}

struct libinput_tablet_pad_mode_group *libinput_event_tablet_pad_get_mode_group(struct libinput_event_tablet_pad *event)
{
    (void)event;
    return NULL;
}

/* Tablet pad mode group functions */
int libinput_tablet_pad_mode_group_button_is_toggle(struct libinput_tablet_pad_mode_group *group, unsigned int button)
{
    (void)group; (void)button;
    return 0;
}

/* Tablet tool configuration functions */
int libinput_tablet_tool_config_pressure_range_is_available(struct libinput_tablet_tool *tool)
{
    (void)tool;
    return 0;
}

enum libinput_config_status libinput_tablet_tool_config_pressure_range_set(struct libinput_tablet_tool *tool, double minimum, double maximum)
{
    (void)tool; (void)minimum; (void)maximum;
    return LIBINPUT_CONFIG_STATUS_SUCCESS;
}

double libinput_tablet_tool_config_pressure_range_get_minimum(struct libinput_tablet_tool *tool)
{
    (void)tool;
    return 0.0;
}

double libinput_tablet_tool_config_pressure_range_get_maximum(struct libinput_tablet_tool *tool)
{
    (void)tool;
    return 1.0;
}

double libinput_tablet_tool_config_pressure_range_get_default_minimum(struct libinput_tablet_tool *tool)
{
    (void)tool;
    return 0.0;
}

double libinput_tablet_tool_config_pressure_range_get_default_maximum(struct libinput_tablet_tool *tool)
{
    (void)tool;
    return 1.0;
}

/* Additional tap configuration functions */
enum libinput_config_status libinput_device_config_tap_set_drag_enabled(struct libinput_device *device, enum libinput_config_drag_state enable)
{
    (void)device; (void)enable;
    return LIBINPUT_CONFIG_STATUS_SUCCESS;
}

enum libinput_config_status libinput_device_config_tap_set_drag_lock_enabled(struct libinput_device *device, enum libinput_config_drag_lock_state enable)
{
    (void)device; (void)enable;
    return LIBINPUT_CONFIG_STATUS_SUCCESS;
}

enum libinput_config_drag_state libinput_device_config_tap_get_drag_enabled(struct libinput_device *device)
{
    (void)device;
    return LIBINPUT_CONFIG_DRAG_DISABLED;
}

enum libinput_config_drag_lock_state libinput_device_config_tap_get_default_drag_lock_enabled(struct libinput_device *device)
{
    (void)device;
    return LIBINPUT_CONFIG_DRAG_LOCK_DISABLED;
}

enum libinput_config_drag_lock_state libinput_device_config_tap_get_drag_lock_enabled(struct libinput_device *device)
{
    (void)device;
    return LIBINPUT_CONFIG_DRAG_LOCK_DISABLED;
}

/* Event getter functions - removed duplicates, keeping only the ones already defined above */

/* Tablet tool functions from evdev-mt-touchpad.c that need to be here */
uint64_t libinput_tablet_tool_get_serial(struct libinput_tablet_tool *tool)
{
    (void)tool;
    return 0;
}

uint64_t libinput_tablet_tool_get_tool_id(struct libinput_tablet_tool *tool)
{
    (void)tool;
    return 0;
}

/* Additional tablet tool event functions */
struct libinput_tablet_tool *libinput_event_tablet_tool_get_tool(struct libinput_event_tablet_tool *event)
{
    (void)event;
    return NULL;
}

/* Tablet tool coordinate transformation functions */
double libinput_event_tablet_tool_get_x_transformed(struct libinput_event_tablet_tool *event, uint32_t width)
{
    (void)event; (void)width;
    return 0.0;
}

double libinput_event_tablet_tool_get_y_transformed(struct libinput_event_tablet_tool *event, uint32_t height)
{
    (void)event; (void)height;
    return 0.0;
}

/* Basic tablet tool coordinate functions */
double libinput_event_tablet_tool_get_x(struct libinput_event_tablet_tool *event)
{
    (void)event;
    return 0.0;
}

double libinput_event_tablet_tool_get_y(struct libinput_event_tablet_tool *event)
{
    (void)event;
    return 0.0;
}

double libinput_event_tablet_tool_get_dx(struct libinput_event_tablet_tool *event)
{
    (void)event;
    return 0.0;
}

double libinput_event_tablet_tool_get_dy(struct libinput_event_tablet_tool *event)
{
    (void)event;
    return 0.0;
}

double libinput_event_tablet_tool_get_pressure(struct libinput_event_tablet_tool *event)
{
    (void)event;
    return 0.0;
}

double libinput_event_tablet_tool_get_distance(struct libinput_event_tablet_tool *event)
{
    (void)event;
    return 0.0;
}

double libinput_event_tablet_tool_get_tilt_x(struct libinput_event_tablet_tool *event)
{
    (void)event;
    return 0.0;
}

double libinput_event_tablet_tool_get_tilt_y(struct libinput_event_tablet_tool *event)
{
    (void)event;
    return 0.0;
}

double libinput_event_tablet_tool_get_rotation(struct libinput_event_tablet_tool *event)
{
    (void)event;
    return 0.0;
}

double libinput_event_tablet_tool_get_slider_position(struct libinput_event_tablet_tool *event)
{
    (void)event;
    return 0.0;
}

double libinput_event_tablet_tool_get_wheel_delta(struct libinput_event_tablet_tool *event)
{
    (void)event;
    return 0.0;
}

int libinput_event_tablet_tool_get_wheel_delta_discrete(struct libinput_event_tablet_tool *event)
{
    (void)event;
    return 0;
}

enum libinput_tablet_tool_tip_state libinput_event_tablet_tool_get_tip_state(struct libinput_event_tablet_tool *event)
{
    (void)event;
    return LIBINPUT_TABLET_TOOL_TIP_UP;
}

enum libinput_tablet_tool_proximity_state libinput_event_tablet_tool_get_proximity_state(struct libinput_event_tablet_tool *event)
{
    (void)event;
    return LIBINPUT_TABLET_TOOL_PROXIMITY_STATE_OUT;
}

uint32_t libinput_event_tablet_tool_get_button(struct libinput_event_tablet_tool *event)
{
    (void)event;
    return 0;
}

enum libinput_button_state libinput_event_tablet_tool_get_button_state(struct libinput_event_tablet_tool *event)
{
    (void)event;
    return LIBINPUT_BUTTON_STATE_RELEASED;
}

/* Tablet tool management functions - moved from evdev-mt-touchpad.c */
struct libinput_tablet_tool *libinput_tablet_tool_ref(struct libinput_tablet_tool *tool)
{
    return tool;
}

struct libinput_tablet_tool *libinput_tablet_tool_unref(struct libinput_tablet_tool *tool)
{
    return tool;
}

enum libinput_tablet_tool_type libinput_tablet_tool_get_type(struct libinput_tablet_tool *tool)
{
    (void)tool;
    return LIBINPUT_TABLET_TOOL_TYPE_PEN;
}

int libinput_tablet_tool_has_pressure(struct libinput_tablet_tool *tool)
{
    (void)tool;
    return 0;
}

int libinput_tablet_tool_has_distance(struct libinput_tablet_tool *tool)
{
    (void)tool;
    return 0;
}

int libinput_tablet_tool_has_rotation(struct libinput_tablet_tool *tool)
{
    (void)tool;
    return 0;
}

int libinput_tablet_tool_has_tilt(struct libinput_tablet_tool *tool)
{
    (void)tool;
    return 0;
}

int libinput_tablet_tool_has_slider(struct libinput_tablet_tool *tool)
{
    (void)tool;
    return 0;
}

int libinput_tablet_tool_has_wheel(struct libinput_tablet_tool *tool)
{
    (void)tool;
    return 0;
}
