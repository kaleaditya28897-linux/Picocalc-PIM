#ifndef PIM_COMPAT_ESP_LOG_H
#define PIM_COMPAT_ESP_LOG_H

#include <stdio.h>

#include "sdkconfig.h"

/* The log goes to the UART, which is where this firmware's stdio already points.
   Levels are compared at run time against a single global, so a noisy module can
   be quietened without rebuilding. */

typedef enum {
    ESP_LOG_NONE, ESP_LOG_ERROR, ESP_LOG_WARN, ESP_LOG_INFO, ESP_LOG_DEBUG, ESP_LOG_VERBOSE
} esp_log_level_t;

/* ESP-IDF lets a file raise or lower its own level by defining this before the
   include, and supplies the default otherwise. Upstream reads it directly. */
#ifndef LOG_LOCAL_LEVEL
#define LOG_LOCAL_LEVEL CONFIG_LOG_MAXIMUM_LEVEL
#endif

void esp_log_level_set(const char *tag, esp_log_level_t level);
esp_log_level_t esp_log_level_get(const char *tag);
void pim_esp_log(esp_log_level_t level, const char *tag, const char *format, ...)
    __attribute__((format(printf, 3, 4)));

#define ESP_LOGE(tag, format, ...) pim_esp_log(ESP_LOG_ERROR, tag, format, ##__VA_ARGS__)
#define ESP_LOGW(tag, format, ...) pim_esp_log(ESP_LOG_WARN, tag, format, ##__VA_ARGS__)
#define ESP_LOGI(tag, format, ...) pim_esp_log(ESP_LOG_INFO, tag, format, ##__VA_ARGS__)
#define ESP_LOGD(tag, format, ...) pim_esp_log(ESP_LOG_DEBUG, tag, format, ##__VA_ARGS__)
#define ESP_LOGV(tag, format, ...) pim_esp_log(ESP_LOG_VERBOSE, tag, format, ##__VA_ARGS__)

#endif
