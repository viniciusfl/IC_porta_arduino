static const char *TAG = "log";

#include <tramela.h>

#include <Arduino.h>
#include <SD.h>

#include <timemanager.h> // getTime()
#include <mqttmanager.h> // sendLog and isClientConnected

// This does four things:
//
//  1. Receives log messages and writes them to a file on the disk
//  2. When the file is "big", closes it and opens a new one
//  3. Looks for closed log files and enqueues them to be sent
//     over MQTT to the controlling server (taking care to only
//     do that if we are online and if there is no file already
//     being uploaded)
//  4. When uploading is successful, a callback is called that
//     deletes the file that was just sent.
//
// Log messages are both system logs and access logs, and they are
// all mixed together; it is up to the server to separate them.
//
// We may need to log stuff before the system time is correct.
// When this happens, we (1) use millis() as the time and (2) add
// "BOOT#XX" (where "XX" is the boot count) to the log messages so
// that the server can order them.


// Rotate the logfile if it reaches this many records or this total
// size. We want to keep each file small because they are copied to
// memory for transmission to the MQTT broker. We also want to
// send out information to the broker regularly, so it is easier
// to detect whether we crashed or lost connectivity.
#define MAX_RECORDS 100
#define MAX_LOG_FILE_SIZE 5000 // 5kb

// If we have no file to send and the current log file has not been
// rotated for this long, rotate it so we send something to the
// broker. This guarantees, if everything is ok, that the broker
// hears from us on a somewhat regular basis.
#define MAX_IDLE_TIME 3600000 // 1 hour

// Check the disk periodically for logfiles to upload.
#define LOG_SEARCH_INTERVAL 15000 // 15 seconds

// We divide the log files in a few subdirectories to avoid filesystem
// performance issues.
#define NUM_SUBDIRS 10


namespace LOGNS {
    class Logger {
        public:
            inline void init();
            void flushSentLogfile(); // erase logfiles that have been sent ok
            void uploadLogs(); // called periodically
            void logEvent(const char* message);
            void logAccess(const char* readerID, unsigned long cardID,
                            bool authorized);
        private:
            void logAnything(const char* message);

            File logfile;
            char logfilename[100]; // current file we're logging to
            int numberOfRecords;

            char inTransitFilename[100]; // current file we're sending

            void createNewLogfile();
            bool sendNextLogfile();
            bool logfileTooBig(const char* nextLogMessage);

            unsigned long lastLogCheck = 0;
            bool sendingLogfile = false;
            unsigned long lastLogRotationTime = 0;

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
        if (!sdPresent) { return; }

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

        lastLogRotationTime = timestamp;
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

    void Logger::logAnything(const char* message) {
        if (logfileTooBig(message)) { createNewLogfile(); }

        logfile.print(message);
        logfile.flush();
        ++numberOfRecords;
    }

    void Logger::logAccess(const char* readerID, unsigned long cardID,
                            bool authorized) {
        if (!sdPresent) return;

        char buffer[1024];
        if (loggingWithoutTime) {
            snprintf(buffer, 1024, "%lu (ACCESS/BOOT#%d): %d %s %d %lu",
                    millis(), bootcount, doorID, readerID, authorized, cardID);
        } else {
            snprintf(buffer, 1024, "%lu (ACCESS): %d %s %d %lu",
                    getTime(), doorID, readerID, authorized, cardID);
        }

        logAnything(buffer);
    }

    void Logger::logEvent(const char* message) {
        if (!sdPresent) return;

        char buffer[1024];
        if (loggingWithoutTime) {
            snprintf(buffer, 1024, "%lu (SYSTEM/BOOT#%d): %d  %s",
                     millis(), bootcount, doorID, message);
        } else {
            snprintf(buffer, 1024, "%lu (SYSTEM): %d %s",
                     getTime(), doorID, message);
        }

        logAnything(buffer);
    }

    void Logger::uploadLogs() {
        if (!sdPresent) return;

        // If we are already sending a file or are offline,
        // we should wait before sending anything else
        if (sendingLogfile || !isClientConnected()) { return; }

        if (currentMillis - lastLogCheck < LOG_SEARCH_INTERVAL) { return; }

        lastLogCheck = currentMillis;

        if (!sendNextLogfile()) {
            // There was no file to send. If the file currently being
            // written to is "too old", rotate it to force something
            // to be sent on the next iteration.
            if (currentMillis - lastLogRotationTime > MAX_IDLE_TIME ) {
                log_w("We're alive!"); // Make sure the file is not empty
                createNewLogfile();
            }
        }
    }

    bool Logger::logfileTooBig(const char* nextLogMessage) {
        if (numberOfRecords +1 > MAX_RECORDS) { return true; }

        File f = SD.open(logfilename);
        int size = f.size() + strlen(nextLogMessage);
        f.close();
        // ">=" and not ">" because we will add a "\0" character
        if (size >= MAX_LOG_FILE_SIZE) { return true; }

        return false;
    }

    bool Logger::sendNextLogfile() {
        char buffer[12];
        log_d("Searching for logs in SD to send...");

        for (int i = 0; i < NUM_SUBDIRS; i++) {
            snprintf(buffer, 12, "/logs/%d", i); /* Open each dir */
            File root = SD.open(buffer);
            File entry;

            while (entry = root.openNextFile()) {
                // Skip directories, files that are not named like "log_*",
                // and the logfile that is still being written to.
                // "+1" means "skip the initial slash character"
                if (
                          entry.isDirectory()
                      || (strncmp("log_", entry.name(), strlen("log_")) != 0)
                      || (strcmp(logfilename+1, entry.name()) != 0)
                   )
                      { entry.close(); continue; }

                log_d("Found a logfile to send: %s", entry.name());
                snprintf(inTransitFilename, 100, "/logs/%d/%s", i, entry.name());
                sendingLogfile = true;
                sendLog(inTransitFilename);
                entry.close();

                // Do not send more than one file at the same time
                root.close();
                return true;
            }

            root.close();
        }

        log_d("No logfiles to send for now.");
        return false;
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
        char buf[1024];
        buf[0] = 0;
        count = vsnprintf(buf, 1024, format, ap);

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

void uploadLogs() {
    LOGNS::logger.uploadLogs();
}

