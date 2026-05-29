/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include "config.h"
#include "libudev.h"
#include "libudev-private.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Android compatibility */
#ifdef __ANDROID__
#ifndef makedev
#define makedev(major, minor) ((dev_t)(((major) << 8) | (minor)))
#endif
#else
#ifdef __linux__
#include <sys/sysmacros.h>
#else
/* Mock makedev for non-Linux systems */
#define makedev(major, minor) (((major) << 8) | (minor))
#endif
#endif

/**
 * SECTION:libudev-device
 * @short_description: kernel sys devices
 *
 * Representation of kernel sys devices. Devices are uniquely identified
 * by their syspath, every device has exactly one path in the kernel sys
 * filesystem. Devices usually belong to a kernel subsystem, and have
 * a unique name inside that subsystem.
 */

static struct udev_device *udev_device_new(struct udev *udev) {
        struct udev_device *udev_device;

        if (!udev)
                return NULL;

        udev_device = calloc(1, sizeof(struct udev_device));
        if (!udev_device)
                return NULL;

        udev_device->refcount = 1;
        udev_device->udev = udev_ref(udev);
        udev_list_init(udev, &udev_device->devlinks_list, true);
        udev_list_init(udev, &udev_device->properties_list, true);
        udev_list_init(udev, &udev_device->sysattr_list, false);
        udev_list_init(udev, &udev_device->tags_list, true);
        udev_list_init(udev, &udev_device->current_tags_list, true);

        return udev_device;
}

/**
 * udev_device_new_from_syspath:
 * @udev: udev library context
 * @syspath: sys device path including sys directory
 *
 * Create new udev device, and fill in information from the sys
 * device and the udev database entry. The syspath is the absolute
 * path to the device, including the sys mount point.
 *
 * The initial refcount is 1, and needs to be decremented to
 * release the resources of the udev device.
 *
 * Returns: a new udev device, or #NULL, if it does not exist
 */
struct udev_device *udev_device_new_from_syspath(struct udev *udev, const char *syspath) {
        struct udev_device *udev_device;
        char *pos;

        if (!udev || !syspath)
                return NULL;

        /* For Termux/Android, we create mock devices */
        udev_device = udev_device_new(udev);
        if (!udev_device)
                return NULL;

        /* Set basic properties */
        udev_device->syspath = strdup(syspath);
        if (!udev_device->syspath) {
                udev_device_unref(udev_device);
                return NULL;
        }

        /* Extract sysname from syspath */
        pos = strrchr(syspath, '/');
        if (pos) {
                udev_device->sysname = strdup(pos + 1);
        } else {
                udev_device->sysname = strdup(syspath);
        }

        /* For Termux, create mock subsystem */
        udev_device->subsystem = strdup("platform");
        udev_device->devtype = strdup("device");
        
        /* Set mock device node for DRM devices */
        if (strstr(syspath, "card")) {
                if (strstr(syspath, "card0")) {
                        udev_device->devnode = strdup("/dev/dri/card0");
                        udev_device->devnum = makedev(226, 0);
                } else if (strstr(syspath, "card1")) {
                        udev_device->devnode = strdup("/dev/dri/card1");
                        udev_device->devnum = makedev(226, 1);
                }
                udev_device->subsystem = strdup("drm");
                
                /* Add mock properties for GPU detection */
                udev_list_entry_add(&udev_device->properties_list, "ID_SEAT", "seat0");
                udev_list_entry_add(&udev_device->properties_list, "DEVTYPE", "drm_minor");
                udev_list_entry_add(&udev_device->properties_list, "MAJOR", "226");
        }

        udev_device->is_initialized = 1;
        
        return udev_device;
}

/**
 * udev_device_new_from_subsystem_sysname:
 * @udev: udev library context
 * @subsystem: the subsystem of the device
 * @sysname: the name of the device
 *
 * Create new udev device, and fill in information from the sys
 * device and the udev database entry.
 *
 * Returns: a new udev device, or #NULL, if it does not exist
 */
struct udev_device *udev_device_new_from_subsystem_sysname(struct udev *udev, const char *subsystem, const char *sysname) {
        struct udev_device *udev_device;
        
        if (!udev || !subsystem || !sysname)
                return NULL;

        udev_device = calloc(1, sizeof(struct udev_device));
        if (!udev_device)
                return NULL;

        udev_device->refcount = 1;
        udev_device->udev = udev_ref(udev);
        udev_device->sysname = strdup(sysname);
        
        /* Create a mock syspath */
        if (asprintf(&udev_device->syspath, "/sys/class/%s/%s", subsystem, sysname) < 0) {
                udev_device->syspath = strdup("/sys/devices/virtual/input/unknown");
        }
        
        /* Set some default properties based on subsystem */
        udev_list_init(udev, &udev_device->properties_list, true);
        if (strcmp(subsystem, "input") == 0) {
                udev_list_entry_add(&udev_device->properties_list, "ID_INPUT", "1");
                udev_list_entry_add(&udev_device->properties_list, "ID_INPUT_KEYBOARD", "1");
                udev_list_entry_add(&udev_device->properties_list, "ID_INPUT_MOUSE", "1");
                udev_list_entry_add(&udev_device->properties_list, "SUBSYSTEM", "input");
        }
        
        return udev_device;
}

/**
 * udev_device_new_from_device_id:
 * @udev: udev library context
 * @id: text string identifying a kernel device
 *
 * Create new udev device, and fill in information from the sys
 * device and the udev database entry.
 *
 * Returns: a new udev device, or #NULL, if it does not exist
 */
struct udev_device *udev_device_new_from_device_id(struct udev *udev, const char *id) {
        (void)udev;  /* unused */
        (void)id;    /* unused */
        
        /* For Termux, we don't support device ID lookup */
        return NULL;
}

/**
 * udev_device_new_from_environment:
 * @udev: udev library context
 *
 * Create new udev device, and fill in information from the current
 * process environment. This only works reliable if the process
 * is called from a udev rule.
 *
 * Returns: a new udev device, or #NULL, if it does not exist
 */
struct udev_device *udev_device_new_from_environment(struct udev *udev) {
        (void)udev;  /* unused */
        
        /* For Termux, we don't support environment-based device creation */
        return NULL;
}

/**
 * udev_device_ref:
 * @udev_device: udev device
 *
 * Take a reference of a udev device.
 *
 * Returns: the passed udev device
 */
struct udev_device *udev_device_ref(struct udev_device *udev_device) {
        if (!udev_device)
                return NULL;
        udev_device->refcount++;
        return udev_device;
}

/**
 * udev_device_unref:
 * @udev_device: udev device
 *
 * Drop a reference of a udev device. If the refcount reaches zero,
 * the resources of the device will be released.
 *
 * Returns: #NULL
 */
void udev_device_unref(struct udev_device *udev_device) {
        if (!udev_device)
                return;
        
        udev_device->refcount--;
        if (udev_device->refcount > 0)
                return;

        if (udev_device->parent_device)
                udev_device_unref(udev_device->parent_device);

        udev_list_cleanup(&udev_device->devlinks_list);
        udev_list_cleanup(&udev_device->properties_list);
        udev_list_cleanup(&udev_device->sysattr_list);
        udev_list_cleanup(&udev_device->tags_list);
        udev_list_cleanup(&udev_device->current_tags_list);

        free(udev_device->syspath);
        free(udev_device->sysname);
        free(udev_device->devnode);
        free(udev_device->subsystem);
        free(udev_device->devtype);
        free(udev_device->driver);
        free(udev_device->action);
        
        udev_unref(udev_device->udev);
        free(udev_device);
}

/**
 * udev_device_get_udev:
 * @udev_device: udev device
 *
 * Retrieve the udev library context the device was created with.
 *
 * Returns: the udev library context
 */
struct udev *udev_device_get_udev(struct udev_device *udev_device) {
        if (!udev_device)
                return NULL;
        return udev_device->udev;
}

/**
 * udev_device_get_syspath:
 * @udev_device: udev device
 *
 * Retrieve the sys path of the udev device. The path does not contain
 * the sys mount point, and starts with a '/'.
 *
 * Returns: the sys path of the udev device
 */
const char *udev_device_get_syspath(struct udev_device *udev_device) {
        if (!udev_device)
                return NULL;
        return udev_device->syspath;
}

/**
 * udev_device_get_sysname:
 * @udev_device: udev device
 *
 * Get the kernel device name in /sys.
 *
 * Returns: the name string of the device device
 */
const char *udev_device_get_sysname(struct udev_device *udev_device) {
        if (!udev_device)
                return NULL;
        return udev_device->sysname;
}

/**
 * udev_device_get_devnode:
 * @udev_device: udev device
 *
 * Retrieve the device node file name belonging to the udev device.
 * The path is an absolute path, and starts with the device directory.
 *
 * Returns: the device node file name of the udev device, or #NULL if no device node exists
 */
const char *udev_device_get_devnode(struct udev_device *udev_device) {
        if (!udev_device)
                return NULL;
        return udev_device->devnode;
}

/**
 * udev_device_get_devnum:
 * @udev_device: udev device
 *
 * Get the device major/minor number.
 *
 * Returns: the dev_t number of the device, or 0 if no device number is available.
 */
dev_t udev_device_get_devnum(struct udev_device *udev_device) {
        if (!udev_device)
                return makedev(0, 0);
        return udev_device->devnum;
}

/**
 * udev_device_get_subsystem:
 * @udev_device: udev device
 *
 * Retrieve the subsystem the device belongs to.
 *
 * Returns: the subsystem name of the udev device, or #NULL if it can not be determined
 */
const char *udev_device_get_subsystem(struct udev_device *udev_device) {
        if (!udev_device)
                return NULL;
        return udev_device->subsystem;
}

/**
 * udev_device_get_devtype:
 * @udev_device: udev device
 *
 * Retrieve the devtype string of the udev device.
 *
 * Returns: the devtype name of the udev device, or #NULL if it can not be determined
 */
const char *udev_device_get_devtype(struct udev_device *udev_device) {
        if (!udev_device)
                return NULL;
        return udev_device->devtype;
}

/**
 * udev_device_get_properties_list_entry:
 * @udev_device: udev device
 *
 * Retrieve the list of key/value device properties of the udev
 * device. The next list entry can be retrieved with
 * udev_list_entry_get_next(), which returns #NULL if no more entries exist.
 * The property name can be retrieved from the list entry by
 * udev_list_entry_get_name(), the property value by
 * udev_list_entry_get_value().
 *
 * Returns: the first entry of the device property list
 */
struct udev_list_entry *udev_device_get_properties_list_entry(struct udev_device *udev_device) {
        if (!udev_device)
                return NULL;
        return udev_list_get_entry(&udev_device->properties_list);
}

/**
 * udev_device_get_property_value:
 * @udev_device: udev device
 * @key: property name
 *
 * Get the value of a given property.
 *
 * Returns: the property string, or #NULL if there is no such property.
 */
const char *udev_device_get_property_value(struct udev_device *udev_device, const char *key) {
        struct udev_list_entry *list_entry;

        if (!udev_device || !key)
                return NULL;

        list_entry = udev_list_get_entry(&udev_device->properties_list);
        list_entry = udev_list_entry_get_by_name(list_entry, key);
        return udev_list_entry_get_value(list_entry);
}

/**
 * udev_device_get_action:
 * @udev_device: udev device
 *
 * This is only valid if the device was received through a monitor. Devices read from
 * sys do not have an action string. Usual actions are: add, remove, change, online,
 * offline.
 *
 * Returns: the kernel action value, or #NULL if there is no action value available.
 */
const char *udev_device_get_action(struct udev_device *udev_device) {
        if (!udev_device)
                return NULL;
        return udev_device->action;
}

/**
 * udev_device_get_sysattr_value:
 * @udev_device: udev device
 * @sysattr: attribute name
 *
 * The retrieved value is cached in the device. Repeated calls will return the same
 * value and not open the attribute again.
 *
 * Returns: the content of the attribute file, or #NULL if the attribute does not exist.
 */
const char *udev_device_get_sysattr_value(struct udev_device *udev_device, const char *sysattr) {
        if (!udev_device || !sysattr)
                return NULL;

        /* For Termux, return mock values for common attributes */
        if (strcmp(sysattr, "boot_vga") == 0) {
                return "1";  /* Mock as boot VGA */
        }
        
        return NULL;
}

/**
 * udev_device_get_parent_with_subsystem_devtype:
 * @udev_device: udev device to start searching from
 * @subsystem: the subsystem of the device
 * @devtype: the type (DEVTYPE) of the device
 *
 * Find the next parent device, with a matching subsystem and devtype value,
 * and return it. It is not necessarily just the next parent device.
 * It is the next parent device which matches the given subsystem and devtype.
 * If devtype is #NULL, only subsystem is checked, and any devtype will match.
 * If subsystem is #NULL, the devtype must match for any subsystem.
 *
 * Returns: a new udev device, or #NULL if no matching parent exists.
 */
struct udev_device *udev_device_get_parent_with_subsystem_devtype(struct udev_device *udev_device,
                                                                  const char *subsystem,
                                                                  const char *devtype) {
        (void)subsystem;  /* unused */
        (void)devtype;    /* unused */
        
        if (!udev_device)
                return NULL;

        /* For Termux, we don't have real parent devices, return NULL */
        return NULL;
}

/**
 * udev_device_has_tag:
 * @udev_device: udev device
 * @tag: tag name
 *
 * Check if a given device has a certain tag associated.
 *
 * Returns: 1 if the tag is found. 0 otherwise.
 */
int udev_device_has_tag(struct udev_device *udev_device, const char *tag) {
        (void)tag;  /* unused */
        
        if (!udev_device)
                return 0;

        /* For Termux, we don't have real tags, always return 0 */
        return 0;
}

/**
 * udev_device_has_current_tag:
 * @udev_device: udev device
 * @tag: tag name
 *
 * Check if a given device has a certain tag associated.
 *
 * Returns: 1 if the tag is found. 0 otherwise.
 */
int udev_device_has_current_tag(struct udev_device *udev_device, const char *tag) {
        (void)tag;  /* unused */
        
        if (!udev_device)
                return 0;

        /* For Termux, we don't have real tags, always return 0 */
        return 0;
}

/**
 * udev_device_get_devlinks_list_entry:
 * @udev_device: udev device
 *
 * Retrieve the list of device links pointing to the device file of the
 * udev device. The next list entry can be retrieved with
 * udev_list_entry_get_next(), which returns #NULL if no more entries exist.
 *
 * Returns: the first entry of the device node link list
 */
struct udev_list_entry *udev_device_get_devlinks_list_entry(struct udev_device *udev_device) {
        if (!udev_device)
                return NULL;
        
        /* For Termux, we don't have real device links, return NULL */
        return NULL;
}

/**
 * udev_device_get_tags_list_entry:
 * @udev_device: udev device
 *
 * Retrieve the list of tags attached to the udev device. The next
 * list entry can be retrieved with udev_list_entry_get_next(),
 * which returns #NULL if no more entries exist. The tag string
 * can be retrieved from the list entry by udev_list_entry_get_name().
 *
 * Returns: the first entry of the tag list
 */
struct udev_list_entry *udev_device_get_tags_list_entry(struct udev_device *udev_device) {
        if (!udev_device)
                return NULL;
        
        /* For Termux, we don't have real tags, return NULL */
        return NULL;
}

/**
 * udev_device_set_sysattr_value:
 * @udev_device: udev device
 * @sysattr: attribute name
 * @value: new value to be set
 *
 * Update the contents of the sys attribute and the cached value of the device.
 *
 * Returns: Negative error code on failure or 0 on success.
 */
int udev_device_set_sysattr_value(struct udev_device *udev_device, const char *sysattr, const char *value) {
        (void)sysattr;  /* unused */
        (void)value;    /* unused */
        
        if (!udev_device)
                return -1;

        /* For Termux, we don't support setting sysattr values, return success */
        return 0;
}

/**
 * udev_device_get_devpath:
 * @udev_device: udev device
 *
 * Retrieve the kernel devpath value of the udev device. The path
 * does not contain the sys mount point, and starts with a '/'.
 *
 * Returns: the devpath of the udev device
 */
const char *udev_device_get_devpath(struct udev_device *udev_device)
{
    if (!udev_device)
        return NULL;
    
    /* Return a mock devpath for Termux */
    return "/devices/mock";
}