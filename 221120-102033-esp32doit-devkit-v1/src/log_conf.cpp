static const char *TAG = "log";

#include <common.h>
#include <Arduino.h>
#include <sqlite3.h>
#include <SD.h>
#include "esp_tls.h"
#include <timemanager.h>
#include <networkmanager.h>
#include <dbmanager.h>
#include <mqttmanager.h>

#define BACKUP_INTERVAL 120000

#define RETRY_TIME 60000 // TODO: if we switch to alarms instead of
                         //       time intervals, how to handle this?

#define DEBUG_TERMINAL

//#define maxLogLifetime 14400000 // 4h in ms
// #define maxLogLifetime 120000
#define MAXLOGLIFETIME 180000

#define MAXRECORDS 50

// Value of keepalive. If we don't send at least one log for one hour,
// we will send a message saying we are alive!
#define KEEPALIVE 3600000

#define LOG_SEARCH_INTERVAL 15000

namespace LOGNS {
    class Logger {
        public:
            inline void init();
            void flushSentLogfile();
            void processLogs();
            void logEvent(const char* message);
            void logAccess(const char* readerID, unsigned long cardID,
                            bool authorized);
            void initOfflineLogger();
            int increaseBootCounter();
        private:
            File logfile;
            char logfilename[100]; // Keeps track of the current log file we're using
            unsigned long logfileCreationTime;
            int numberOfRecords;

            char inTransitFilename[100]; // Keeps track of the current log file we're sending

            void createNewLogfile();
            void sendNextLogfile();

            unsigned long lastLogCheck = 0;
            bool sendingLogfile = false;
            unsigned long lastLogfileSentTime = 0;

            bool usingOfflineLog = false;
            int bootcount;
    };

    inline void Logger::init() {
        createNewLogfile();
    }

    inline void Logger::createNewLogfile() {
        if ((!sdPresent || !getTime())) return;

        if (logfile) {
            log_d("Closing logfile: %s", logfilename);
            logfile.flush();
            logfile.close();
            usingOfflineLog = false;
        }

        unsigned long date = getTime();
        logfileCreationTime = millis();
        snprintf(logfilename, 100, "/log_%lu", date);

        log_d("Creating new logfile: %s", logfilename);

        logfile = SD.open(logfilename, FILE_WRITE);

        if (usingOfflineLog) {
            char buffer[100];
            snprintf(buffer, 100, "boot %d OK; this message was logged when millis = %lu\n", bootcount, millis());
            logfile.print(buffer);
            logfile.flush();
        }

        numberOfRecords = 0;
    }

    inline void Logger::initOfflineLogger() {
        if (!sdPresent) return;

        usingOfflineLog = true;
        logfileCreationTime = millis();

        bootcount = increaseBootCounter();

        snprintf(logfilename, 100, "/bootlog_%lu", bootcount);

        log_d("Creating new logfile: %s", logfilename);

        logfile = SD.open(logfilename, FILE_WRITE, 1);
        numberOfRecords = 0;
    }

    inline int Logger::increaseBootCounter() {
        int r;
        File boot;
        if (SD.exists("/bootcount.txt")) {
            boot = SD.open("/bootcount.txt", FILE_READ);
            r = boot.parseInt();
            boot.close();
        } else {
            r = 0;
        }

        boot = SD.open("/bootcount.txt", FILE_WRITE);
        boot.print(r+1);
        boot.close();

        return r;
    }

    void Logger::logAccess(const char* readerID, unsigned long cardID,
                            bool authorized) {
        if (!sdPresent) return;

        char buffer[100];
        if (usingOfflineLog) {
            snprintf(buffer, 100, "(BOOT#%d): %d %s %d %lu",
                    bootcount, doorID, readerID, authorized, cardID);
        } else {
            snprintf(buffer, 100, "%lu (ACCESS): %d %s %d %lu",
                    getTime(), doorID, readerID, authorized, cardID);
        }

        logfile.print(buffer);
        logfile.flush();

        if (!usingOfflineLog)
            numberOfRecords++;
    }

    void Logger::logEvent(const char* message) {
        if (!sdPresent) return;

        char buffer[192];
        if (usingOfflineLog) {
            snprintf(buffer, 192, "(BOOT#%d): %s", bootcount, message);
        } else {
            snprintf(buffer, 192, "%lu (SYSTEM): %s", getTime(), message);
        }

        logfile.print(buffer);
        logfile.flush();
        if (!usingOfflineLog)
            numberOfRecords++;
    }

    void Logger::processLogs() {
        if (!sdPresent || usingOfflineLog) return;

        // We create a new logfile if:
        //
        // 1. There are more than MAXRECORDS in the current file
        //    (it has to fit in the device memory)
        //
        // OR
        //
        // 2. The file is non-empty and created too long ago
        //    (this will force the file to be sent, which
        //    serves as a notification that we are alive)
        if (
                numberOfRecords > MAXRECORDS
                or
                (
                    currentMillis - logfileCreationTime > MAXLOGLIFETIME
                    and
                    numberOfRecords > 0
                )
           ) { createNewLogfile(); }

        // If we are already sending a file or are offline,
        // we should wait before sending anything else
        if (sendingLogfile || !isClientConnected()) { return; }

        if (currentMillis - lastLogCheck < LOG_SEARCH_INTERVAL) { return; }

        lastLogCheck = currentMillis;

        sendNextLogfile();

        // If we haven't sent anything for a long time, send
        // a message to the broker saying we are alive!!
        if (currentMillis - lastLogfileSentTime > KEEPALIVE) {
            log_d("Sending keepalive message to MQTT broker");
            // sendAliveMessage(); TODO: Easy
        }
    }

    void Logger::sendNextLogfile() {
        return;
        log_d("Searching for logs in SD to send...");
        File root = SD.open("/");
        File entry;

        // TODO: check if this works as expected :)
        while (entry = root.openNextFile()) {
            if (entry.isDirectory()) { continue; }

            if (strncmp("log_", entry.name(), strlen("log_")) != 0 &&
            strncmp("bootlog_", entry.name(), strlen("bootlog_")) != 0 ) {
                continue;
            }

            // "+1" means "skip the initial slash character"
            if (strcmp(logfilename+1, entry.name()) != 0) {
                log_d("Found a logfile to send: %s", entry.name());
                snprintf(inTransitFilename, 100, "/%s", entry.name());
                log_d("Sending logfile %s.", entry.name());
                sendingLogfile = true;
                lastLogfileSentTime = currentMillis;
                sendLog(inTransitFilename);
                break; // Do not send anything else
            }
        }

        log_d("Finished search for logfiles.");
        root.close();
        entry.close();
    }

    void Logger::flushSentLogfile() {
        log_d("Finished sending logfile %s.", inTransitFilename);
        sendingLogfile = false;
        log_d("Removing sent logfile: %s", inTransitFilename);
        SD.remove(inTransitFilename);
        inTransitFilename[0] = 0;
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
        char buf[192];
        buf[0] = 0;
        count = vsnprintf(buf, 192, format, ap);

        Serial.print(buf);
        if (sdPresent)
            logger.logEvent(buf);

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

void logAccess(const char* readerID, unsigned long cardID,
                    bool authorized) {
    LOGNS::logger.logAccess(readerID, cardID, authorized);
}

void flushSentLogfile() {
    LOGNS::logger.flushSentLogfile();
}

void processLogs() {
    LOGNS::logger.processLogs();
}

void initOfflineLogger() {
    LOGNS::logger.initOfflineLogger();
}