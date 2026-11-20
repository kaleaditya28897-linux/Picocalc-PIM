#ifndef PIM_MBEDTLS_CONFIG_H
#define PIM_MBEDTLS_CONFIG_H

/* mbedTLS for one job only: a TLS 1.2 client connection to a Tailscale DERP
   relay. Everything not needed for that is left out, because this board has
   520 KB of SRAM in total and the Tailscale client is already most of what is
   left of it.

   Only used by the PIM_ENABLE_TAILSCALE build. */

/* Some mbedTLS sources use INT_MAX without including limits.h. */
#include <limits.h>

/* ---- platform ---- */
#define MBEDTLS_NO_PLATFORM_ENTROPY         /* no /dev/urandom here */
#define MBEDTLS_ENTROPY_HARDWARE_ALT        /* mbedtls_hardware_poll in ts_platform.c */
#define MBEDTLS_PLATFORM_C
/* mbedTLS's own net_sockets.c only builds on Unix and Windows. It is not needed:
   the vendored code includes mbedtls/net_sockets.h but drives the TLS session
   through its own lwIP sockets, so nothing calls mbedtls_net_*. */
/* #undef MBEDTLS_NET_C */
#define MBEDTLS_HAVE_ASM
/* No clock to check validity against, and certificate checking is off anyway
   (see sdkconfig.h), so the date fields are parsed and not compared. */
#define MBEDTLS_X509_ALLOW_UNSUPPORTED_CRITICAL_EXTENSION

/* ---- TLS 1.2 client ---- */
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_SSL_SERVER_NAME_INDICATION
#define MBEDTLS_SSL_ENCRYPT_THEN_MAC
#define MBEDTLS_SSL_EXTENDED_MASTER_SECRET
/* A record can be 16 KB, which is more than can be spared. Asking for a smaller
   maximum fragment is the supported way to say so; a relay that ignores it and
   sends a full-size record will fail, which is one of the first things to check
   during bring-up. */
#define MBEDTLS_SSL_MAX_FRAGMENT_LENGTH
#define MBEDTLS_SSL_IN_CONTENT_LEN 8192
#define MBEDTLS_SSL_OUT_CONTENT_LEN 2048

/* ---- key exchange: what Let's Encrypt relays actually offer ---- */
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECDSA_C
#define MBEDTLS_ECP_C
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED
#define MBEDTLS_ECP_DP_CURVE25519_ENABLED
#define MBEDTLS_ECP_NIST_OPTIM
#define MBEDTLS_RSA_C
#define MBEDTLS_PKCS1_V15
#define MBEDTLS_PKCS1_V21
#define MBEDTLS_BIGNUM_C

/* ---- ciphers and hashes ---- */
#define MBEDTLS_AES_C
#define MBEDTLS_GCM_C
#define MBEDTLS_CHACHA20_C
#define MBEDTLS_CHACHAPOLY_C
#define MBEDTLS_POLY1305_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_MD_C
#define MBEDTLS_SHA224_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA256_SMALLER              /* smaller code, slightly slower */
#define MBEDTLS_SHA384_C
#define MBEDTLS_SHA512_C
#define MBEDTLS_CTR_DRBG_C
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_AES_ROM_TABLES              /* tables in flash, not RAM */

/* ---- certificates ---- */
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_OID_C
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_X509_USE_C
#define MBEDTLS_X509_CRT_PARSE_C
#define MBEDTLS_BASE64_C                    /* ml_derp.c uses it directly */

/* ---- left out on purpose ---- */
/* No server, no DTLS, no session tickets, no renegotiation, no filesystem,
   no CRL or CSR handling, no PSA layer: none of it is on the DERP path. */

#endif
