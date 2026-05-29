/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include "config.h"
#include "libudev.h"
#include "libudev-private.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * SECTION:libudev-enumerate
 * @short_description: lookup and sort sys devices
 *
 * Lookup devices in the sys filesystem, filter devices by properties,
 * and return a sorted list of devices.
 */

/**
 * udev_enumerate_new:
 * @udev: udev library context
 *
 * Create an enumeration context to scan /sys.
 *
 * Returns: an enumeration context.
 */
struct udev_enumerate *udev_enumerate_new(struct udev *udev) {
        struct udev_enumerate *udev_enumerate;

        if (!udev)
                return NULL;

        udev_enumerate = calloc(1, sizeof(struct udev_enumerate));
        if (!udev_enumerate)
                return NULL;

        udev_enumerate->refcount = 1;
        udev_enumerate->udev = udev_ref(udev);

        udev_list_init(udev, &udev_enumerate->devices_list, false);
        udev_list_init(udev, &udev_enumerate->match_subsystem, true);
        udev_list_init(udev, &udev_enumerate->nomatch_subsystem, true);
        udev_list_init(udev, &udev_enumerate->match_sysattr, false);
        udev_list_init(udev, &udev_enumerate->nomatch_sysattr, false);
        udev_list_init(udev, &udev_enumerate->match_property, false);
        udev_list_init(udev, &udev_enumerate->match_tag, true);
        udev_list_init(udev, &udev_enumerate->match_parent, false);
        udev_list_init(udev, &udev_enumerate->match_sysname, true);

        return udev_enumerate;
}

/**
 * udev_enumerate_ref:
 * @udev_enumerate: context
 *
 * Take a reference of a enumeration context.
 *
 * Returns: the passed enumeration context
 */
struct udev_enumerate *udev_enumerate_ref(struct udev_enumerate *udev_enumerate) {
        if (!udev_enumerate)
                return NULL;
        udev_enumerate->refcount++;
        return udev_enumerate;
}

/**
 * udev_enumerate_unref:
 * @udev_enumerate: context
 *
 * Drop a reference of an enumeration context. If the refcount reaches zero,
 * all resources of the enumeration context will be released.
 *
 * Returns: #NULL
 */
void udev_enumerate_unref(struct udev_enumerate *udev_enumerate) {
        if (!udev_enumerate)
                return;

        udev_enumerate->refcount--;
        if (udev_enumerate->refcount > 0)
                return;

        udev_list_cleanup(&udev_enumerate->devices_list);
        udev_list_cleanup(&udev_enumerate->match_subsystem);
        udev_list_cleanup(&udev_enumerate->nomatch_subsystem);
        udev_list_cleanup(&udev_enumerate->match_sysattr);
        udev_list_cleanup(&udev_enumerate->nomatch_sysattr);
        udev_list_cleanup(&udev_enumerate->match_property);
        udev_list_cleanup(&udev_enumerate->match_tag);
        udev_list_cleanup(&udev_enumerate->match_parent);
        udev_list_cleanup(&udev_enumerate->match_sysname);

        udev_unref(udev_enumerate->udev);
        free(udev_enumerate);
}

/**
 * udev_enumerate_get_udev:
 * @udev_enumerate: context
 *
 * Get the udev library context.
 *
 * Returns: a pointer to the context.
 */
struct udev *udev_enumerate_get_udev(struct udev_enumerate *udev_enumerate) {
        if (!udev_enumerate)
                return NULL;
        return udev_enumerate->udev;
}

/**
 * udev_enumerate_add_match_subsystem:
 * @udev_enumerate: context
 * @subsystem: filter for a subsystem of the device to include in the list
 *
 * Match only devices belonging to a certain kernel subsystem.
 *
 * Returns: 0 on success, otherwise a negative error value.
 */
int udev_enumerate_add_match_subsystem(struct udev_enumerate *udev_enumerate, const char *subsystem) {
        if (!udev_enumerate || !subsystem)
                return -EINVAL;
        udev_list_entry_add(&udev_enumerate->match_subsystem, subsystem, NULL);
        return 0;
}

/**
 * udev_enumerate_add_match_sysname:
 * @udev_enumerate: context
 * @sysname: filter for the name of the device to include in the list
 *
 * Match only devices with a certain /sys device name.
 *
 * Returns: 0 on success, otherwise a negative error value.
 */
int udev_enumerate_add_match_sysname(struct udev_enumerate *udev_enumerate, const char *sysname) {
        if (!udev_enumerate || !sysname)
                return -EINVAL;
        udev_list_entry_add(&udev_enumerate->match_sysname, sysname, NULL);
        return 0;
}

/**
 * udev_enumerate_scan_devices:
 * @udev_enumerate: udev enumeration context
 *
 * Scan /sys for all devices which match the given filters. No matches
 * will return all currently available devices.
 *
 * Returns: 0 on success, otherwise a negative error value.
 */
int udev_enumerate_scan_devices(struct udev_enumerate *udev_enumerate) {
        struct udev_list_entry *list_entry;

        if (!udev_enumerate)
                return -EINVAL;

        /* For Termux/Android, create mock devices based on filters */
        list_entry = udev_list_get_entry(&udev_enumerate->match_subsystem);
        if (list_entry) {
                const char *subsystem = udev_list_entry_get_name(list_entry);
                
                if (strcmp(subsystem, "drm") == 0) {
                        /* Check for sysname filter */
                        struct udev_list_entry *sysname_entry = udev_list_get_entry(&udev_enumerate->match_sysname);
                        if (sysname_entry) {
                                const char *sysname = udev_list_entry_get_name(sysname_entry);
                                
                                /* Match card[0-9] pattern */
                                if (strncmp(sysname, "card", 4) == 0) {
                                        /* Add mock DRM devices */
                                        udev_list_entry_add(&udev_enumerate->devices_list, "/sys/devices/platform/card0", NULL);
                                        udev_list_entry_add(&udev_enumerate->devices_list, "/sys/devices/platform/card1", NULL);
                                }
                        } else {
                                /* No sysname filter, add all DRM devices */
                                udev_list_entry_add(&udev_enumerate->devices_list, "/sys/devices/platform/card0", NULL);
                                udev_list_entry_add(&udev_enumerate->devices_list, "/sys/devices/platform/card1", NULL);
                        }
                }
        } else {
                /* No subsystem filter, add some default devices */
                udev_list_entry_add(&udev_enumerate->devices_list, "/sys/devices/platform/card0", NULL);
        }

        udev_enumerate->scan_uptodate = true;
        return 0;
}

/**
 * udev_enumerate_get_list_entry:
 * @udev_enumerate: context
 *
 * Get the first entry of the sorted list of device paths.
 *
 * Returns: a udev_list_entry.
 */
struct udev_list_entry *udev_enumerate_get_list_entry(struct udev_enumerate *udev_enumerate) {
        if (!udev_enumerate)
                return NULL;
        return udev_list_get_entry(&udev_enumerate->devices_list);
}