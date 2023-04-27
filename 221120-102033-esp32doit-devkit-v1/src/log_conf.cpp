static const char *TAG = "log";

#include <common.h>
#include <Arduino.h>
#include <sqlite3.h>
#include <SD.h>
#include "esp_tls.h"
#include <timemanager.h>
#include <networkmanager.h>
#include <dbmanager.h>

#define BACKUP_INTERVAL 120000

#define RETRY_TIME 60000 // TODO: if we switch to alarms instead of
                         //       time intervals, how to handle this?

#define DEBUG_TERMINAL

//#define maxLogLifetime 14400000 // 4h in ms
// #define maxLogLifetime 120000
#define MAXLOGLIFETIME 180000

#define MAXRECORDS 5

// Value of keepalive. If we don't send at least one log for one hour,
// we will send a message saying we are alive!
#define KEEPALIVE 3600000

#define LOG_SEARCH_INTERVAL 15000

namespace LOGNS {
    class Logger {
        public:
            // TODO: change functions and variables name
            inline void init();
            void logEvent(const char* readerID, unsigned long cardID,
                            bool authorized);
            void flushSentLogfile();
            void processLogs();
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
    };

    inline void Logger::init() {
        createNewLogfile();
    }

    inline void Logger::createNewLogfile() {
        log_d("Creating new log file...");

        // FIXME: when this remains open we need to close it
        //if (logfile) {
        //    logfile.close();
        //}

        unsigned long date = getTime();
        logfileCreationTime = millis();
        sprintf(logfilename, "/log_%lu", date);

        log_d("Name of new log: %s\n", logfilename);

        logfile = SD.open(logfilename, "w", 1);
        logfile.close(); // FIXME it should remain open
        numberOfRecords = 0;
    }

    void Logger::logEvent(const char* readerID, unsigned long cardID,
                            bool authorized) {
        // If i let the file open the write method doesn't work...
        // I dont know why, but i will test when we finish this stuff
        logfile = SD.open(logfilename, "aw");

        char buffer[100];
        sprintf(buffer, "%lu: %d %s %d %lu\n",
                getTime(), doorID, readerID, authorized, cardID);

        log_d("Writing to log file: %s", buffer);
        logfile.print(buffer);
        logfile.close(); // FIXME

        numberOfRecords++;
    }

    void Logger::processLogs() {
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
        log_d("Searching for logs in SD to send...");
        File root = SD.open("/");
        File entry;

        // TODO: check if this works as expected :)
        while (entry = root.openNextFile()) {
            if (entry.isDirectory()) { continue; }

            if (strncmp("log_", entry.name(), strlen("log_")) != 0) {
                continue;
            }

            // "+1" means "skip the initial slash character"
            if (strcmp(logfilename+1, entry.name()) != 0) {
                log_d("Found a logfile to send: %s", entry.name());
                sprintf(inTransitFilename,"/%s", entry.name());
                log_d("Sending logfile %s.", entry.name());
                sendingLogfile = true;
                lastLogfileSentTime = currentMillis;
                sendLog(inTransitFilename);
                break; // Do not send anything else
            }
        }

        log_d("Finished search for logfiles...\n");
        root.close();
        entry.close();
    }

    void Logger::flushSentLogfile() {
        log_d("Finished sending logfile...");
        sendingLogfile = false;
        log_d("Removing file: %s", inTransitFilename);
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

void logEvent(const char* readerID, unsigned long cardID,
                    bool authorized) {
    LOGNS::logger.logEvent(readerID, cardID, authorized);
}

void flushSentLogfile() {
    LOGNS::logger.flushSentLogfile();
}

void processLogs() {
    LOGNS::logger.processLogs();
}
