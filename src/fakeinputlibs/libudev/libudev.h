/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef _LIBUDEV_H_
#define _LIBUDEV_H_

#include <sys/types.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * libudev - Linux userspace device enumeration library
 *
 * Copyright © 2008-2012 Kay Sievers <kay@vrfy.org>
 * Copyright © 2008-2014 Harald Hoyer <harald@redhat.com>
 * Copyright © 2013-2015 Tom Gundersen <teg@jklm.no>
 * Copyright © 2008-2021 systemd contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

struct udev;
struct udev_list_entry;
struct udev_device;
struct udev_monitor;
struct udev_hwdb;
struct udev_enumerate;
struct udev_queue;

/*
 * udev - library context
 */
struct udev *udev_new(void);
struct udev *udev_ref(struct udev *udev);
void udev_unref(struct udev *udev);
void udev_set_log_fn(struct udev *udev,
                     void (*log_fn)(struct udev *udev,
                                    int priority, const char *file, int line,
                                    const char *fn, const char *format, va_list args));
int udev_get_log_priority(struct udev *udev);
void udev_set_log_priority(struct udev *udev, int priority);
void *udev_get_userdata(struct udev *udev);
void udev_set_userdata(struct udev *udev, void *userdata);

/*
 * udev_list
 */
struct udev_list_entry *udev_list_entry_get_next(struct udev_list_entry *list_entry);
struct udev_list_entry *udev_list_entry_get_by_name(struct udev_list_entry *list_entry, const char *name);
const char *udev_list_entry_get_name(struct udev_list_entry *list_entry);
const char *udev_list_entry_get_value(struct udev_list_entry *list_entry);
#define udev_list_entry_foreach(entry, first) \
        for (entry = first; entry != NULL; entry = udev_list_entry_get_next(entry))

/*
 * udev_device
 */
struct udev_device *udev_device_ref(struct udev_device *udev_device);
void udev_device_unref(struct udev_device *udev_device);
struct udev *udev_device_get_udev(struct udev_device *udev_device);
struct udev_device *udev_device_new_from_syspath(struct udev *udev, const char *syspath);
struct udev_device *udev_device_new_from_devnum(struct udev *udev, char type, dev_t devnum);
struct udev_device *udev_device_new_from_subsystem_sysname(struct udev *udev, const char *subsystem, const char *sysname);
struct udev_device *udev_device_new_from_device_id(struct udev *udev, const char *id);
struct udev_device *udev_device_new_from_environment(struct udev *udev);
struct udev_device *udev_device_get_parent(struct udev_device *udev_device);
struct udev_device *udev_device_get_parent_with_subsystem_devtype(struct udev_device *udev_device,
                                                                  const char *subsystem, const char *devtype);
const char *udev_device_get_devpath(struct udev_device *udev_device);
const char *udev_device_get_subsystem(struct udev_device *udev_device);
const char *udev_device_get_devtype(struct udev_device *udev_device);
const char *udev_device_get_syspath(struct udev_device *udev_device);
const char *udev_device_get_sysname(struct udev_device *udev_device);
const char *udev_device_get_sysnum(struct udev_device *udev_device);
const char *udev_device_get_devnode(struct udev_device *udev_device);
int udev_device_get_is_initialized(struct udev_device *udev_device);
struct udev_list_entry *udev_device_get_devlinks_list_entry(struct udev_device *udev_device);
struct udev_list_entry *udev_device_get_properties_list_entry(struct udev_device *udev_device);
struct udev_list_entry *udev_device_get_tags_list_entry(struct udev_device *udev_device);
struct udev_list_entry *udev_device_get_current_tags_list_entry(struct udev_device *udev_device);
struct udev_list_entry *udev_device_get_sysattr_list_entry(struct udev_device *udev_device);
const char *udev_device_get_property_value(struct udev_device *udev_device, const char *key);
const char *udev_device_get_driver(struct udev_device *udev_device);
dev_t udev_device_get_devnum(struct udev_device *udev_device);
const char *udev_device_get_action(struct udev_device *udev_device);
unsigned long long int udev_device_get_seqnum(struct udev_device *udev_device);
unsigned long long int udev_device_get_usec_since_initialized(struct udev_device *udev_device);
const char *udev_device_get_sysattr_value(struct udev_device *udev_device, const char *sysattr);
int udev_device_set_sysattr_value(struct udev_device *udev_device, const char *sysattr, const char *value);
int udev_device_has_tag(struct udev_device *udev_device, const char *tag);
int udev_device_has_current_tag(struct udev_device *udev_device, const char *tag);

/*
 * udev_monitor
 */
struct udev_monitor *udev_monitor_ref(struct udev_monitor *udev_monitor);
void udev_monitor_unref(struct udev_monitor *udev_monitor);
struct udev *udev_monitor_get_udev(struct udev_monitor *udev_monitor);
struct udev_monitor *udev_monitor_new_from_netlink(struct udev *udev, const char *name);
int udev_monitor_enable_receiving(struct udev_monitor *udev_monitor);
int udev_monitor_set_receive_buffer_size(struct udev_monitor *udev_monitor, int size);
int udev_monitor_get_fd(struct udev_monitor *udev_monitor);
struct udev_device *udev_monitor_receive_device(struct udev_monitor *udev_monitor);
int udev_monitor_filter_add_match_subsystem_devtype(struct udev_monitor *udev_monitor,
                                                    const char *subsystem, const char *devtype);
int udev_monitor_filter_add_match_tag(struct udev_monitor *udev_monitor, const char *tag);
int udev_monitor_filter_update(struct udev_monitor *udev_monitor);
int udev_monitor_filter_remove(struct udev_monitor *udev_monitor);

/*
 * udev_enumerate
 */
struct udev_enumerate *udev_enumerate_ref(struct udev_enumerate *udev_enumerate);
void udev_enumerate_unref(struct udev_enumerate *udev_enumerate);
struct udev *udev_enumerate_get_udev(struct udev_enumerate *udev_enumerate);
struct udev_enumerate *udev_enumerate_new(struct udev *udev);
int udev_enumerate_add_match_subsystem(struct udev_enumerate *udev_enumerate, const char *subsystem);
int udev_enumerate_add_nomatch_subsystem(struct udev_enumerate *udev_enumerate, const char *subsystem);
int udev_enumerate_add_match_sysattr(struct udev_enumerate *udev_enumerate, const char *sysattr, const char *value);
int udev_enumerate_add_nomatch_sysattr(struct udev_enumerate *udev_enumerate, const char *sysattr, const char *value);
int udev_enumerate_add_match_property(struct udev_enumerate *udev_enumerate, const char *property, const char *value);
int udev_enumerate_add_match_sysname(struct udev_enumerate *udev_enumerate, const char *sysname);
int udev_enumerate_add_match_tag(struct udev_enumerate *udev_enumerate, const char *tag);
int udev_enumerate_add_match_parent(struct udev_enumerate *udev_enumerate, struct udev_device *parent);
int udev_enumerate_add_match_is_initialized(struct udev_enumerate *udev_enumerate);
int udev_enumerate_add_syspath(struct udev_enumerate *udev_enumerate, const char *syspath);
int udev_enumerate_scan_devices(struct udev_enumerate *udev_enumerate);
int udev_enumerate_scan_subsystems(struct udev_enumerate *udev_enumerate);
struct udev_list_entry *udev_enumerate_get_list_entry(struct udev_enumerate *udev_enumerate);

/*
 * udev_queue
 */
struct udev_queue *udev_queue_ref(struct udev_queue *udev_queue);
void udev_queue_unref(struct udev_queue *udev_queue);
struct udev *udev_queue_get_udev(struct udev_queue *udev_queue);
struct udev_queue *udev_queue_new(struct udev *udev);
unsigned long long int udev_queue_get_kernel_seqnum(struct udev_queue *udev_queue);
unsigned long long int udev_queue_get_udev_seqnum(struct udev_queue *udev_queue);
int udev_queue_get_udev_is_active(struct udev_queue *udev_queue);
int udev_queue_get_queue_is_empty(struct udev_queue *udev_queue);
int udev_queue_get_seqnum_is_finished(struct udev_queue *udev_queue, unsigned long long int seqnum);
int udev_queue_get_seqnum_sequence_is_finished(struct udev_queue *udev_queue,
                                               unsigned long long int start, unsigned long long int end);
int udev_queue_get_fd(struct udev_queue *udev_queue);
int udev_queue_flush(struct udev_queue *udev_queue);

/*
 * udev_hwdb
 */
struct udev_hwdb *udev_hwdb_ref(struct udev_hwdb *hwdb);
void udev_hwdb_unref(struct udev_hwdb *hwdb);
struct udev_hwdb *udev_hwdb_new(struct udev *udev);
struct udev_list_entry *udev_hwdb_get_properties_list_entry(struct udev_hwdb *hwdb, const char *modalias, unsigned flags);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif