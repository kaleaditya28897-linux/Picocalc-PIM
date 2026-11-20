#ifndef PIM_COMPAT_ESP_LWIP_EXT_H
#define PIM_COMPAT_ESP_LWIP_EXT_H

/* ESP-IDF's lwIP fork adds a way to ask "am I the thread that holds the core
   lock?", which upstream uses to decide between calling lwIP directly and
   marshalling the call onto the tcpip thread. Vanilla lwIP has no such call, but
   the question is answerable: the tcpip thread is a FreeRTOS task with a known
   name, so compare handles.

   Force-included into the vendored sources (see CMakeLists.txt) because the
   header upstream would have got it from belongs to a different lwIP. */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/opt.h"

#define LWIP_CORE_LOCK_MARK_HOLDER  0
#define LWIP_CORE_LOCK_UNMARK_HOLDER 1
#define LWIP_CORE_LOCK_QUERY_HOLDER 2

static inline int sys_thread_tcpip(int query) {
    /* The handle is looked up by name once: xTaskGetHandle walks the task
       lists, and this sits on the send path. */
    static TaskHandle_t tcpip_task;
    if (query != LWIP_CORE_LOCK_QUERY_HOLDER) return 0;
    if (tcpip_task == NULL) tcpip_task = xTaskGetHandle(TCPIP_THREAD_NAME);
    return tcpip_task != NULL && tcpip_task == xTaskGetCurrentTaskHandle();
}

#endif
