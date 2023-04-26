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
            void generateLog(const char* readerID, unsigned long cardID,
                            bool authorized);
            void finishLog();

            void checkLogs();
        private:
            char buffer[100];

            unsigned long logCreationTime;
            int numberOfRecords;
            char logWeAreSending[100]; // Keeps track of the current log we're sending
            char latestLog[100]; // Keeps track of the current log file we're using
            File logFile;

            void send(const char* file);
            void startNewLog();
            void searchForLogs();
            void eraseLogs();

            unsigned long lastLogCheck = 0;
            bool sendingLog = false;
            // When ESP initiates, there might be some logs in the system that
            // were not sent to the server. So, this variable is used to search
            // and send logs while they are in the system.
            bool isThereLogs = true;
            unsigned long lastMessage = 0;
    };

    inline void Logger::init() {
        startNewLog();
    }

    inline void Logger::startNewLog() {
        log_d("Creating new log file...");

        unsigned long data = getTime();
        logCreationTime = millis();
        sprintf(latestLog, "/log_%lu", data);

        log_d("Name of new log: %s\n", latestLog);

        logFile = SD.open(latestLog, "w", 1);
        numberOfRecords = 0;
    }

    void Logger::generateLog(const char* readerID, unsigned long cardID,
                            bool authorized) {
        // If i let the file open the write method doesn't work...
        // I dont know why, but i will test when we finish this stuff
        logFile = SD.open(latestLog, "aw");

        sprintf(buffer, "%d %s %d %lu %lu\n", doorID, readerID, authorized, cardID, getTime());
        logFile.print(buffer);
        logFile.close();

        numberOfRecords++;

        log_d("Writing in log file: %s ", buffer);
    }

    void Logger::checkLogs() {
        // If we are sending a log already, there is no reason to start a new log
        // or send keepAliveMessage.
        if (sendingLog || !isClientConnected()) return;

        if (isThereLogs && currentMillis - lastLogCheck > LOG_SEARCH_INTERVAL) {
            lastLogCheck = currentMillis;
            searchForLogs();
        }
        // If we recorded more than MAXRECORDS or the log was created longer than
        // we expected, we send the log and start a new one
        if (numberOfRecords > MAXRECORDS || currentMillis - logCreationTime > MAXLOGLIFETIME) {
            memcpy((void*) logWeAreSending, (void*) latestLog, 100);
            send(logWeAreSending);
            startNewLog();
        }
        // If we don't send nothing during a long time, we send a message to the
        // client saying we are alive!!
        if (currentMillis - lastMessage > KEEPALIVE) {
            log_d("Sending keepalive message to MQTT broker");
            // sendAliveMessage(); TODO: Easy
        }
    }
    void Logger::searchForLogs() {
        log_d("Searching for logs in SD to send...");
        bool isLog;
        char logPrefix[10] = "log_";
        File root = SD.open("/");
        File entry = root.openNextFile();

        isThereLogs = false;
        while (entry) {
            if (!entry.isDirectory()) {
                isLog = true;

                // Verify if file is a log
                for (int i = 0; i < 3 && isLog; i++) {
                    if (logPrefix[i] != entry.name()[i]) {
                        // log_d("%c x %c", logPrefix[i], entry.name()[i]);
                        isLog = false;
                        break;
                    }
                }
                if (isLog && strcmp(latestLog+1, entry.name())) {
                    log_d("Found a log: %s", entry.name());
                    if (sendingLog) {
                        isThereLogs = true;
                    } else {
                        sprintf(logWeAreSending,"/%s", entry.name());
                        send(logWeAreSending);
                    }
                }
            }
            entry = root.openNextFile();
        }
        log_d("Finished search for logs...\n");
        root.close();
        entry.close();
    }

    void Logger::send(const char* file) {
        log_d("Sending log.");
        sendLog(file);
        sendingLog = true;
    }

    void Logger::finishLog() {
        log_d("Finished sending log...");
        sendingLog = false;
        log_d("Removing file: %s", logWeAreSending);
        SD.remove(logWeAreSending);
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

void finishSendingLog() {
    LOGNS::logger.finishLog();
}

void checkLogs() {
    LOGNS::logger.checkLogs();
}