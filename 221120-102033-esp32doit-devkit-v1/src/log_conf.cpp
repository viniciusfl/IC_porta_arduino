static const char *TAG = "log";

#include <common.h>
#include <Arduino.h>
#include <sqlite3.h>
#include <SD.h>
#include "esp_tls.h"
#include <timemanager.h>
#include <networkmanager.h>

#define BACKUP_INTERVAL 120000

#define RETRY_TIME 60000 // TODO: if we switch to alarms instead of
                         //       time intervals, how to handle this?

#define DEBUG_TERMINAL

namespace LOGNS {
    class Logger {
        public:
            inline void init();
            void generateLog(const char* readerID, unsigned long cardID,
                            bool authorized);

            File systemlogFile;
        private:
            char buffer[100];
            File logFile;
    };

    inline void Logger::init() {
        unsigned long now = getTime();
        sprintf(buffer, "/log_%lu", now);
        logFile = SD.open(buffer, FILE_WRITE);

        sprintf(buffer, "/syslog_%lu", now);
        systemlogFile = SD.open(buffer, FILE_WRITE);
    }

    void Logger::generateLog(const char* readerID, unsigned long cardID,
                            bool authorized) {
        sprintf(buffer, "%d %s %d %lu %lu\n", doorID, readerID, authorized, cardID, getTime());
        logFile.print(buffer);
        log_d("Writing in log file: %s ", buffer);
    }

    Logger logger;

    int logmessage(const char* format, va_list ap) {
        // "format" is "LEVEL (%u) %s: user-defined part"
        // Where "LEVEL" is a letter (V/D/I/W/E)
        // %u is the timestamp
        // %s is the tag
        //
        // So, to obtain the level we need to extract the first letter
        // of the format; the timestamp is the first parameter in the
        // va_list and the tag is the second parameter in the va_list.
        //
        // By default, the timestamp is equivalent to millis(); defining
        // CONFIG_LOG_TIMESTAMP_SOURCE_SYSTEM changes that to HH:MM:SS.sss
        // (in that case, "%u" becomes "%s"). We may also get the system
        // time here ourselves and ignore this.
        int count;
        char buf[512];
        buf[0] = 0;
        count = vsnprintf(buf, 512, format, ap);

#       ifdef DEBUG_TERMINAL
        Serial.print(buf);
#       else
        logger.systemlogFile.print(buf);
#       endif

        return count;
    }
}

void initLog() {
    LOGNS::logger.init();
}

void initLogSystem() {
    // Do not change this! Instead, define the desired level in log_conf.h
    esp_log_level_set("*", ESP_LOG_VERBOSE);

    // Log messages will be processed by the function defined above
    esp_log_set_vprintf(LOGNS::logmessage);
}

void generateLog(const char* readerID, unsigned long cardID,
                    bool authorized) {
    LOGNS::logger.generateLog(readerID, cardID, authorized);
}
