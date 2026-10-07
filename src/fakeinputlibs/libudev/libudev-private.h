/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef _LIBUDEV_PRIVATE_H_
#define _LIBUDEV_PRIVATE_H_

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
#include <syslog.h>
#include <sys/socket.h>
#include <stdint.h>

#ifndef __u32
#define __u32 uint32_t
#endif
/* Mock netlink definitions for non-Linux systems */
#ifdef __linux__
#include <linux/netlink.h>
#else
/* Mock netlink structures for compatibility */
struct sockaddr_nl {
    sa_family_t nl_family;
    unsigned short nl_pad;
    pid_t nl_pid;
    __u32 nl_groups;
};

#define AF_NETLINK 16
#define NETLINK_KOBJECT_UEVENT 15
#define SOL_NETLINK 270
#define NETLINK_PKTINFO 3
#define NETLINK_ADD_MEMBERSHIP 1
#define NETLINK_DROP_MEMBERSHIP 2
#endif

#include "libudev.h"

#define UDEV_EXPORT __attribute__((visibility("default")))
#define UDEV_LIST_STATIC_MAX 128

struct udev {
        int refcount;
        void (*log_fn)(struct udev *udev,
                       int priority, const char *file, int line,
                       const char *fn, const char *format, va_list args);
        void *userdata;
        int log_priority;
};

struct udev_list_entry {
        struct udev_list_entry *next;
        struct udev_list_entry *prev;
        char *name;
        char *value;
        unsigned int num;
};

struct udev_list {
        struct udev *udev;
        struct udev_list_entry *list_entry;
        bool unique;
        unsigned int entries_cur;
        unsigned int entries_max;
};

struct udev_device {
        struct udev *udev;
        int refcount;
        char *syspath;
        char *sysname;
        char *devnode;
        char *subsystem;
        char *devtype;
        char *driver;
        char *action;
        dev_t devnum;
        unsigned long long seqnum;
        unsigned long long usec_initialized;
        int is_initialized;
        struct udev_list devlinks_list;
        struct udev_list properties_list;
        struct udev_list sysattr_list;
        struct udev_list tags_list;
        struct udev_list current_tags_list;
        struct udev_device *parent_device;
        bool parent_set;
        bool info_loaded;
        bool db_loaded;
};

struct udev_enumerate {
        struct udev *udev;
        int refcount;
        struct udev_list devices_list;
        struct udev_list match_subsystem;
        struct udev_list nomatch_subsystem;
        struct udev_list match_sysattr;
        struct udev_list nomatch_sysattr;
        struct udev_list match_property;
        struct udev_list match_tag;
        struct udev_list match_parent;
        struct udev_list match_sysname;
        bool match_is_initialized;
        bool scan_uptodate;
};

struct udev_monitor {
        struct udev *udev;
        int refcount;
        int sock;
        struct sockaddr_nl snl;
        struct sockaddr_nl snl_trusted_sender;
        struct sockaddr_nl snl_destination;
        struct udev_list filter_subsystem_list;
        struct udev_list filter_tag_list;
        bool bound;
        bool enable_receiving;
};

struct udev_queue {
        struct udev *udev;
        int refcount;
        int fd;
};

struct udev_hwdb {
        struct udev *udev;
        int refcount;
        void *hwdb;
};

/* libudev-list.c */
void udev_list_init(struct udev *udev, struct udev_list *list, bool unique);
void udev_list_cleanup(struct udev_list *list);
struct udev_list_entry *udev_list_get_entry(struct udev_list *list);
struct udev_list_entry *udev_list_entry_add(struct udev_list *list, const char *name, const char *value);
void udev_list_entry_delete(struct udev_list_entry *entry);
int udev_list_entry_get_num(struct udev_list_entry *list_entry);
void udev_list_entry_set_num(struct udev_list_entry *list_entry, int num);

/* libudev-util.c */
ssize_t util_get_sys_core_link_value(const char *slink, const char *syspath, char *value, size_t size);
int util_resolve_sys_link(struct udev *udev, char *result, size_t size, const char *syspath, const char *slink);
int util_log_priority(const char *priority);
size_t util_path_encode(const char *src, char *dest, size_t size);
void util_remove_trailing_chars(char *path, char c);
int util_replace_whitespace(const char *str, char *to, size_t len);
int util_replace_chars(char *str, const char *white);
int util_encode_string(const char *str, char *str_enc, size_t len);
unsigned int util_string_hash32(const char *str);
uint64_t util_string_bloom64(const char *str);

#endif