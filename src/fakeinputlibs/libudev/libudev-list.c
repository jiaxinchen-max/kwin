/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include "config.h"
#include "libudev.h"
#include "libudev-private.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * SECTION:libudev-list
 * @short_description: list operation
 *
 * Libudev list operations.
 */

void udev_list_init(struct udev *udev, struct udev_list *list, bool unique) {
        memset(list, 0x00, sizeof(struct udev_list));
        list->udev = udev;
        list->unique = unique;
}

/* add entry to list */
static struct udev_list_entry *list_entry_add(struct udev_list *list, const char *name, const char *value) {
        struct udev_list_entry *entry;

        entry = calloc(1, sizeof(struct udev_list_entry));
        if (!entry)
                return NULL;

        if (name) {
                entry->name = strdup(name);
                if (!entry->name) {
                        free(entry);
                        return NULL;
                }
        }

        if (value) {
                entry->value = strdup(value);
                if (!entry->value) {
                        free(entry->name);
                        free(entry);
                        return NULL;
                }
        }

        if (list->list_entry == NULL) {
                list->list_entry = entry;
                entry->next = entry;
                entry->prev = entry;
        } else {
                entry->next = list->list_entry;
                entry->prev = list->list_entry->prev;
                list->list_entry->prev->next = entry;
                list->list_entry->prev = entry;
        }
        list->entries_cur++;
        return entry;
}

/* remove entry from list */
static void list_entry_remove(struct udev_list_entry *entry) {
        struct udev_list *list = NULL;

        if (entry->next == entry) {
                /* single entry in list */
                if (list)
                        list->list_entry = NULL;
        } else {
                /* remove entry from list */
                entry->next->prev = entry->prev;
                entry->prev->next = entry->next;
                if (list && list->list_entry == entry)
                        list->list_entry = entry->next;
        }
}

void udev_list_cleanup(struct udev_list *list) {
        struct udev_list_entry *entry_loop;
        struct udev_list_entry *entry_tmp;

        if (list->list_entry == NULL)
                return;

        entry_loop = list->list_entry;
        do {
                entry_tmp = entry_loop;
                entry_loop = entry_loop->next;

                free(entry_tmp->name);
                free(entry_tmp->value);
                free(entry_tmp);
        } while (entry_loop != list->list_entry);

        list->list_entry = NULL;
        list->entries_cur = 0;
}

struct udev_list_entry *udev_list_get_entry(struct udev_list *list) {
        if (list->list_entry == NULL)
                return NULL;
        return list->list_entry;
}

/**
 * udev_list_entry_add:
 * @list: udev list
 * @name: name of the entry
 * @value: value of the entry
 *
 * Add an entry to the list, with a name and a value.
 *
 * Returns: a udev_list_entry.
 */
struct udev_list_entry *udev_list_entry_add(struct udev_list *list, const char *name, const char *value) {
        struct udev_list_entry *entry;

        if (list->unique) {
                entry = udev_list_entry_get_by_name(udev_list_get_entry(list), name);
                if (entry != NULL) {
                        free(entry->value);
                        if (value == NULL) {
                                entry->value = NULL;
                                return entry;
                        }
                        entry->value = strdup(value);
                        if (entry->value == NULL)
                                return NULL;
                        return entry;
                }
        }

        entry = list_entry_add(list, name, value);
        return entry;
}

/**
 * udev_list_entry_delete:
 * @entry: udev list entry
 *
 * Delete entry from list.
 */
void udev_list_entry_delete(struct udev_list_entry *entry) {
        if (entry == NULL)
                return;
        list_entry_remove(entry);
        free(entry->name);
        free(entry->value);
        free(entry);
}

/**
 * udev_list_entry_get_next:
 * @list_entry: current entry
 *
 * Get the next entry from the list.
 *
 * Returns: udev_list_entry, #NULL if no more entries in the list.
 */
struct udev_list_entry *udev_list_entry_get_next(struct udev_list_entry *list_entry) {
        struct udev_list_entry *entry_next;

        if (list_entry == NULL)
                return NULL;
        entry_next = list_entry->next;
        if (entry_next == list_entry)
                return NULL;
        return entry_next;
}

/**
 * udev_list_entry_get_by_name:
 * @list_entry: current entry
 * @name: name string to match
 *
 * Lookup an entry in the list with a certain name.
 *
 * Returns: udev_list_entry, #NULL if no matching entry is found.
 */
struct udev_list_entry *udev_list_entry_get_by_name(struct udev_list_entry *list_entry, const char *name) {
        struct udev_list_entry *entry;

        if (list_entry == NULL)
                return NULL;

        entry = list_entry;
        do {
                if (strcmp(entry->name, name) == 0)
                        return entry;
                entry = entry->next;
        } while (entry != list_entry);
        return NULL;
}

/**
 * udev_list_entry_get_name:
 * @list_entry: current entry
 *
 * Get the name of a list entry.
 *
 * Returns: the name string of this entry.
 */
const char *udev_list_entry_get_name(struct udev_list_entry *list_entry) {
        if (list_entry == NULL)
                return NULL;
        return list_entry->name;
}

/**
 * udev_list_entry_get_value:
 * @list_entry: current entry
 *
 * Get the value of list entry.
 *
 * Returns: the value string of this entry.
 */
const char *udev_list_entry_get_value(struct udev_list_entry *list_entry) {
        if (list_entry == NULL)
                return NULL;
        return list_entry->value;
}

int udev_list_entry_get_num(struct udev_list_entry *list_entry) {
        if (list_entry == NULL)
                return -EINVAL;
        return list_entry->num;
}

void udev_list_entry_set_num(struct udev_list_entry *list_entry, int num) {
        if (list_entry == NULL)
                return;
        list_entry->num = num;
}