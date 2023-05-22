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

// Rotate the logfile if it reaches this many records. We want to keep
// each file small because they are copied to memory for transmission
// to the MQTT broker.
#define MAX_RECORDS 50

// If we don't send anything to the MQTT broker for this long, rotate
// the logfile even if it did not reach MAX_RECORDS. This will indirectly
// cause us to transmit something, indicating we are alive.
#define MAX_IDLE_TIME 3600000 // 1 hour

// Check the disk periodically for logfiles to upload.
#define LOG_SEARCH_INTERVAL 15000 // 15 seconds

namespace LOGNS {
    class Logger {
        public:
            inline void init();
            void flushSentLogfile();
            void processLogs();
            void logEvent(const char* message);
            void logAccess(const char* readerID, unsigned long cardID,
                            bool authorized);
        private:
            File logfile;
            char logfilename[100]; // Keeps track of the current log file we're using
            int numberOfRecords;

            char inTransitFilename[100]; // Keeps track of the current log file we're sending

            void createNewLogfile();
            void sendNextLogfile();

            unsigned long lastLogCheck = 0;
            bool sendingLogfile = false;
            unsigned long lastLogfileSentTime = 0;

            bool loggingWithoutTime;
            inline void initBootCount();
            int bootcount = 0;
            bool justStarted = true;
    };

    inline void Logger::init() {
        if (timeIsValid()) {
            loggingWithoutTime = false;
        } else {
            loggingWithoutTime = true;
            initBootCount();
        }

        createNewLogfile();
    }

    inline void Logger::createNewLogfile() {
        if (!sdPresent) return;

        if (logfile) {
            log_d("Closing logfile: %s", logfilename);
            logfile.flush();
            logfile.close();
        }


        if (timeIsValid()) { loggingWithoutTime = false; }

        if (loggingWithoutTime) {
            unsigned long timestamp = millis();
            snprintf(logfilename, 100, "/bootlog_%lu_%lu", bootcount, timestamp);
        } else {
            unsigned long timestamp = getTime();
            snprintf(logfilename, 100, "/log_%lu", timestamp);
        }

        logfile = SD.open(logfilename, FILE_WRITE);
        numberOfRecords = 0;

        if (loggingWithoutTime && justStarted) {
            char buffer[100];
            snprintf(buffer, 100, "boot %d OK; this message was logged when millis = %lu\n", bootcount, millis());
            logfile.print(buffer);
            logfile.flush();
            justStarted = false;
        }

        log_d("Created new logfile: %s", logfilename);
    }

    inline void Logger::initBootCount() {
        File boot;
        if (SD.exists("/bootcount.txt")) {
            boot = SD.open("/bootcount.txt", FILE_READ);
            bootcount = boot.parseInt();
            boot.close();
        }

        ++bootcount;

        boot = SD.open("/bootcount.txt", FILE_WRITE);
        boot.print(bootcount);
        boot.close();
    }

    void Logger::logAccess(const char* readerID, unsigned long cardID,
                            bool authorized) {
        if (!sdPresent) return;

        char buffer[100];
        if (loggingWithoutTime) {
            snprintf(buffer, 100, "%lu (ACCESS/BOOT#%d): %d %s %d %lu",
                    millis(), bootcount, doorID, readerID, authorized, cardID);
        } else {
            snprintf(buffer, 100, "%lu (ACCESS): %d %s %d %lu",
                    getTime(), doorID, readerID, authorized, cardID);
        }

        logfile.print(buffer);
        logfile.flush();
        numberOfRecords++;
    }

    void Logger::logEvent(const char* message) {
        if (!sdPresent) return;

        char buffer[192];
        if (loggingWithoutTime) {
            snprintf(buffer, 192, "%lu %d (BOOT#%d): %s", millis(), doorID, bootcount, message);
        } else {
            snprintf(buffer, 192, "%lu %d (SYSTEM): %s", getTime(), doorID, message);
        }

        logfile.print(buffer);
        logfile.flush();
        numberOfRecords++;
    }

    void Logger::processLogs() {
        if (!sdPresent) return;

        // We create a new logfile if:
        //
        // 1. There are more than MAX_RECORDS in the current file
        //    (it has to fit in the device memory)
        //
        // OR
        //
        // 2. The file is non-empty and nothing has been sent for
        //    too long (this will force the file to be sent, which
        //    serves as a notification that we are alive)
        if (numberOfRecords > MAX_RECORDS
                or currentMillis - lastLogfileSentTime > MAX_IDLE_TIME) {

            if (numberOfRecords > 0) {
                createNewLogfile();
                lastLogfileSentTime = currentMillis; // that's a lie, but ok
            } else { // Make numberOfRecords > 0 on the next iteration
                log_w("We're alive!");
            }
        }

        // If we are already sending a file or are offline,
        // we should wait before sending anything else
        if (sendingLogfile || !isClientConnected()) { return; }

        if (currentMillis - lastLogCheck < LOG_SEARCH_INTERVAL) { return; }

        lastLogCheck = currentMillis;

        sendNextLogfile();
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
        logger.logEvent(buf);

        return count;
    }
}

void initLog() {
    LOGNS::logger.init();

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

