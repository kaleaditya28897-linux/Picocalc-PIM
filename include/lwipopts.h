#ifndef PIM_LWIPOPTS_H
#define PIM_LWIPOPTS_H

/* Two configurations. The default firmware runs lwIP with NO_SYS=1 on the
   cooperative LVGL loop, which is small and needs no RTOS. The Tailscale build
   needs BSD sockets, which only exist with a thread-aware lwIP, so it switches
   to NO_SYS=0 under FreeRTOS. */
#ifdef PIM_TAILSCALE

#define NO_SYS 0
#define LWIP_SOCKET 1
#define LWIP_NETCONN 1
#define LWIP_NETIF_API 1
#define LWIP_RAW 1                      /* ICMP over the tunnel, for tailscale ping */
/* A tailnet is dual stack and every node carries an fd7a: address, which the
   netmap parser reads with lwIP's own IPv6 helpers. Traffic still runs over
   IPv4 only, as upstream documents, but the types have to exist. */
#define LWIP_IPV6 1
#define LWIP_IPV6_DHCP6 0
#define LWIP_IPV6_MLD 1
#define LWIP_ICMP6 1
#define MEMP_NUM_ND6_QUEUE 4
#define LWIP_ND6_NUM_NEIGHBORS 4
#define SYS_LIGHTWEIGHT_PROT 1
#define LWIP_TCPIP_CORE_LOCKING 1
#define LWIP_TCPIP_CORE_LOCKING_INPUT 1
#define TCPIP_THREAD_NAME "tcpip"
#define TCPIP_THREAD_STACKSIZE 2048
/* Below the radio driver's task (CYW43_TASK_PRIORITY, idle + 4) and above the
   UI's. The driver has to be able to interrupt the stack to service the radio;
   it used to be the other way round. */
#define TCPIP_THREAD_PRIO 3
#define TCPIP_MBOX_SIZE 16
#define DEFAULT_TCP_RECVMBOX_SIZE 16
#define DEFAULT_UDP_RECVMBOX_SIZE 16
#define DEFAULT_RAW_RECVMBOX_SIZE 8
#define DEFAULT_ACCEPTMBOX_SIZE 8
#define DEFAULT_THREAD_STACKSIZE 1024
#define LWIP_SO_RCVTIMEO 1
#define LWIP_SO_SNDTIMEO 1
#define LWIP_SO_RCVBUF 1
#define LWIP_TIMEVAL_PRIVATE 0
/* Upstream calls socket(), bind(), inet_pton() and close() by their plain
   names, which is what these two give it. The io-names option only rewrites
   read/write/close inside files that include lwip/sockets.h, and the only such
   file of ours uses stdio for the SD card, so file I/O is unaffected. */
#define LWIP_COMPAT_SOCKETS 1
#define LWIP_POSIX_SOCKETS_IO_NAMES 1
#define MEMP_NUM_NETCONN 12
/* A WireGuard netif doubles the traffic: every tunnelled packet also travels as
   an underlay packet, so the pools want to be larger here than in the default
   build. They are not, because of what has to happen once: the first network
   map this tailnet sends is 32 KB of JSON and its parse tree needs about 90 KB
   contiguous, and these two pools are reserved whether the tunnel is up or
   not. A terminal session over the tunnel is a few hundred packets a second at
   most, which 16 buffers carry; failing to join the tailnet at all is the worse
   outcome. Raise them if throughput ever matters more. */
#define MEM_SIZE (32 * 1024)
#define PBUF_POOL_SIZE 12

#else

#define NO_SYS 1
#define LWIP_SOCKET 0
#define LWIP_NETCONN 0
#define LWIP_RAW 0
#define MEM_SIZE (48 * 1024)
#define PBUF_POOL_SIZE 16

#endif

#define LWIP_IPV4 1
#define LWIP_ARP 1
#define LWIP_ICMP 1
#define MEM_ALIGNMENT 4
#define LWIP_CHKSUM_ALGORITHM 3
#define LWIP_NETIF_STATUS_CALLBACK 1
#define LWIP_NETIF_LINK_CALLBACK 1
#define LWIP_NETIF_HOSTNAME 1
#define LWIP_NETIF_TX_SINGLE_PBUF 1
#define DHCP_DOES_ARP_CHECK 0
#define LWIP_DHCP_DOES_ACD_CHECK 0
#define MEM_STATS 0
#define SYS_STATS 0
#define MEMP_STATS 0
#define LINK_STATS 0
#define LWIP_TCP 1
#define LWIP_UDP 1
#define LWIP_DHCP 1
#define LWIP_DNS 1

#define MEMP_NUM_TCP_PCB 6
#define MEMP_NUM_TCP_PCB_LISTEN 2
#define MEMP_NUM_TCP_SEG 32
#define TCP_MSS 1460
#define TCP_WND (4 * TCP_MSS)
#define TCP_SND_BUF (4 * TCP_MSS)
#define TCP_SND_QUEUELEN ((4 * (TCP_SND_BUF) + (TCP_MSS - 1)) / (TCP_MSS))

#endif
