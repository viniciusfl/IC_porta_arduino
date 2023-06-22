static const char *TAG = "log";

#include <tramela.h>

#include <Arduino.h>
#include <SD.h>

#include <timemanager.h> // getTime()
#include <mqttmanager.h> // sendLog and isClientConnected

// This does three things:
//
//  1. Receives log messages and writes them to a file on the disk
//  2. When the file is "big", closes it and opens a new one
//  3. Sends one file at a time to the controlling server and, if
//     upload is successful, deletes the sent file
//
// Log messages are both system logs and access logs, and they are
// all mixed together; it is up to the server to separate them.
//
// We may need to log stuff before the system time is correct.
// When this happens, we (1) use millis() as the time and (2) add
// "BOOT#XX" (where "XX" is the boot count) to the log messages so
// that the server can order them.


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

// We divide the log files in a few subdirectories to avoid filesystem
// performance issues.
#define NUM_SUBDIRS 10

#define MAX_LOG_FILE_SIZE 5000 // 5kb

namespace LOGNS {
    class Logger {
        public:
            inline void init();
            void flushSentLogfile(); // erase logfiles that have been sent ok
            void processLogs();
            void logEvent(const char* message);
            void logAccess(const char* readerID, unsigned long cardID,
                            bool authorized);
        private:
            File logfile;
            char logfilename[100]; // current file we're logging to
            int numberOfRecords;

            char inTransitFilename[100]; // current file we're sending

            void createNewLogfile();
            void sendNextLogfile();
            bool verifyLogFileSize(int newLogSize);

            unsigned long lastLogCheck = 0;
            bool sendingLogfile = false;
            unsigned long lastLogfileSentTime = 0;

            bool loggingWithoutTime;
            inline void initBootCount();
            int bootcount = 0;
            bool justStarted = true;
    };

    inline void Logger::init() {

        // Create the log subdirectories if they do not already exist
        char buf[12];
        for (int i = 0; i < NUM_SUBDIRS; i++) {
            snprintf(buf, 12, "/logs/%d", i);
            SD.mkdir(buf);
        }

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
        numberOfRecords = 0;

        if (logfile) {
            log_d("Closing logfile: %s", logfilename);
            logfile.flush();
            logfile.close();
        }

        if (timeIsValid()) {
            loggingWithoutTime = false;
        } else {
            loggingWithoutTime = true;
        }

        unsigned long timestamp = millis();
        if (!loggingWithoutTime) { timestamp = getTime(); }
        int dirName = timestamp % NUM_SUBDIRS;
        snprintf(logfilename, 100, "/logs/%d/log_%lu", dirName, timestamp);

        logfile = SD.open(logfilename, FILE_WRITE);

        if (loggingWithoutTime && justStarted) {
            char buffer[100];
            snprintf(buffer, 100, "boot %d, door %d OK; "
                     "this message was logged when millis = %lu\n",
                     bootcount, doorID, millis());
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

        if (numberOfRecords > MAX_RECORDS || verifyLogFileSize(100)) {
            createNewLogfile();
            lastLogfileSentTime = currentMillis;
        }

        logfile.print(buffer);
        logfile.flush();
        numberOfRecords++;

    }

    void Logger::logEvent(const char* message) {
        if (!sdPresent) return;

        char buffer[192];
        if (loggingWithoutTime) {
            snprintf(buffer, 192, "%lu (SYSTEM/BOOT#%d): %d  %s",
                     millis(), bootcount, doorID, message);
        } else {
            snprintf(buffer, 192, "%lu (SYSTEM): %d %s",
                     getTime(), doorID, message);
        }

        if (numberOfRecords > MAX_RECORDS || verifyLogFileSize(192)) {
            createNewLogfile();
            lastLogfileSentTime = currentMillis;
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
        if (currentMillis - lastLogfileSentTime > MAX_IDLE_TIME ) {

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

    bool Logger::verifyLogFileSize(int newLogSize) {
        File f = SD.open(logfilename);
        int size = f.size() + newLogSize;
        f.close();
        if (size >= MAX_LOG_FILE_SIZE) return true;
        return false;
    }

    void Logger::sendNextLogfile() {
        char buffer[12];
        log_d("Searching for logs in SD to send...");

        for (int i = 0; i < NUM_SUBDIRS; i++) {
            snprintf(buffer, 12, "/logs/%d", i); /* Open each dir */
            File root = SD.open(buffer);
            File entry;

            while (entry = root.openNextFile()) {
                if (entry.isDirectory()) { entry.close(); continue; }

                if (strncmp("log_", entry.name(), strlen("log_")) != 0) {
                    entry.close();
                    continue;
                }

                // "+1" means "skip the initial slash character"
                if (strcmp(logfilename+1, entry.name()) != 0) {
                    log_d("Found a logfile to send: %s", entry.name());
                    snprintf(inTransitFilename, 100, "/logs/%d/%s", i, entry.name());
                    sendingLogfile = true;
                    lastLogfileSentTime = currentMillis;
                    sendLog(inTransitFilename);
                    entry.close();
                    root.close();

                    return; // Do not send anything else
                }
            }

            entry.close();
            root.close();
        }

        log_d("No logfiles to send for now.");
    }

    void Logger::flushSentLogfile() {
        log_v("Finished sending logfile %s.", inTransitFilename);
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

