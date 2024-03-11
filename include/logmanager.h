#ifndef LOG_CONF_H
#define LOG_CONF_H

/*****

Some info on logging for ESP: https://thingpulse.com/esp32-logging/

There are two logging facilities available to us:

 1. The native ESP-IDF version:
    https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/log.html

    It uses calls like ESP_LOGE(TAG, message), ESP_LOGW(TAG, message)

 2. The arduino framework for ESP version:
    https://github.com/espressif/arduino-esp32/blob/master/cores/esp32/esp32-hal-log.h

    It uses calls like log_e(message), log_w(message)


THE ESP-IDF LOGGING FACILITY

If I understand correctly, the ESP-IDF logging facility filtering works
on two fronts:

 1. CONFIG_LOG_MAXIMUM_LEVEL affects the logging macros, so that log
    messages with higher verbosity level than this do not even generate
    any code. This can be overriden per file with LOG_LOCAL_LEVEL.

 2. CONFIG_LOG_DEFAULT_LEVEL is, as the name implies, a default: as long
    as the code for a given level is not excluded by the maximum above,
    messages of that level may or may not be shown according to this
    setting. This can be overriden at runtime with esp_log_level_set().

There may be some unnecessary runtime overhead and memory consumption if
DEFAULT verbosity < MAXIMUM verbosity (some messages are excluded, but
the code to process them is not). The advantage is that it may be useful
to change logging levels at runtime.

CONFIG_LOG_MAXIMUM_LEVEL and CONFIG_LOG_DEFAULT_LEVEL cannot be modified
here, they are compilation options. As mentioned above, MAXIMUM can be
overriden per file with LOG_LOCAL_LEVEL and DEFAULT can be overriden at
runtime with esp_log_level_set().


THE ARDUINO FRAMEWORK FOR ESP LOGGING FACILITY

The arduino logging system can use colors, includes the calling function
name, and prevents the unnecessary runtime overhead mentioned above, but
it does not provide a simple way to redefine the logging target (where
log messages are sent to), which is very important to us. By default, it
overrides the ESP-IDF macros -- ESP_LOGE, ESP_LOGW etc. -- with its own
versions, unless CONFIG_ARDUHAL_ESP_LOG is not defined (by default it is
and it cannot be undefined here, it is a compilation option).

*****/

// With this #define, the arduino logging facility is modified to just
// wrap around the ESP-IDF version, which also preserves the original
// ESP-IDF macros.
#define USE_ESP_IDF_LOG // must be defined before loading Arduino.h !

/*
CORE_DEBUG_LEVEL defines the maximum allowed verbosity level. With
USE_ESP_IDF_LOG, this simply sets LOG_LOCAL_LEVEL (used by the ESP-IDF
logging facility). This means we also need to call esp_log_level_set()
to increase verbosity beyond the default (ERROR).

Valid values, ordered by verbosity:

ARDUHAL_LOG_LEVEL_NONE    (0)
ARDUHAL_LOG_LEVEL_ERROR   (1)
ARDUHAL_LOG_LEVEL_WARN    (2)
ARDUHAL_LOG_LEVEL_INFO    (3)
ARDUHAL_LOG_LEVEL_DEBUG   (4)
ARDUHAL_LOG_LEVEL_VERBOSE (5)
*/

// must be defined before loading Arduino.h !
//#define CORE_DEBUG_LEVEL ARDUHAL_LOG_LEVEL_VERBOSE
#define CORE_DEBUG_LEVEL ARDUHAL_LOG_LEVEL_DEBUG

void initLog();

void initDiskLog();

void logAccess(const char* readerID, unsigned long cardID,
                  bool authorized);

void notifyMessageSent();

void uploadLogs();

void cancelLogUpload();

#endif
