/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include "config.h"
#include "libudev.h"
#include "libudev-private.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/**
 * SECTION:libudev-queue
 * @short_description: access to currently active events
 *
 * This exports the current state of the udev processing queue.
 */

/**
 * udev_queue_new:
 * @udev: udev library context
 *
 * The initial refcount is 1, and needs to be decremented to
 * release the resources of the udev queue context.
 *
 * Returns: the udev queue context, or #NULL on error.
 */
struct udev_queue *udev_queue_new(struct udev *udev) {
        struct udev_queue *udev_queue;

        if (!udev)
                return NULL;

        udev_queue = calloc(1, sizeof(struct udev_queue));
        if (!udev_queue)
                return NULL;

        udev_queue->refcount = 1;
        udev_queue->udev = udev_ref(udev);
        udev_queue->fd = -1;

        return udev_queue;
}

/**
 * udev_queue_ref:
 * @udev_queue: udev queue context
 *
 * Take a reference of a udev queue context.
 *
 * Returns: the same udev queue context.
 */
struct udev_queue *udev_queue_ref(struct udev_queue *udev_queue) {
        if (!udev_queue)
                return NULL;
        udev_queue->refcount++;
        return udev_queue;
}

/**
 * udev_queue_unref:
 * @udev_queue: udev queue context
 *
 * Drop a reference of a udev queue context. If the refcount reaches zero,
 * the resources of the queue context will be released.
 *
 * Returns: #NULL
 */
void udev_queue_unref(struct udev_queue *udev_queue) {
        if (!udev_queue)
                return;

        udev_queue->refcount--;
        if (udev_queue->refcount > 0)
                return;

        if (udev_queue->fd >= 0)
                close(udev_queue->fd);
        udev_unref(udev_queue->udev);
        free(udev_queue);
}

/**
 * udev_queue_get_udev:
 * @udev_queue: udev queue context
 *
 * Retrieve the udev library context the queue context was created with.
 *
 * Returns: the udev library context.
 */
struct udev *udev_queue_get_udev(struct udev_queue *udev_queue) {
        if (!udev_queue)
                return NULL;
        return udev_queue->udev;
}

/**
 * udev_queue_get_queue_is_empty:
 * @udev_queue: udev queue context
 *
 * Check if udev is currently processing any events.
 *
 * Returns: 1 if the queue is empty, 0 if events are queued.
 */
int udev_queue_get_queue_is_empty(struct udev_queue *udev_queue) {
        if (!udev_queue)
                return -EINVAL;
        
        /* For Termux, always return empty queue */
        return 1;
}

/**
 * udev_queue_get_fd:
 * @udev_queue: udev queue context
 *
 * Get the file descriptor to watch for a queue to become empty.
 *
 * Returns: a file descriptor, or a negative value on error.
 */
int udev_queue_get_fd(struct udev_queue *udev_queue) {
        if (!udev_queue)
                return -EINVAL;
        return udev_queue->fd;
}