static const char *TAG = "log";

// TODO: adapt this when arduino-esp32 migrates to ESP-IDF version 5.
//
// ESP-IDF version 4, currently used by arduino-esp32, offers
// the function compare_and_set_native(addr1, val, addr2)
// from <compare_set.h> . In version 5, this was replaced by
// esp_cpu_compare_and_set(addr1, val1, val2) from <esp_cpu.h>.
//
// compare_and_set_native(addr1, val, addr2) does not return
// anything. If the value in addr1 is val, it swaps the
// contents of addr1 and addr2.
//
// esp_cpu_compare_and_set(addr, val1, val2) returns a boolean
// indicating if the operation succeeded or not. It checks
// whether the value in addr in val1; if so, it changes it to
// val2 and returns true.

//#include <esp_cpu.h> // esp_cpu_compare_and_set()
#include <compare_set.h> // compare_and_set_native()
#include <stdlib.h> // rand()

#include <tramela.h>

#include <Arduino.h>
#include <SD.h>

#include <timemanager.h> // getTime()
#include <mqttmanager.h> // sendLog() and isClientConnected()

#define NUM_SIMULTANEOUS_FILES 5

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

    void rawLog(const char* message);

    class TimeStamper {
        public:
            inline void init();
            int stamp(char* buf);
        private:
            inline int countStamp(char* buf); // always use bootcount
            inline int clockStamp(char* buf); // always use current time
            int bootcount;
            bool timeAlreadySet;
    };


    inline void TimeStamper::init() {
        File boot;

        if (SD.exists("/bootcnt.txt")) {
            boot = SD.open("/bootcnt.txt", FILE_READ);
            bootcount = boot.parseInt();
            boot.close();
        }

        ++bootcount;

        boot = SD.open("/bootcnt.txt", FILE_WRITE);
        boot.print(bootcount);
        boot.close();

        char buf[120];
        int n = countStamp(buf);
        snprintf(buf +n, 120 -n, "|%d| (LOGGING): boot detected", doorID);
        rawLog(buf);
    }

    // There is a race condition here: timeAlreadySet may be set to true
    // by more than one log message and, therefore, the "switched to
    // clock time" message may be logged more than once. That is harmless.
    int TimeStamper::stamp(char* buf) {
        if (timeAlreadySet) { return clockStamp(buf); }

        if (timeIsValid()) {
            timeAlreadySet = true;

            char buf[120];
            snprintf(buf, 120, "%lu |%d| (LOGGING): disk log for boot %d "
                               "switched to clock time at %lu millis",
                               getTime(), doorID, bootcount, millis());

            rawLog(buf);

            return clockStamp(buf);
        }

        return countStamp(buf);
    }

    inline int TimeStamper::countStamp(char* buf) {
        return snprintf(buf, 30, "BOOT#%d-%lu ", bootcount, millis());
    }

    inline int TimeStamper::clockStamp(char* buf) {
        return snprintf(buf, 30, "%lu ", getTime());
    }


    TimeStamper timestamper;

    class Logfile {
        public:
            inline void init(int id);
            inline void log(const char* message);
            inline bool isMyCurrentName(const char* name);
            inline void rotate();

            uint32_t inUse; // we use this for esp_cpu_compare_and_set()
        private:
            int id;

            char filename[30];
            int numberOfRecords;
            File file;

            inline bool doesNotFit(const char* nextLogMessage);
            inline void chooseNewFileName();
            void createNewFile();
    };


    inline void Logfile::init(int id) {
        this->id = id;
        filename[0] = '\0';
        inUse = 0;
        numberOfRecords = 0;
        createNewFile();
    }

    inline void Logfile::log(const char* message) {
        if (doesNotFit(message)) { createNewFile(); }

        file.print(message);
        file.flush();
        ++numberOfRecords;
    }

    inline bool Logfile::isMyCurrentName(const char* name) {
        const char* myname = filename +12; // skip the initial "/logs/XY/AB/"
        return strcmp(myname, name) == 0;
    }

    inline void Logfile::rotate() {
        createNewFile();
    }

    inline bool Logfile::doesNotFit(const char* nextLogMessage) {
        if (numberOfRecords +1 > MAX_RECORDS) { return true; }

        int size = file.size() + strlen(nextLogMessage);
        if (size +1 > MAX_LOG_FILE_SIZE) { return true; }

        return false;
    }

    void Logfile::createNewFile() {
        if (!sdPresent) { return; }

        char buf[100];
        int n;

        if (file) {
            // Using ordinary log functions here may deadlock.
            // XXX Doing this here means we might end up with
            // a file a little larger than MAX_LOG_FILE_SIZE!
            n = timestamper.stamp(buf);
            snprintf(buf +n, 100 -n,
                     "|%d| (LOGGING): Closing logfile: %s",
                      doorID, filename);
            file.print(buf);
            file.flush();
            file.close();
        }

        chooseNewFileName();

        file = SD.open(filename, FILE_WRITE);

        // No logfile is ever empty because of this, so
        // we never need to check for an empty file.
        n = timestamper.stamp(buf);
        snprintf(buf +n, 100 -n,
                 "|%d| (LOGGING): Created new logfile: %s",
                 doorID, filename);
        file.print(buf);
        file.flush();
        numberOfRecords = 1;
    }

    // We just need a random filename
    inline void Logfile::chooseNewFileName() {
        unsigned long n = millis();

        bool validName = false;
        while (! validName) {
            snprintf(filename, 30, "/logs/%.2d/%.2d/%.8lu.log",
                                   n % NUM_SUBDIRS, id,
                                   n % 100000000); // 8 chars max

            if (SD.exists(filename)) {
                ++n;
            } else {
                validName = true;
            }
        }
    }


    class Logger {
        public:
            inline void init();

            void logEvent(const char* message);
            void logAccess(const char* readerID, unsigned long cardID,
                            bool authorized);
            void logAnything(const char* message);

            inline bool fileIsInUse(const char* filename);
            inline void rotateSomeLog();
        private:
            Logfile files[NUM_SIMULTANEOUS_FILES];

            Logfile* logfile;

            void chooseLogfile();
            void releaseLogfile();
    };


    inline void Logger::init() {
        logfile = NULL;

        // Create the log subdirectories if they do not already exist
        char buf[12];
        for (int i = 0; i < NUM_SUBDIRS; ++i) {
            for (int j = 0; j < NUM_SIMULTANEOUS_FILES; ++j) {
                snprintf(buf, 12, "/logs/%.2d/%.2d", i, j);
                SD.mkdir(buf);
            }
        }

        for (int i = 0; i < NUM_SIMULTANEOUS_FILES; ++i) {
            files[i].init(i);
        }

        timestamper.init();
    }



    // logEvent and logAccess are very similar
    void Logger::logEvent(const char* message) {
        if (!sdPresent) return;

        char buffer[1024];
        int tsSize = timestamper.stamp(buffer);

        snprintf(buffer +tsSize, 1024 - tsSize, "|%d| (SYSTEM): %s",
                 doorID, message);

        logAnything(buffer);
    }

    // logEvent and logAccess are very similar
    void Logger::logAccess(const char* readerID, unsigned long cardID,
                            bool authorized) {
        if (!sdPresent) return;

        const char* status;
        if (authorized) {
            status = "authorized";
        } else {
            status = "not authorized";
        }

        char buffer[1024];
        int tsSize = timestamper.stamp(buffer);

        snprintf(buffer + tsSize, 1024 - tsSize,
                 "|%d| (ACCESS): reader %s, ID %lu %s",
                 doorID, readerID, cardID, status);

        logAnything(buffer);
    }

    // TODO Check the comment at the beginning of the file.
    // This should work with ESP-IDF version >= 5
    /*
    void Logger::chooseLogfile() {
        int id = rand();
        do {
            ++id;
            id %= NUM_SIMULTANEOUS_FILES;
            logfile = &(files[id]);
        } while (not esp_cpu_compare_and_set(&(logfile->inUse), 0, 1))
    }
    */

    void Logger::chooseLogfile() {
        int id = rand();
        uint32_t result = 1;
        do {
            ++id;
            id %= NUM_SIMULTANEOUS_FILES;
            logfile = &(files[id]);
            compare_and_set_native(&(logfile->inUse), 0, &result);
        } while (result == 1);
    }

    void Logger::releaseLogfile() {
        logfile->inUse = 0;
        logfile = NULL;
    }

    void Logger::logAnything(const char* message) {
        chooseLogfile();
        logfile->log(message);
        releaseLogfile();
    }

    inline void Logger::rotateSomeLog() {
        chooseLogfile();
        logfile->rotate();
        releaseLogfile();
    }

    inline bool Logger::fileIsInUse(const char* filename) {
        for (int i = 0; i < NUM_SIMULTANEOUS_FILES; ++i) {
            if (files[i].isMyCurrentName(filename)) { return true; }
        }
        return false;
    }


    Logger logger;

    void rawLog(const char* message) {
        logger.logAnything(message);
    }


    class LogManager {
        public:
            void uploadLogs(); // called periodically
            void flushSentLogfile(); // erase logfiles that have been sent ok
        private:
            bool sendingLogfile = false;
            char inTransitFilename[30]; // current file we're sending
            bool sendNextLogfile();
            unsigned long lastLogCheckTime = 0;
            unsigned long lastLogSentTime = 0;
            bool findFileToSend();
    };


    void LogManager::uploadLogs() {
        if (!sdPresent) { return; }

        if (currentMillis - lastLogCheckTime < LOG_SEARCH_INTERVAL) { return; }

        lastLogCheckTime = currentMillis;

        // If we are already sending a file or are offline,
        // we should wait before sending anything else
        if (sendingLogfile || !isClientConnected()) { return; }

        if (sendNextLogfile()) {
            lastLogSentTime = currentMillis;
        } else {
            // There was no file to send. If it's been too long since
            // we sent anything, force some logfile to be rotated so
            // we do send something on the next iteration.
            if (currentMillis - lastLogSentTime > MAX_IDLE_TIME) {
                logger.rotateSomeLog();
            }
        }
    }

    void LogManager::flushSentLogfile() {
        log_v("Finished sending logfile %s.", inTransitFilename);
        sendingLogfile = false;
        log_d("Removing sent logfile: %s", inTransitFilename);

        // This should never be false
        if (SD.exists(inTransitFilename)) { SD.remove(inTransitFilename); }
        inTransitFilename[0] = 0;
    }

    // TODO this may take some time, so it might make the system
    //      unresponsive. If this proves to be the case, we will
    //      need to do something.
    bool LogManager::sendNextLogfile() {
        log_d("Searching for logs in SD to send...");

        if (findFileToSend()) {
            log_d("Found a logfile to send: %s", inTransitFilename);
            sendingLogfile = true;
            sendLog(inTransitFilename);
            return true;
        } else {
            log_d("No logfiles to send for now.");
            inTransitFilename[0] = 0;
            return false;
        }
    }

    // This uses 2 open files (we can only have 5)
    bool LogManager::findFileToSend() {
        bool found = false;

        for (int i = 0; i < NUM_SUBDIRS && ! found; ++i) {
            for (int j = 0; j < NUM_SIMULTANEOUS_FILES && ! found; ++j) {
                char dirnamebuf[12];
                snprintf(dirnamebuf, 12, "/logs/%.2d/%.2d", i, j);
                File dir = SD.open(dirnamebuf);

                while (!found) {
                    File f = dir.openNextFile();
                    if (!f) { break; }

                    char filenamebuf[30];
                    snprintf(filenamebuf, 30, "%s/%s", dirnamebuf , f.name());
                    f.close();
                    if(! logger.fileIsInUse(filenamebuf)) {
                        strncpy(inTransitFilename, filenamebuf, 30);
                        found = true;
                    }
                }

                dir.close();
            }
        }

        return found;
    }

    LogManager manager;


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

void uploadLogs() {
    LOGNS::manager.uploadLogs();
}

void flushSentLogfile() {
    LOGNS::manager.flushSentLogfile();
}
