/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include "config.h"
#include "libudev.h"
#include "libudev-private.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/**
 * SECTION:libudev
 * @short_description: libudev context
 *
 * The context contains the default values read from the udev config file,
 * and is passed to all library operations.
 */

/**
 * udev_new:
 *
 * Create udev library context. This reads the udev configuration
 * file, and fills in the default values.
 *
 * The initial refcount is 1, and needs to be decremented to
 * release the resources of the udev library context.
 *
 * Returns: a new udev library context
 */
struct udev *udev_new(void) {
        struct udev *udev;

        udev = calloc(1, sizeof(struct udev));
        if (!udev)
                return NULL;

        udev->refcount = 1;
        udev->log_priority = LOG_ERR;
        udev->userdata = NULL;

        return udev;
}

/**
 * udev_ref:
 * @udev: udev library context
 *
 * Take a reference of the udev library context.
 *
 * Returns: the passed udev library context
 */
struct udev *udev_ref(struct udev *udev) {
        if (!udev)
                return NULL;
        udev->refcount++;
        return udev;
}

/**
 * udev_unref:
 * @udev: udev library context
 *
 * Drop a reference of the udev library context. If the refcount
 * reaches zero, the resources of the context will be released.
 *
 * Returns: the passed udev library context if it has still an active reference, or #NULL otherwise.
 */
void udev_unref(struct udev *udev) {
        if (!udev)
                return;
        
        udev->refcount--;
        if (udev->refcount > 0)
                return;

        free(udev);
}

/**
 * udev_set_log_fn:
 * @udev: udev library context
 * @log_fn: function to be called for logging messages
 *
 * The built-in logging writes to stderr. It can be
 * overridden by a custom function, to plug log messages
 * into the users' logging functionality.
 *
 */
void udev_set_log_fn(struct udev *udev,
                     void (*log_fn)(struct udev *udev,
                                    int priority, const char *file, int line,
                                    const char *fn, const char *format, va_list args)) {
        if (!udev)
                return;
        udev->log_fn = log_fn;
}

/**
 * udev_get_log_priority:
 * @udev: udev library context
 *
 * The initial logging priority is read from the udev config file
 * at startup.
 *
 * Returns: the current logging priority
 */
int udev_get_log_priority(struct udev *udev) {
        if (!udev)
                return LOG_ERR;
        return udev->log_priority;
}

/**
 * udev_set_log_priority:
 * @udev: udev library context
 * @priority: the new logging priority
 *
 * Set the current logging priority. The value controls which messages
 * are logged.
 */
void udev_set_log_priority(struct udev *udev, int priority) {
        if (!udev)
                return;
        udev->log_priority = priority;
}

/**
 * udev_get_userdata:
 * @udev: udev library context
 *
 * Retrieve stored data pointer from library context. This might be useful
 * to access from callbacks.
 *
 * Returns: stored userdata
 */
void *udev_get_userdata(struct udev *udev) {
        if (!udev)
                return NULL;
        return udev->userdata;
}

/**
 * udev_set_userdata:
 * @udev: udev library context
 * @userdata: data pointer
 *
 * Store custom @userdata in the library context.
 */
void udev_set_userdata(struct udev *udev, void *userdata) {
        if (!udev)
                return;
        udev->userdata = userdata;
}