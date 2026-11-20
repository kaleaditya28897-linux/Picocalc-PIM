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

/* True when the calling task holds lwIP's core lock. Defined in ts_platform.c.

   This used to answer a different question - "is this the tcpip thread?" -
   and that is wrong whenever core locking is on, because then any task that
   calls the socket API runs lwIP's code itself while holding the lock. The UI
   task did exactly that inside connect(): its SYN went out through WireGuard,
   the send callback was told "you do not hold the lock", queued the send to
   the tcpip thread and waited - and the tcpip thread could not run without the
   lock the UI task was holding. Every network task stopped, and the idle task
   kept the watchdog fed, so the device simply froze. Only direct paths went
   through this callback, which is why peers reached over DERP still worked. */
int sys_thread_tcpip(int query);

#endif
