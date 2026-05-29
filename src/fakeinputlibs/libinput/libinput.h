/*
 * SPDX-FileCopyrightText: 2024 Termux LibInput Implementation
 * SPDX-License-Identifier: MIT
 * 
 * Compatible libinput API implementation for Termux/Android
 * Based on official libinput.h from freedesktop.org
 */

#ifndef LIBINPUT_H
#define LIBINPUT_H

#include <stdint.h>
#include <stdarg.h>
#include <stdlib.h>
#include <libudev.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup base Initialization and manipulation of libinput contexts
 */

/* Forward declarations */
struct libinput;
struct libinput_device;
struct libinput_event;
struct libinput_seat;
struct libinput_tablet_tool;
struct libinput_tablet_pad;
struct libinput_event_tablet_tool;
struct libinput_event_tablet_pad;
struct libinput_tablet_pad_mode_group;
struct libinput_device_group;
struct udev;

struct libinput_config_area_rectangle {
	double x1, y1;
	double x2, y2;
};

/* Event-specific structs */
struct libinput_event_keyboard;
struct libinput_event_pointer;
struct libinput_event_touch;
struct libinput_event_gesture;
struct libinput_event_switch;

/* Complete enum definitions from official libinput */
enum libinput_log_priority {
	LIBINPUT_LOG_PRIORITY_DEBUG = 10,
	LIBINPUT_LOG_PRIORITY_INFO = 20,
	LIBINPUT_LOG_PRIORITY_ERROR = 30,
};

enum libinput_device_capability {
	LIBINPUT_DEVICE_CAP_KEYBOARD = 0,
	LIBINPUT_DEVICE_CAP_POINTER = 1,
	LIBINPUT_DEVICE_CAP_TOUCH = 2,
	LIBINPUT_DEVICE_CAP_TABLET_TOOL = 3,
	LIBINPUT_DEVICE_CAP_TABLET_PAD = 4,
	LIBINPUT_DEVICE_CAP_GESTURE = 5,
	LIBINPUT_DEVICE_CAP_SWITCH = 6,
};

enum libinput_key_state {
	LIBINPUT_KEY_STATE_RELEASED = 0,
	LIBINPUT_KEY_STATE_PRESSED = 1
};

enum libinput_led {
	LIBINPUT_LED_NUM_LOCK = (1 << 0),
	LIBINPUT_LED_CAPS_LOCK = (1 << 1),
	LIBINPUT_LED_SCROLL_LOCK = (1 << 2),
	LIBINPUT_LED_COMPOSE = (1 << 3),
	LIBINPUT_LED_KANA = (1 << 4)
};

enum libinput_button_state {
	LIBINPUT_BUTTON_STATE_RELEASED = 0,
	LIBINPUT_BUTTON_STATE_PRESSED = 1
};

enum libinput_pointer_axis {
	LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL = 0,
	LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL = 1,
};

enum libinput_pointer_axis_source {
	LIBINPUT_POINTER_AXIS_SOURCE_WHEEL = 1,
	LIBINPUT_POINTER_AXIS_SOURCE_FINGER,
	LIBINPUT_POINTER_AXIS_SOURCE_CONTINUOUS,
	LIBINPUT_POINTER_AXIS_SOURCE_WHEEL_TILT,
};

enum libinput_tablet_pad_ring_axis_source {
	LIBINPUT_TABLET_PAD_RING_SOURCE_UNKNOWN = 1,
	LIBINPUT_TABLET_PAD_RING_SOURCE_FINGER,
};

enum libinput_tablet_pad_strip_axis_source {
	LIBINPUT_TABLET_PAD_STRIP_SOURCE_UNKNOWN = 1,
	LIBINPUT_TABLET_PAD_STRIP_SOURCE_FINGER,
};

enum libinput_tablet_tool_type {
	LIBINPUT_TABLET_TOOL_TYPE_PEN = 1,
	LIBINPUT_TABLET_TOOL_TYPE_ERASER,
	LIBINPUT_TABLET_TOOL_TYPE_BRUSH,
	LIBINPUT_TABLET_TOOL_TYPE_PENCIL,
	LIBINPUT_TABLET_TOOL_TYPE_AIRBRUSH,
	LIBINPUT_TABLET_TOOL_TYPE_MOUSE,
	LIBINPUT_TABLET_TOOL_TYPE_LENS,
	LIBINPUT_TABLET_TOOL_TYPE_TOTEM,
};

enum libinput_tablet_tool_proximity_state {
	LIBINPUT_TABLET_TOOL_PROXIMITY_STATE_OUT = 0,
	LIBINPUT_TABLET_TOOL_PROXIMITY_STATE_IN = 1,
};

enum libinput_tablet_tool_tip_state {
	LIBINPUT_TABLET_TOOL_TIP_UP = 0,
	LIBINPUT_TABLET_TOOL_TIP_DOWN = 1,
};

enum libinput_switch {
	LIBINPUT_SWITCH_LID = 1,
	LIBINPUT_SWITCH_TABLET_MODE,
};

enum libinput_switch_state {
	LIBINPUT_SWITCH_STATE_OFF = 0,
	LIBINPUT_SWITCH_STATE_ON = 1,
};

enum libinput_event_type {
	LIBINPUT_EVENT_NONE = 0,

	LIBINPUT_EVENT_DEVICE_ADDED,
	LIBINPUT_EVENT_DEVICE_REMOVED,

	LIBINPUT_EVENT_KEYBOARD_KEY = 300,

	LIBINPUT_EVENT_POINTER_MOTION = 400,
	LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE,
	LIBINPUT_EVENT_POINTER_BUTTON,
	LIBINPUT_EVENT_POINTER_SCROLL_WHEEL,
	LIBINPUT_EVENT_POINTER_SCROLL_FINGER,
	LIBINPUT_EVENT_POINTER_SCROLL_CONTINUOUS,
	LIBINPUT_EVENT_POINTER_AXIS = LIBINPUT_EVENT_POINTER_SCROLL_WHEEL,

	LIBINPUT_EVENT_TOUCH_DOWN = 500,
	LIBINPUT_EVENT_TOUCH_UP,
	LIBINPUT_EVENT_TOUCH_MOTION,
	LIBINPUT_EVENT_TOUCH_CANCEL,
	LIBINPUT_EVENT_TOUCH_FRAME,

	LIBINPUT_EVENT_TABLET_TOOL_AXIS = 600,
	LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY,
	LIBINPUT_EVENT_TABLET_TOOL_TIP,
	LIBINPUT_EVENT_TABLET_TOOL_BUTTON,

	LIBINPUT_EVENT_TABLET_PAD_BUTTON = 700,
	LIBINPUT_EVENT_TABLET_PAD_RING,
	LIBINPUT_EVENT_TABLET_PAD_STRIP,
	LIBINPUT_EVENT_TABLET_PAD_KEY,
	LIBINPUT_EVENT_TABLET_PAD_DIAL,

	LIBINPUT_EVENT_GESTURE_SWIPE_BEGIN = 800,
	LIBINPUT_EVENT_GESTURE_SWIPE_UPDATE,
	LIBINPUT_EVENT_GESTURE_SWIPE_END,
	LIBINPUT_EVENT_GESTURE_PINCH_BEGIN,
	LIBINPUT_EVENT_GESTURE_PINCH_UPDATE,
	LIBINPUT_EVENT_GESTURE_PINCH_END,
	LIBINPUT_EVENT_GESTURE_HOLD_BEGIN,
	LIBINPUT_EVENT_GESTURE_HOLD_END,

	LIBINPUT_EVENT_SWITCH_TOGGLE = 900,
};

/* Configuration enums */
enum libinput_config_status {
	LIBINPUT_CONFIG_STATUS_SUCCESS = 0,
	LIBINPUT_CONFIG_STATUS_UNSUPPORTED,
	LIBINPUT_CONFIG_STATUS_INVALID,
};

enum libinput_config_tap_state {
	LIBINPUT_CONFIG_TAP_DISABLED,
	LIBINPUT_CONFIG_TAP_ENABLED,
};

enum libinput_config_tap_button_map {
	LIBINPUT_CONFIG_TAP_MAP_LRM,
	LIBINPUT_CONFIG_TAP_MAP_LMR,
};

enum libinput_config_drag_state {
	LIBINPUT_CONFIG_DRAG_DISABLED,
	LIBINPUT_CONFIG_DRAG_ENABLED,
};

enum libinput_config_drag_lock_state {
	LIBINPUT_CONFIG_DRAG_LOCK_DISABLED,
	LIBINPUT_CONFIG_DRAG_LOCK_ENABLED,
};

enum libinput_config_scroll_method {
	LIBINPUT_CONFIG_SCROLL_NO_SCROLL = 0,
	LIBINPUT_CONFIG_SCROLL_2FG = (1 << 0),
	LIBINPUT_CONFIG_SCROLL_EDGE = (1 << 1),
	LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN = (1 << 2),
};

enum libinput_config_accel_profile {
	LIBINPUT_CONFIG_ACCEL_PROFILE_NONE = 0,
	LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT = (1 << 0),
	LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE = (1 << 1),
	LIBINPUT_CONFIG_ACCEL_PROFILE_CUSTOM = (1 << 2),
};

enum libinput_config_click_method {
	LIBINPUT_CONFIG_CLICK_METHOD_NONE = 0,
	LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS = (1 << 0),
	LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER = (1 << 1),
};

enum libinput_config_send_events_mode {
	LIBINPUT_CONFIG_SEND_EVENTS_ENABLED,
	LIBINPUT_CONFIG_SEND_EVENTS_DISABLED,
	LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE,
};

enum libinput_config_dwt_state {
	LIBINPUT_CONFIG_DWT_DISABLED,
	LIBINPUT_CONFIG_DWT_ENABLED,
};

enum libinput_config_middle_emulation_state {
	LIBINPUT_CONFIG_MIDDLE_EMULATION_DISABLED,
	LIBINPUT_CONFIG_MIDDLE_EMULATION_ENABLED,
};

/* Duplicate enum definitions removed - they already exist earlier in the file */

/**
 * The libinput interface struct
 */
struct libinput_interface {
	int (*open_restricted)(const char *path, int flags, void *user_data);
	void (*close_restricted)(int fd, void *user_data);
};

/* Core API functions */
struct libinput *
libinput_udev_create_context(const struct libinput_interface *interface,
			     void *user_data,
			     struct udev *udev);

struct libinput *
libinput_path_create_context(const struct libinput_interface *interface,
			     void *user_data);

/* Termux-specific context creation with external event fd */
struct libinput *
libinput_termux_create_context(const struct libinput_interface *interface,
                              void *user_data,
                              int termux_event_fd);

struct libinput_device *
libinput_path_add_device(struct libinput *libinput,
			 const char *path);

void
libinput_path_remove_device(struct libinput_device *device);

int
libinput_udev_assign_seat(struct libinput *libinput,
			  const char *seat_id);

void
libinput_suspend(struct libinput *libinput);

int
libinput_resume(struct libinput *libinput);

struct libinput *
libinput_ref(struct libinput *libinput);

struct libinput *
libinput_unref(struct libinput *libinput);

void
libinput_destroy(struct libinput *libinput);

void
libinput_set_user_data(struct libinput *libinput,
		       void *user_data);

void *
libinput_get_user_data(struct libinput *libinput);

int
libinput_get_fd(struct libinput *libinput);

int
libinput_dispatch(struct libinput *libinput);

struct libinput_event *
libinput_get_event(struct libinput *libinput);

enum libinput_event_type
libinput_next_event_type(struct libinput *libinput);

typedef void (*libinput_log_handler)(struct libinput *libinput,
				     enum libinput_log_priority priority,
				     const char *format, va_list args);

void
libinput_set_log_handler(struct libinput *libinput,
			libinput_log_handler log_handler);

void
libinput_log_set_priority(struct libinput *libinput,
			  enum libinput_log_priority priority);

enum libinput_log_priority
libinput_log_get_priority(const struct libinput *libinput);

void
libinput_log_set_handler(struct libinput *libinput,
			 libinput_log_handler log_handler);

/* Device functions */
struct libinput_device *
libinput_device_ref(struct libinput_device *device);

struct libinput_device *
libinput_device_unref(struct libinput_device *device);

const char *
libinput_device_get_name(struct libinput_device *device);

const char *
libinput_device_get_sysname(struct libinput_device *device);

const char *
libinput_device_get_output_name(struct libinput_device *device);

struct libinput_seat *
libinput_device_get_seat(struct libinput_device *device);

void
libinput_device_set_user_data(struct libinput_device *device, void *user_data);

void *
libinput_device_get_user_data(struct libinput_device *device);

int
libinput_device_has_capability(struct libinput_device *device,
			       enum libinput_device_capability capability);

int
libinput_device_get_size(struct libinput_device *device,
			 double *width,
			 double *height);

unsigned int
libinput_device_get_id_product(struct libinput_device *device);

unsigned int
libinput_device_get_id_vendor(struct libinput_device *device);

unsigned int
libinput_device_get_id_bustype(struct libinput_device *device);

/* Event functions */
void
libinput_event_destroy(struct libinput_event *event);

enum libinput_event_type
libinput_event_get_type(struct libinput_event *event);

struct libinput_device *
libinput_event_get_device(struct libinput_event *event);

struct libinput *
libinput_event_get_context(struct libinput_event *event);

struct libinput_event_pointer *
libinput_event_get_pointer_event(struct libinput_event *event);

struct libinput_event_keyboard *
libinput_event_get_keyboard_event(struct libinput_event *event);

struct libinput_event_touch *
libinput_event_get_touch_event(struct libinput_event *event);

struct libinput_event_gesture *
libinput_event_get_gesture_event(struct libinput_event *event);

struct libinput_event_tablet_tool *
libinput_event_get_tablet_tool_event(struct libinput_event *event);

struct libinput_event_tablet_pad *
libinput_event_get_tablet_pad_event(struct libinput_event *event);

struct libinput_event_switch *
libinput_event_get_switch_event(struct libinput_event *event);

/* Keyboard event functions */
uint32_t
libinput_event_keyboard_get_key(struct libinput_event_keyboard *event);

enum libinput_key_state
libinput_event_keyboard_get_key_state(struct libinput_event_keyboard *event);

uint64_t
libinput_event_keyboard_get_time_usec(struct libinput_event_keyboard *event);

/* Pointer event functions */
double
libinput_event_pointer_get_dx(struct libinput_event_pointer *event);

double
libinput_event_pointer_get_dy(struct libinput_event_pointer *event);

double
libinput_event_pointer_get_absolute_x(struct libinput_event_pointer *event);

double
libinput_event_pointer_get_absolute_y(struct libinput_event_pointer *event);

uint32_t
libinput_event_pointer_get_button(struct libinput_event_pointer *event);

enum libinput_button_state
libinput_event_pointer_get_button_state(struct libinput_event_pointer *event);

int
libinput_event_pointer_has_axis(struct libinput_event_pointer *event,
				enum libinput_pointer_axis axis);

double
libinput_event_pointer_get_axis_value(struct libinput_event_pointer *event,
				      enum libinput_pointer_axis axis);

enum libinput_pointer_axis_source
libinput_event_pointer_get_axis_source(struct libinput_event_pointer *event);

uint64_t
libinput_event_pointer_get_time_usec(struct libinput_event_pointer *event);

double
libinput_event_pointer_get_dx_unaccelerated(struct libinput_event_pointer *event);

double
libinput_event_pointer_get_dy_unaccelerated(struct libinput_event_pointer *event);

double
libinput_event_pointer_get_absolute_x_transformed(struct libinput_event_pointer *event, uint32_t width);

double
libinput_event_pointer_get_absolute_y_transformed(struct libinput_event_pointer *event, uint32_t height);

double
libinput_event_pointer_get_scroll_value(struct libinput_event_pointer *event, enum libinput_pointer_axis axis);

int32_t
libinput_event_pointer_get_scroll_value_v120(struct libinput_event_pointer *event, enum libinput_pointer_axis axis);

/* Touch event functions */
int32_t
libinput_event_touch_get_slot(struct libinput_event_touch *event);

double
libinput_event_touch_get_x(struct libinput_event_touch *event);

double
libinput_event_touch_get_y(struct libinput_event_touch *event);

uint64_t
libinput_event_touch_get_time_usec(struct libinput_event_touch *event);

int32_t
libinput_event_touch_get_seat_slot(struct libinput_event_touch *event);

double
libinput_event_touch_get_x_transformed(struct libinput_event_touch *event, uint32_t width);

double
libinput_event_touch_get_y_transformed(struct libinput_event_touch *event, uint32_t height);

/* Gesture event functions */
uint32_t
libinput_event_gesture_get_finger_count(struct libinput_event_gesture *event);

int
libinput_event_gesture_get_cancelled(struct libinput_event_gesture *event);

uint64_t
libinput_event_gesture_get_time_usec(struct libinput_event_gesture *event);

double
libinput_event_gesture_get_dx_unaccelerated(struct libinput_event_gesture *event);

double
libinput_event_gesture_get_dy_unaccelerated(struct libinput_event_gesture *event);

double
libinput_event_gesture_get_scale(struct libinput_event_gesture *event);

double
libinput_event_gesture_get_angle_delta(struct libinput_event_gesture *event);

uint32_t
libinput_event_gesture_get_time(struct libinput_event_gesture *event);

struct libinput_event *
libinput_event_gesture_get_base_event(struct libinput_event_gesture *event);

double
libinput_event_gesture_get_dx(struct libinput_event_gesture *event);

double
libinput_event_gesture_get_dy(struct libinput_event_gesture *event);

/* Tablet tool functions */
struct libinput_tablet_tool *
libinput_event_tablet_tool_get_tool(struct libinput_event_tablet_tool *event);

struct libinput_event_tablet_tool *
libinput_event_get_tablet_tool_event(struct libinput_event *event);

uint64_t
libinput_tablet_tool_get_serial(struct libinput_tablet_tool *tool);

uint64_t
libinput_tablet_tool_get_tool_id(struct libinput_tablet_tool *tool);

/* Tablet tool event functions */
uint64_t
libinput_event_tablet_tool_get_time_usec(struct libinput_event_tablet_tool *event);

struct libinput_event *
libinput_event_tablet_tool_get_base_event(struct libinput_event_tablet_tool *event);

uint32_t
libinput_event_tablet_tool_get_time(struct libinput_event_tablet_tool *event);

int
libinput_event_tablet_tool_x_has_changed(struct libinput_event_tablet_tool *event);

int
libinput_event_tablet_tool_y_has_changed(struct libinput_event_tablet_tool *event);

int
libinput_event_tablet_tool_pressure_has_changed(struct libinput_event_tablet_tool *event);

int
libinput_event_tablet_tool_distance_has_changed(struct libinput_event_tablet_tool *event);

int
libinput_event_tablet_tool_tilt_x_has_changed(struct libinput_event_tablet_tool *event);

int
libinput_event_tablet_tool_tilt_y_has_changed(struct libinput_event_tablet_tool *event);

int
libinput_event_tablet_tool_rotation_has_changed(struct libinput_event_tablet_tool *event);

int
libinput_event_tablet_tool_slider_has_changed(struct libinput_event_tablet_tool *event);

int
libinput_event_tablet_tool_wheel_has_changed(struct libinput_event_tablet_tool *event);

double
libinput_event_tablet_tool_get_x(struct libinput_event_tablet_tool *event);

double
libinput_event_tablet_tool_get_y(struct libinput_event_tablet_tool *event);

double
libinput_event_tablet_tool_get_dx(struct libinput_event_tablet_tool *event);

double
libinput_event_tablet_tool_get_dy(struct libinput_event_tablet_tool *event);

double
libinput_event_tablet_tool_get_pressure(struct libinput_event_tablet_tool *event);

double
libinput_event_tablet_tool_get_distance(struct libinput_event_tablet_tool *event);

double
libinput_event_tablet_tool_get_tilt_x(struct libinput_event_tablet_tool *event);

double
libinput_event_tablet_tool_get_tilt_y(struct libinput_event_tablet_tool *event);

double
libinput_event_tablet_tool_get_rotation(struct libinput_event_tablet_tool *event);

double
libinput_event_tablet_tool_get_slider_position(struct libinput_event_tablet_tool *event);

double
libinput_event_tablet_tool_get_wheel_delta(struct libinput_event_tablet_tool *event);

int32_t
libinput_event_tablet_tool_get_wheel_delta_discrete(struct libinput_event_tablet_tool *event);

double
libinput_event_tablet_tool_get_x_transformed(struct libinput_event_tablet_tool *event, uint32_t width);

double
libinput_event_tablet_tool_get_y_transformed(struct libinput_event_tablet_tool *event, uint32_t height);

enum libinput_tablet_tool_tip_state
libinput_event_tablet_tool_get_tip_state(struct libinput_event_tablet_tool *event);

enum libinput_tablet_tool_proximity_state
libinput_event_tablet_tool_get_proximity_state(struct libinput_event_tablet_tool *event);

uint32_t
libinput_event_tablet_tool_get_button(struct libinput_event_tablet_tool *event);

enum libinput_button_state
libinput_event_tablet_tool_get_button_state(struct libinput_event_tablet_tool *event);

/* Tablet tool management functions */
struct libinput_tablet_tool *
libinput_tablet_tool_ref(struct libinput_tablet_tool *tool);

struct libinput_tablet_tool *
libinput_tablet_tool_unref(struct libinput_tablet_tool *tool);

/* Tablet tool capability functions */
enum libinput_tablet_tool_type
libinput_tablet_tool_get_type(struct libinput_tablet_tool *tool);

int
libinput_tablet_tool_has_pressure(struct libinput_tablet_tool *tool);

int
libinput_tablet_tool_has_distance(struct libinput_tablet_tool *tool);

int
libinput_tablet_tool_has_rotation(struct libinput_tablet_tool *tool);

int
libinput_tablet_tool_has_tilt(struct libinput_tablet_tool *tool);

int
libinput_tablet_tool_has_slider(struct libinput_tablet_tool *tool);

int
libinput_tablet_tool_has_wheel(struct libinput_tablet_tool *tool);

/* Tablet tool configuration functions */
int
libinput_tablet_tool_config_pressure_range_is_available(struct libinput_tablet_tool *tool);

enum libinput_config_status
libinput_tablet_tool_config_pressure_range_set(struct libinput_tablet_tool *tool, double minimum, double maximum);

double
libinput_tablet_tool_config_pressure_range_get_minimum(struct libinput_tablet_tool *tool);

double
libinput_tablet_tool_config_pressure_range_get_maximum(struct libinput_tablet_tool *tool);

double
libinput_tablet_tool_config_pressure_range_get_default_minimum(struct libinput_tablet_tool *tool);

double
libinput_tablet_tool_config_pressure_range_get_default_maximum(struct libinput_tablet_tool *tool);

/* Device group functions */
struct libinput_device_group *
libinput_device_get_device_group(struct libinput_device *device);

/* Switch functions */
int
libinput_device_switch_has_switch(struct libinput_device *device, enum libinput_switch sw);

/* Pointer functions */
int
libinput_device_pointer_has_button(struct libinput_device *device, uint32_t code);

/* Configuration functions */
int libinput_device_config_tap_get_finger_count(struct libinput_device *device);
enum libinput_config_tap_state libinput_device_config_tap_get_enabled(struct libinput_device *device);
enum libinput_config_tap_state libinput_device_config_tap_get_default_enabled(struct libinput_device *device);
enum libinput_config_status libinput_device_config_tap_set_enabled(struct libinput_device *device, enum libinput_config_tap_state enable);

enum libinput_config_tap_button_map libinput_device_config_tap_get_button_map(struct libinput_device *device);
enum libinput_config_tap_button_map libinput_device_config_tap_get_default_button_map(struct libinput_device *device);
enum libinput_config_status libinput_device_config_tap_set_button_map(struct libinput_device *device, enum libinput_config_tap_button_map map);

enum libinput_config_drag_state libinput_device_config_tap_get_drag_enabled(struct libinput_device *device);
enum libinput_config_drag_state libinput_device_config_tap_get_default_drag_enabled(struct libinput_device *device);
enum libinput_config_status libinput_device_config_tap_set_drag_enabled(struct libinput_device *device, enum libinput_config_drag_state enable);

enum libinput_config_drag_lock_state libinput_device_config_tap_get_drag_lock_enabled(struct libinput_device *device);
enum libinput_config_drag_lock_state libinput_device_config_tap_get_default_drag_lock_enabled(struct libinput_device *device);
enum libinput_config_status libinput_device_config_tap_set_drag_lock_enabled(struct libinput_device *device, enum libinput_config_drag_lock_state enable);

uint32_t libinput_device_config_scroll_get_methods(struct libinput_device *device);
enum libinput_config_scroll_method libinput_device_config_scroll_get_method(struct libinput_device *device);
enum libinput_config_scroll_method libinput_device_config_scroll_get_default_method(struct libinput_device *device);
enum libinput_config_status libinput_device_config_scroll_set_method(struct libinput_device *device, enum libinput_config_scroll_method method);

uint32_t libinput_device_config_scroll_get_button(struct libinput_device *device);
uint32_t libinput_device_config_scroll_get_default_button(struct libinput_device *device);
enum libinput_config_status libinput_device_config_scroll_set_button(struct libinput_device *device, uint32_t button);

int libinput_device_config_scroll_has_natural_scroll(struct libinput_device *device);
int libinput_device_config_scroll_get_natural_scroll_enabled(struct libinput_device *device);
int libinput_device_config_scroll_get_default_natural_scroll_enabled(struct libinput_device *device);
enum libinput_config_status libinput_device_config_scroll_set_natural_scroll_enabled(struct libinput_device *device, int enable);

int libinput_device_config_left_handed_is_available(struct libinput_device *device);
int libinput_device_config_left_handed_get(struct libinput_device *device);
int libinput_device_config_left_handed_get_default(struct libinput_device *device);
enum libinput_config_status libinput_device_config_left_handed_set(struct libinput_device *device, int left_handed);

uint32_t libinput_device_config_click_get_methods(struct libinput_device *device);
enum libinput_config_click_method libinput_device_config_click_get_method(struct libinput_device *device);
enum libinput_config_click_method libinput_device_config_click_get_default_method(struct libinput_device *device);
enum libinput_config_status libinput_device_config_click_set_method(struct libinput_device *device, enum libinput_config_click_method method);

int libinput_device_config_middle_emulation_is_available(struct libinput_device *device);
enum libinput_config_middle_emulation_state libinput_device_config_middle_emulation_get_enabled(struct libinput_device *device);
enum libinput_config_middle_emulation_state libinput_device_config_middle_emulation_get_default_enabled(struct libinput_device *device);
enum libinput_config_status libinput_device_config_middle_emulation_set_enabled(struct libinput_device *device, enum libinput_config_middle_emulation_state enable);

int libinput_device_config_dwt_is_available(struct libinput_device *device);
enum libinput_config_dwt_state libinput_device_config_dwt_get_enabled(struct libinput_device *device);
enum libinput_config_dwt_state libinput_device_config_dwt_get_default_enabled(struct libinput_device *device);
enum libinput_config_status libinput_device_config_dwt_set_enabled(struct libinput_device *device, enum libinput_config_dwt_state enable);

int libinput_device_config_accel_is_available(struct libinput_device *device);
double libinput_device_config_accel_get_speed(struct libinput_device *device);
double libinput_device_config_accel_get_default_speed(struct libinput_device *device);
enum libinput_config_status libinput_device_config_accel_set_speed(struct libinput_device *device, double speed);

uint32_t libinput_device_config_accel_get_profiles(struct libinput_device *device);
enum libinput_config_accel_profile libinput_device_config_accel_get_profile(struct libinput_device *device);
enum libinput_config_accel_profile libinput_device_config_accel_get_default_profile(struct libinput_device *device);
enum libinput_config_status libinput_device_config_accel_set_profile(struct libinput_device *device, enum libinput_config_accel_profile profile);

uint32_t libinput_device_config_send_events_get_modes(struct libinput_device *device);
enum libinput_config_send_events_mode libinput_device_config_send_events_get_mode(struct libinput_device *device);
enum libinput_config_send_events_mode libinput_device_config_send_events_get_default_mode(struct libinput_device *device);
enum libinput_config_status libinput_device_config_send_events_set_mode(struct libinput_device *device, enum libinput_config_send_events_mode mode);

/* Calibration functions */
int libinput_device_config_calibration_has_matrix(struct libinput_device *device);
enum libinput_config_status libinput_device_config_calibration_set_matrix(struct libinput_device *device, const float matrix[6]);
int libinput_device_config_calibration_get_matrix(struct libinput_device *device, float matrix[6]);
int libinput_device_config_calibration_get_default_matrix(struct libinput_device *device, float matrix[6]);

/* Area configuration functions */
struct libinput_config_area_rectangle
libinput_device_config_area_get_rectangle(struct libinput_device *device);

enum libinput_config_status
libinput_device_config_area_set_rectangle(struct libinput_device *device, const struct libinput_config_area_rectangle *rect);

struct libinput_config_area_rectangle
libinput_device_config_area_get_default_rectangle(struct libinput_device *device);

/* Configuration functions */
const char *
libinput_config_status_to_str(enum libinput_config_status status);

/* Tablet pad functions */
int
libinput_device_tablet_pad_get_num_buttons(struct libinput_device *device);

int
libinput_device_tablet_pad_get_num_dials(struct libinput_device *device);

int
libinput_device_tablet_pad_get_num_rings(struct libinput_device *device);

int
libinput_device_tablet_pad_get_num_strips(struct libinput_device *device);

int
libinput_device_tablet_pad_get_num_mode_groups(struct libinput_device *device);

struct libinput_tablet_pad_mode_group *
libinput_device_tablet_pad_get_mode_group(struct libinput_device *device, unsigned int index);

int
libinput_device_tablet_pad_has_key(struct libinput_device *device, uint32_t code);

/* Tablet pad mode group functions */
unsigned int
libinput_tablet_pad_mode_group_get_index(struct libinput_tablet_pad_mode_group *group);

unsigned int
libinput_tablet_pad_mode_group_get_num_modes(struct libinput_tablet_pad_mode_group *group);

unsigned int
libinput_tablet_pad_mode_group_get_mode(struct libinput_tablet_pad_mode_group *group);

int
libinput_tablet_pad_mode_group_has_button(struct libinput_tablet_pad_mode_group *group, unsigned int button);

int
libinput_tablet_pad_mode_group_has_dial(struct libinput_tablet_pad_mode_group *group, unsigned int dial);

int
libinput_tablet_pad_mode_group_has_ring(struct libinput_tablet_pad_mode_group *group, unsigned int ring);

int
libinput_tablet_pad_mode_group_has_strip(struct libinput_tablet_pad_mode_group *group, unsigned int strip);

int
libinput_tablet_pad_mode_group_button_is_toggle(struct libinput_tablet_pad_mode_group *group, unsigned int button);

/* Device LED functions */
void
libinput_device_led_update(struct libinput_device *device, enum libinput_led leds);

/* Area configuration functions */
int
libinput_device_config_area_has_rectangle(struct libinput_device *device);

/* Rotation configuration functions */
int
libinput_device_config_rotation_is_available(struct libinput_device *device);

unsigned int
libinput_device_config_rotation_get_angle(struct libinput_device *device);

enum libinput_config_status
libinput_device_config_rotation_set_angle(struct libinput_device *device, unsigned int degrees_cw);

unsigned int
libinput_device_config_rotation_get_default_angle(struct libinput_device *device);

/* Keyboard functions */
int libinput_device_keyboard_has_key(struct libinput_device *device, uint32_t code);

/* libinput specific udev integration */
struct udev_device *libinput_device_get_udev_device(struct libinput_device *device);

/* Additional missing functions from official libinput API */

/* Context management */
struct libinput *libinput_udev_create_context(const struct libinput_interface *interface, void *user_data, struct udev *udev);
int libinput_udev_assign_seat(struct libinput *libinput, const char *seat_id);
struct libinput *libinput_path_create_context(const struct libinput_interface *interface, void *user_data);
struct libinput_device *libinput_path_add_device(struct libinput *libinput, const char *path);
void libinput_path_remove_device(struct libinput_device *device);
int libinput_get_fd(struct libinput *libinput);
struct libinput_event *libinput_get_event(struct libinput *libinput);
enum libinput_event_type libinput_next_event_type(struct libinput *libinput);
void libinput_set_user_data(struct libinput *libinput, void *user_data);
void *libinput_get_user_data(struct libinput *libinput);
int libinput_resume(struct libinput *libinput);
void libinput_suspend(struct libinput *libinput);
struct libinput *libinput_ref(struct libinput *libinput);
struct libinput *libinput_unref(struct libinput *libinput);
void libinput_log_set_priority(struct libinput *libinput, enum libinput_log_priority priority);
enum libinput_log_priority libinput_log_get_priority(const struct libinput *libinput);
void libinput_log_set_handler(struct libinput *libinput, libinput_log_handler log_handler);

/* Seat management */
struct libinput_seat *libinput_seat_ref(struct libinput_seat *seat);
struct libinput_seat *libinput_seat_unref(struct libinput_seat *seat);
void libinput_seat_set_user_data(struct libinput_seat *seat, void *user_data);
void *libinput_seat_get_user_data(struct libinput_seat *seat);
struct libinput *libinput_seat_get_context(struct libinput_seat *seat);
const char *libinput_seat_get_physical_name(struct libinput_seat *seat);
const char *libinput_seat_get_logical_name(struct libinput_seat *seat);

/* Device management */
struct libinput_device *libinput_device_ref(struct libinput_device *device);
struct libinput_device *libinput_device_unref(struct libinput_device *device);
void libinput_device_set_user_data(struct libinput_device *device, void *user_data);
void *libinput_device_get_user_data(struct libinput_device *device);
struct libinput *libinput_device_get_context(struct libinput_device *device);
const char *libinput_device_get_sysname(struct libinput_device *device);
const char *libinput_device_get_name(struct libinput_device *device);
unsigned int libinput_device_get_id_bustype(struct libinput_device *device);
unsigned int libinput_device_get_id_product(struct libinput_device *device);
unsigned int libinput_device_get_id_vendor(struct libinput_device *device);
const char *libinput_device_get_output_name(struct libinput_device *device);
struct libinput_seat *libinput_device_get_seat(struct libinput_device *device);
int libinput_device_set_seat_logical_name(struct libinput_device *device, const char *name);
int libinput_device_has_capability(struct libinput_device *device, enum libinput_device_capability capability);
int libinput_device_get_size(struct libinput_device *device, double *width, double *height);
int libinput_device_touch_get_touch_count(struct libinput_device *device);

/* Event management */
void libinput_event_destroy(struct libinput_event *event);
enum libinput_event_type libinput_event_get_type(struct libinput_event *event);
struct libinput_device *libinput_event_get_device(struct libinput_event *event);
struct libinput_event_pointer *libinput_event_get_pointer_event(struct libinput_event *event);
struct libinput_event_keyboard *libinput_event_get_keyboard_event(struct libinput_event *event);
struct libinput_event_touch *libinput_event_get_touch_event(struct libinput_event *event);
struct libinput_event_gesture *libinput_event_get_gesture_event(struct libinput_event *event);
struct libinput_event_tablet_tool *libinput_event_get_tablet_tool_event(struct libinput_event *event);
struct libinput_event_tablet_pad *libinput_event_get_tablet_pad_event(struct libinput_event *event);
struct libinput_event_switch *libinput_event_get_switch_event(struct libinput_event *event);
struct libinput_event_device_notify *libinput_event_get_device_notify_event(struct libinput_event *event);
struct libinput_event *libinput_event_device_notify_get_base_event(struct libinput_event_device_notify *event);

/* Keyboard events */
uint32_t libinput_event_keyboard_get_time(struct libinput_event_keyboard *event);
uint32_t libinput_event_keyboard_get_key(struct libinput_event_keyboard *event);
enum libinput_key_state libinput_event_keyboard_get_key_state(struct libinput_event_keyboard *event);
struct libinput_event *libinput_event_keyboard_get_base_event(struct libinput_event_keyboard *event);
uint32_t libinput_event_keyboard_get_seat_key_count(struct libinput_event_keyboard *event);

/* Pointer events */
uint32_t libinput_event_pointer_get_time(struct libinput_event_pointer *event);
double libinput_event_pointer_get_dx(struct libinput_event_pointer *event);
double libinput_event_pointer_get_dy(struct libinput_event_pointer *event);
double libinput_event_pointer_get_absolute_x(struct libinput_event_pointer *event);
double libinput_event_pointer_get_absolute_y(struct libinput_event_pointer *event);
uint32_t libinput_event_pointer_get_button(struct libinput_event_pointer *event);
enum libinput_button_state libinput_event_pointer_get_button_state(struct libinput_event_pointer *event);
uint32_t libinput_event_pointer_get_seat_button_count(struct libinput_event_pointer *event);
int libinput_event_pointer_has_axis(struct libinput_event_pointer *event, enum libinput_pointer_axis axis);
double libinput_event_pointer_get_axis_value(struct libinput_event_pointer *event, enum libinput_pointer_axis axis);
enum libinput_pointer_axis_source libinput_event_pointer_get_axis_source(struct libinput_event_pointer *event);
double libinput_event_pointer_get_axis_value_discrete(struct libinput_event_pointer *event, enum libinput_pointer_axis axis);
struct libinput_event *libinput_event_pointer_get_base_event(struct libinput_event_pointer *event);

/* Touch events */
uint32_t libinput_event_touch_get_time(struct libinput_event_touch *event);
int32_t libinput_event_touch_get_slot(struct libinput_event_touch *event);
double libinput_event_touch_get_x(struct libinput_event_touch *event);
double libinput_event_touch_get_y(struct libinput_event_touch *event);
struct libinput_event *libinput_event_touch_get_base_event(struct libinput_event_touch *event);

/* Gesture events - duplicate declarations removed */

/* Switch events */
enum libinput_switch libinput_event_switch_get_switch(struct libinput_event_switch *event);
enum libinput_switch_state libinput_event_switch_get_switch_state(struct libinput_event_switch *event);
struct libinput_event *libinput_event_switch_get_base_event(struct libinput_event_switch *event);
uint32_t libinput_event_switch_get_time(struct libinput_event_switch *event);
uint64_t libinput_event_switch_get_time_usec(struct libinput_event_switch *event);

/* Tablet pad events */
struct libinput_event *libinput_event_tablet_pad_get_base_event(struct libinput_event_tablet_pad *event);
double libinput_event_tablet_pad_get_ring_position(struct libinput_event_tablet_pad *event);
unsigned int libinput_event_tablet_pad_get_ring_number(struct libinput_event_tablet_pad *event);
enum libinput_tablet_pad_ring_axis_source libinput_event_tablet_pad_get_ring_source(struct libinput_event_tablet_pad *event);
double libinput_event_tablet_pad_get_strip_position(struct libinput_event_tablet_pad *event);
unsigned int libinput_event_tablet_pad_get_strip_number(struct libinput_event_tablet_pad *event);
enum libinput_tablet_pad_strip_axis_source libinput_event_tablet_pad_get_strip_source(struct libinput_event_tablet_pad *event);
uint32_t libinput_event_tablet_pad_get_button_number(struct libinput_event_tablet_pad *event);
enum libinput_button_state libinput_event_tablet_pad_get_button_state(struct libinput_event_tablet_pad *event);
uint32_t libinput_event_tablet_pad_get_key(struct libinput_event_tablet_pad *event);
enum libinput_key_state libinput_event_tablet_pad_get_key_state(struct libinput_event_tablet_pad *event);
unsigned int libinput_event_tablet_pad_get_mode(struct libinput_event_tablet_pad *event);
struct libinput_tablet_pad_mode_group *libinput_event_tablet_pad_get_mode_group(struct libinput_event_tablet_pad *event);
uint32_t libinput_event_tablet_pad_get_time(struct libinput_event_tablet_pad *event);
uint64_t libinput_event_tablet_pad_get_time_usec(struct libinput_event_tablet_pad *event);
double libinput_event_tablet_pad_get_dial_delta_v120(struct libinput_event_tablet_pad *event);
unsigned int libinput_event_tablet_pad_get_dial_number(struct libinput_event_tablet_pad *event);

/* Additional missing functions - duplicate declarations removed to avoid conflicts */

#ifdef __cplusplus
}
#endif

#endif /* LIBINPUT_H */