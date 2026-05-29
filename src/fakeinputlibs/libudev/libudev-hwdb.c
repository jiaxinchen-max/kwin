/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include "config.h"
#include "libudev.h"
#include "libudev-private.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * SECTION:libudev-hwdb
 * @short_description: retrieve properties from the hardware database
 *
 * Libudev hardware database interface.
 */

/**
 * udev_hwdb_new:
 * @udev: udev library context
 *
 * Create a hardware database context to query properties for devices.
 *
 * Returns: a hwdb context.
 */
struct udev_hwdb *udev_hwdb_new(struct udev *udev) {
        struct udev_hwdb *hwdb;

        if (!udev)
                return NULL;

        hwdb = calloc(1, sizeof(struct udev_hwdb));
        if (!hwdb)
                return NULL;

        hwdb->refcount = 1;
        hwdb->udev = udev_ref(udev);

        return hwdb;
}

/**
 * udev_hwdb_ref:
 * @hwdb: context
 *
 * Take a reference of a hwdb context.
 *
 * Returns: the passed enumeration context
 */
struct udev_hwdb *udev_hwdb_ref(struct udev_hwdb *hwdb) {
        if (!hwdb)
                return NULL;
        hwdb->refcount++;
        return hwdb;
}

/**
 * udev_hwdb_unref:
 * @hwdb: context
 *
 * Drop a reference of a hwdb context. If the refcount reaches zero,
 * all resources of the hwdb context will be released.
 *
 * Returns: #NULL
 */
void udev_hwdb_unref(struct udev_hwdb *hwdb) {
        if (!hwdb)
                return;

        hwdb->refcount--;
        if (hwdb->refcount > 0)
                return;

        udev_unref(hwdb->udev);
        free(hwdb);
}

/**
 * udev_hwdb_get_properties_list_entry:
 * @hwdb: context
 * @modalias: modalias string
 * @flags: (unused)
 *
 * Lookup a matching device in the hardware database. The lookup key is a
 * modalias string, whose formats are defined for the Linux kernel modules.
 * Examples are: pci:v00008086d00001C2D*, usb:v04F2pB221*. The first entry
 * of a list of retrieved properties is returned.
 *
 * Returns: a udev_list_entry.
 */
struct udev_list_entry *udev_hwdb_get_properties_list_entry(struct udev_hwdb *hwdb, const char *modalias, unsigned flags) {
        (void)flags;  /* unused */
        
        if (!hwdb || !modalias)
                return NULL;

        /* For Termux, we don't have a real hardware database, return NULL */
        return NULL;
}