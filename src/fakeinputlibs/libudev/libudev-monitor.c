/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include "config.h"
#include "libudev.h"
#include "libudev-private.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/**
 * SECTION:libudev-monitor
 * @short_description: device event source
 *
 * Connects to a device event source.
 */

/**
 * udev_monitor_new_from_netlink:
 * @udev: udev library context
 * @name: name of event source
 *
 * Create new udev monitor and connect to a specified event
 * source. Valid sources identifiers are "udev" and "kernel".
 *
 * Applications should usually not connect directly to the
 * "kernel" events, because the devices might not be useable
 * at that time, before udev has configured them, and created
 * device nodes. Accessing devices at the same time as udev,
 * might result in unpredictable behavior. The "udev" events
 * are sent out after udev has finished its event processing,
 * all rules have been processed, and needed device nodes are
 * created.
 *
 * The initial refcount is 1, and needs to be decremented to
 * release the resources of the udev monitor.
 *
 * Returns: a new udev monitor, or #NULL, in case of an error
 */
struct udev_monitor *udev_monitor_new_from_netlink(struct udev *udev, const char *name) {
        struct udev_monitor *udev_monitor;

        if (!udev || !name)
                return NULL;

        udev_monitor = calloc(1, sizeof(struct udev_monitor));
        if (!udev_monitor)
                return NULL;

        udev_monitor->refcount = 1;
        udev_monitor->udev = udev_ref(udev);

        udev_list_init(udev, &udev_monitor->filter_subsystem_list, true);
        udev_list_init(udev, &udev_monitor->filter_tag_list, true);

        /* For Termux, create a dummy socket */
        udev_monitor->sock = socket(AF_UNIX, SOCK_DGRAM
#ifdef SOCK_CLOEXEC
                                           | SOCK_CLOEXEC
#endif
                                           , 0);
        if (udev_monitor->sock < 0) {
                udev_monitor_unref(udev_monitor);
                return NULL;
        }

        return udev_monitor;
}

/**
 * udev_monitor_ref:
 * @udev_monitor: udev monitor
 *
 * Take a reference of a udev monitor.
 *
 * Returns: the passed udev monitor
 */
struct udev_monitor *udev_monitor_ref(struct udev_monitor *udev_monitor) {
        if (!udev_monitor)
                return NULL;
        udev_monitor->refcount++;
        return udev_monitor;
}

/**
 * udev_monitor_unref:
 * @udev_monitor: udev monitor
 *
 * Drop a reference of a udev monitor. If the refcount reaches zero,
 * the bound socket will be closed, and the resources of the monitor
 * will be released.
 *
 * Returns: #NULL
 */
void udev_monitor_unref(struct udev_monitor *udev_monitor) {
        if (!udev_monitor)
                return;

        udev_monitor->refcount--;
        if (udev_monitor->refcount > 0)
                return;

        if (udev_monitor->sock >= 0)
                close(udev_monitor->sock);

        udev_list_cleanup(&udev_monitor->filter_subsystem_list);
        udev_list_cleanup(&udev_monitor->filter_tag_list);
        udev_unref(udev_monitor->udev);
        free(udev_monitor);
}

/**
 * udev_monitor_get_udev:
 * @udev_monitor: udev monitor
 *
 * Retrieve the udev library context the monitor was created with.
 *
 * Returns: the udev library context
 */
struct udev *udev_monitor_get_udev(struct udev_monitor *udev_monitor) {
        if (!udev_monitor)
                return NULL;
        return udev_monitor->udev;
}

/**
 * udev_monitor_get_fd:
 * @udev_monitor: udev monitor
 *
 * Retrieve the socket file descriptor associated with the monitor.
 *
 * Returns: the socket file descriptor
 */
int udev_monitor_get_fd(struct udev_monitor *udev_monitor) {
        if (!udev_monitor)
                return -1;
        return udev_monitor->sock;
}

/**
 * udev_monitor_enable_receiving:
 * @udev_monitor: the monitor which should receive events
 *
 * Binds the @udev_monitor socket to the event source.
 *
 * Returns: 0 on success, otherwise a negative error value.
 */
int udev_monitor_enable_receiving(struct udev_monitor *udev_monitor) {
        if (!udev_monitor)
                return -EINVAL;

        udev_monitor->enable_receiving = true;
        return 0;
}

/**
 * udev_monitor_filter_add_match_subsystem_devtype:
 * @udev_monitor: the monitor
 * @subsystem: the subsystem value to match the incoming devices against
 * @devtype: the devtype value to match the incoming devices against
 *
 * This filter is efficiently executed inside the kernel, and libudev subscribers
 * will usually not be woken up for devices which do not match.
 *
 * The filter must be installed before the monitor is switched to listening mode.
 *
 * Returns: 0 on success, otherwise a negative error value.
 */
int udev_monitor_filter_add_match_subsystem_devtype(struct udev_monitor *udev_monitor,
                                                    const char *subsystem, const char *devtype) {
        if (!udev_monitor || !subsystem)
                return -EINVAL;

        udev_list_entry_add(&udev_monitor->filter_subsystem_list, subsystem, devtype);
        return 0;
}

/**
 * udev_monitor_receive_device:
 * @udev_monitor: udev monitor
 *
 * Receive data from the udev monitor socket, allocate a new udev
 * device, fill in the received data, and return the device.
 *
 * Only socket connections with uid=0 are accepted.
 *
 * The monitor socket is by default set to NONBLOCK. A variant of poll() on
 * the file descriptor returned by udev_monitor_get_fd() should to be used to
 * wake up when new devices arrive, or alternatively the file descriptor
 * switched into blocking mode.
 *
 * The initial refcount is 1, and needs to be decremented to
 * release the resources of the udev device.
 *
 * Returns: a new udev device, or #NULL, in case of an error
 */
struct udev_device *udev_monitor_receive_device(struct udev_monitor *udev_monitor) {
        if (!udev_monitor)
                return NULL;

        /* For Termux, we don't receive real events, return NULL */
        return NULL;
}