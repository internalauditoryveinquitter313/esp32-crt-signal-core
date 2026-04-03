/* Host-test stub for esp_check.h — minimal ESP-IDF macros */
#pragma once

#include "esp_err.h"

#define ESP_RETURN_ON_FALSE(cond, err, tag, msg, ...) \
    do { if (!(cond)) return (err); } while (0)

#define ESP_RETURN_ON_ERROR(expr, tag, msg, ...) \
    do { esp_err_t _e = (expr); if (_e != ESP_OK) return _e; } while (0)

#define ESP_GOTO_ON_FALSE(cond, err, label, tag, msg, ...) \
    do { if (!(cond)) { ret = (err); goto label; } } while (0)

#define ESP_GOTO_ON_ERROR(expr, label, tag, msg, ...) \
    do { ret = (expr); if (ret != ESP_OK) goto label; } while (0)
