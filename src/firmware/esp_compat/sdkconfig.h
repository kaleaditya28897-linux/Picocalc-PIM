#ifndef PIM_COMPAT_SDKCONFIG_H
#define PIM_COMPAT_SDKCONFIG_H

/* What ESP-IDF's menuconfig would have generated. Only the options the vendored
   subset actually reads are here; see third_party/microlink/PROVENANCE.md.

   The buffer sizes are the interesting part. Upstream defaults to 512 KB for
   each of the two control-plane buffers, which suits a 300-peer tailnet on a
   board with 8 MB of PSRAM. This board has 520 KB of SRAM in total, so they are
   set for a small tailnet instead. Both sit below the Kconfig floor of 64,
   which is fine because nothing here reads the Kconfig: a netmap for a handful
   of peers is a few kilobytes of JSON. */

/* ---- sizing ---- */
/* Every machine on the tailnet needs a slot, with room to grow. Eight meant the
   table churned - peers evicted to make room for others - and a host that had
   been pushed out simply could not be reached, which looked like SSH failing
   rather than like a full table. WIREGUARD_MAX_PEERS follows this above 16. */
#define CONFIG_ML_MAX_PEERS 20              /* tunnels held at once */
#define CONFIG_ML_NVS_MAX_PEERS 20          /* peers remembered across a reboot */
/* This tailnet's own netmap measured 32,687 bytes, which a 32 KB buffer cut in
   half way through a peer - the parse then failed and the client retried for
   ever. Sized with room for the tailnet to grow rather than to just fit. */
#define CONFIG_ML_H2_BUFFER_SIZE_KB 64      /* HTTP/2 receive buffer for MapResponse */
#define CONFIG_ML_JSON_BUFFER_SIZE_KB 64    /* cJSON's DOM runs 2-3x the raw JSON */
#define CONFIG_ML_FILTER_MAX_ENTRIES 32     /* ACL rules from the control plane */
#define CONFIG_ML_STOP_TIMEOUT_MS 5000

/* ---- identity and control plane ---- */
#define CONFIG_ML_DEVICE_NAME "picocalc"
#define CONFIG_ML_CTRL_HOST "controlplane.tailscale.com"
/* The ts2021 handshake authenticates the control plane with Noise, so TLS is
   not what keeps it honest and is left off: it is the configuration upstream
   tests, and it keeps mbedTLS out of this path entirely. The DERP relay still
   needs TLS. */
/* #undef CONFIG_ML_CTRL_TLS */
/* #undef CONFIG_ML_CTRL_TLS_VERIFY_CERT */
#define CONFIG_ML_CTRL_NOISE_PUBKEY_HEX ""  /* set only for a self-hosted control server */
/* Headscale does not answer Stream=false, so turn this on when bringing up
   against Headscale. Upstream also switches by itself if the reply is empty. */
/* #undef CONFIG_ML_CTRL_STREAMING_MAP_FETCH */

/* ---- DERP ---- */
/* Upstream takes the home relay region from the build (region 9, Dallas, which
   is wrong for most people). Here it comes off the card instead - the derp=
   line in /PIM/tailscale.cfg - so moving continents does not need a rebuild.
   `tailscale netcheck` on a machine on the same network names the nearest one.
   This is a plain function call, which is all upstream needs it to be. */
#ifndef __ASSEMBLER__
unsigned pim_ts_derp_region(void);
#define CONFIG_ML_DERP_REGION pim_ts_derp_region()
#endif
/* No CA bundle is shipped, so certificates cannot be checked. Relayed traffic
   stays WireGuard-encrypted end to end; what this gives up is noticing
   something in the middle posing as a relay to drop or delay packets. */
/* #undef CONFIG_ML_DERP_VERIFY_CERT */

/* ---- packet filter ---- */
#define CONFIG_ML_PACKET_FILTER_ENFORCE 1   /* honour the tailnet's ACLs */
/* #undef CONFIG_ML_PACKET_FILTER_LOG */

/* ---- features this build leaves out ---- */
/* #undef CONFIG_ML_ENABLE_CELLULAR */
/* #undef CONFIG_ML_SUBNET_ROUTER */
/* #undef CONFIG_ML_ROUTER_IPV6 */
/* #undef CONFIG_ML_OTA_ROLLBACK */
/* #undef CONFIG_ML_ZERO_COPY_WG */        /* raw-PCB receive; worth trying once it runs */
#define CONFIG_ML_ADVERTISE_ROUTES ""
#define CONFIG_ML_PRIORITY_PEER_IP ""
#define CONFIG_ML_UDP_RX_TASK_PRIO 5

/* ---- what the platform looks like to upstream ---- */
#define CONFIG_FREERTOS_UNICORE 1           /* one core, as FreeRTOSConfig.h sets up */
#define CONFIG_LOG_MAXIMUM_LEVEL 3          /* up to info; ESP_LOGD and below compile out */
#define CONFIG_LWIP_TCPIP_CORE_LOCKING 1    /* matches lwipopts.h */
/* #undef CONFIG_LWIP_CHECK_THREAD_SAFETY */
/* #undef CONFIG_MBEDTLS_CERTIFICATE_BUNDLE */
/* #undef CONFIG_MBEDTLS_ERROR_STRINGS */
/* #undef CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC */
#define CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC 1
/* #undef CONFIG_NEWLIB_NANO_FORMAT */
/* #undef CONFIG_ML_LOG_LEVEL_VERBOSE */

#endif
