#ifndef PIM_COMPAT_ESP_ERR_H
#define PIM_COMPAT_ESP_ERR_H

/* ESP-IDF's esp_err.h pulls these in, and upstream headers rely on that having
   happened, so this one does too. */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int esp_err_t;

#define ESP_OK                  0
#define ESP_FAIL                (-1)
#define ESP_ERR_NO_MEM          0x101
#define ESP_ERR_INVALID_ARG     0x102
#define ESP_ERR_INVALID_STATE   0x103
#define ESP_ERR_INVALID_SIZE    0x104
#define ESP_ERR_NOT_FOUND       0x105
#define ESP_ERR_NOT_SUPPORTED   0x106
#define ESP_ERR_TIMEOUT         0x107
#define ESP_ERR_INVALID_MAC     0x109

const char *esp_err_to_name(esp_err_t code);

/* ESP-IDF makes these available almost everywhere, and upstream uses them for
   its event-group bits. */
#ifndef BIT0
#define BIT(n) (1UL << (n))
#define BIT0  BIT(0)
#define BIT1  BIT(1)
#define BIT2  BIT(2)
#define BIT3  BIT(3)
#define BIT4  BIT(4)
#define BIT5  BIT(5)
#define BIT6  BIT(6)
#define BIT7  BIT(7)
#define BIT8  BIT(8)
#define BIT9  BIT(9)
#define BIT10 BIT(10)
#define BIT11 BIT(11)
#define BIT12 BIT(12)
#define BIT13 BIT(13)
#define BIT14 BIT(14)
#define BIT15 BIT(15)
#define BIT16 BIT(16)
#define BIT17 BIT(17)
#define BIT18 BIT(18)
#define BIT19 BIT(19)
#define BIT20 BIT(20)
#define BIT21 BIT(21)
#define BIT22 BIT(22)
#define BIT23 BIT(23)
#endif

#endif
