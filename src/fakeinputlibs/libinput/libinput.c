/*
 * SPDX-FileCopyrightText: 2024 Termux LibInput Implementation
 * SPDX-License-Identifier: MIT
 */

#include "libinput.h"
#include "libinput-private.h"
#include "termux-input-bridge.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>


static void
libinput_log(struct libinput *libinput,
            enum libinput_log_priority priority,
            const char *format, ...)
{
    va_list args;
    
    if (!libinput || priority > libinput->log_priority)
        return;
    
    if (libinput->log_handler) {
        va_start(args, format);
        libinput->log_handler(libinput, priority, format, args);
        va_end(args);
    }
}

/* Termux-specific context creation function */
struct libinput *
libinput_termux_create_context(const struct libinput_interface *interface,
                              void *user_data,
                              int termux_event_fd)
{
    struct libinput *libinput;
    
    if (!interface || !interface->open_restricted || !interface->close_restricted) {
        return NULL;
    }
    
    libinput = calloc(1, sizeof(*libinput));
    if (!libinput)
        return NULL;
        
    libinput->refcount = 1;
    libinput->interface = interface;
    libinput->user_data = user_data;
    libinput->udev = NULL; /* Not used in Termux */
    libinput->log_priority = LIBINPUT_LOG_PRIORITY_ERROR;
    libinput->fd = termux_event_fd;
    libinput->termux_event_fd = termux_event_fd;
    
    if (pthread_mutex_init(&libinput->event_mutex, NULL) != 0) {
        free(libinput);
        return NULL;
    }
    
    /* Initialize devices list */
    libinput->devices = NULL;
    libinput->num_devices = 0;
    
    /* Initialize event queue */
    libinput->event_queue_head = NULL;
    libinput->event_queue_tail = NULL;
    
    /* Initialize termux input bridge */
    if (termux_input_bridge_init(libinput) != 0) {
        libinput_log(libinput, LIBINPUT_LOG_PRIORITY_ERROR,
                    "Failed to initialize termux input bridge\n");
        pthread_mutex_destroy(&libinput->event_mutex);
        free(libinput);
        return NULL;
    }
    
    libinput_log(libinput, LIBINPUT_LOG_PRIORITY_INFO,
                "Termux libinput context created successfully\n");
    
    return libinput;
}

int
libinput_dispatch(struct libinput *libinput)
{
    if (!libinput) {
        return -1;
    }
    
    if (libinput->suspended) {
        return 0;
    }
    
    /* Dispatch events from termux input bridge */
    int ret = termux_input_bridge_dispatch(libinput->termux_event_fd);
    if (ret < 0) {
        libinput_log(libinput, LIBINPUT_LOG_PRIORITY_ERROR,
                    "Failed to dispatch termux input events: %d\n", ret);
    }
    
    return ret;
}

void
libinput_destroy(struct libinput *libinput)
{
    if (!libinput)
        return;
        
    /* Clean up event queue */
    struct libinput_event *event = libinput->event_queue_head;
    while (event) {
        struct libinput_event *next = event->next;
        free(event);
        event = next;
    }
    
    /* Clean up devices */
    struct libinput_device *device = libinput->devices;
    while (device) {
        struct libinput_device *next = device->next;
        if (device->name)
            free(device->name);
        if (device->sysname)
            free(device->sysname);
        if (device->output_name)
            free(device->output_name);
        free(device);
        device = next;
    }
    
    /* Clean up mutex */
    pthread_mutex_destroy(&libinput->event_mutex);
    
    /* Note: We don't close libinput->fd here because it's managed by KWin */
    
    free(libinput);
}

/* All other libinput functions are implemented in libinput-mock-all.c */
