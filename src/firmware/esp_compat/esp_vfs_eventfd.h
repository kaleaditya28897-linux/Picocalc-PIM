#ifndef PIM_COMPAT_ESP_VFS_EVENTFD_H
#define PIM_COMPAT_ESP_VFS_EVENTFD_H

#include "esp_err.h"

/* The DERP writer wakes its select() loop through an eventfd. There is no VFS
   here, so ts_platform.c makes the same thing out of a loopback UDP socket:
   registering is a no-op and eventfd()/write() are provided alongside it. */

typedef struct { size_t max_fds; } esp_vfs_eventfd_config_t;

#define ESP_VFS_EVENTD_CONFIG_DEFAULT() { .max_fds = 2 }
#define ESP_VFS_EVENTFD_CONFIG_DEFAULT() { .max_fds = 2 }

esp_err_t esp_vfs_eventfd_register(const esp_vfs_eventfd_config_t *config);
/* Implemented in ts_platform.c as a loopback UDP socket. */
int eventfd(unsigned int initial, int flags);

#endif
