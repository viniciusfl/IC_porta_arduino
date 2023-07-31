static const char *TAG = "log";

#include <tramela.h>

#include <Arduino.h>
#include <SD.h>

#include <freertos/queue.h>
#include <freertos/task.h>

#include <dirent.h>
#include <fnmatch.h>

#include <nvs_flash.h>
#include <nvs.h>

#include <timemanager.h> // getTime()
#include <mqttmanager.h> // sendLog() and isClientConnected()

/*
  This code writes log messages to disk files (guaranteeing they are not
  lost if the system is shut down) and uploads these files over MQTT when
  the network is available. It also maintains a ring buffer in memory with
  the latest log messages. If there is no SD card available, this ring
  buffer may still be accessed.

  Log messages are both system logs and access logs, and they are all
  mixed together; it is up to the server to separate them. For the
  system logs, we reconfigure the ESP32 logging system to use our own
  vlogEvent() function. For access logs, we use the logAccess()function.
  For messages generated during the initialization of the logging system,
  we use logLogEvent().

  We may need to log stuff before the system time is correct. When this
  happens, we (1) use millis() as the time and (2) add "BOOT#XX" (where
  "XX" is the boot count) to the log messages so that the server can
  order them.
  -----

  The docs state that "log calls are thread-safe":
  https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/log.html#thread-safety

  But this is not exactly true: the default code simply calls vfprintf.
  This works because, by default, log messages go to the UART, and writes
  to the UART are essetially atomic (because they busy-wait until all
  data is written):
  https://docs.espressif.com/projects/esp-idf/en/v4.4.5/esp32/api-reference/storage/vfs.html?highlight=vfs#standard-io-streams-stdin-stdout-stderr

  When creating our own logging mechanism, however, it is up to us to
  guarantee things will work correctly:
  https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/log.html#_CPPv419esp_log_set_vprintf14vprintf_like_t

  We might use this: <https://github.com/esp32m/logging>. I chose not
  to because (1) it is quite complex and has a lot of features we do
  not need, but there are only 11 commits and 2 contributors; and (2)
  it apparently is not very conservative in terms of memory usage.

  What we do is:

  1. We replace the default log function with our own. This function
     writes the message information to a buffer and sends the pointer
     to it to a FreeRTOS queue, because these queues are thread-safe

  2. On a separate task, we process what comes from the queue by writing
     the formatted message to a ring buffer; since this is a single task,
     there are no synchronization issues

  3. On yet another task, we read what is in the ring buffer and write
     to a logfile on disk (using the Logfile class); again, this is a
     single task, so there are no synchronization issues.

  4. On the default task, we periodically check for files to send over
     MQTT.

  If there is no SD card available, we do not perform steps 3 and 4;
  log messages are still temporarily available in the ring buffer.

  In step 1, we do not copy the data to a new buffer. This means we
  cannot return to the caller right away, as that would deallocate the
  memory. Instead, we wait for step 2 to finish (when the data is copied
  to the ringbuffer).

  In step 2, we do not write to disk directly because we want to return
  from the log call as fast as possible, and writing to disk may take
  a long time.

  In step 3, we close the current file and create a new one if it gets
  "big" (files need to fit in memory to be uploaded over MQTT).

  In step 4, we take care to only send a file if it is already closed, if
  we are actually online and if we are not currently uploading any files
  (sending multiple files concurrently would consume too much memory).
  After the file is successfully sent, it is deleted.

  Log messages are not simple strings; they are often gererated in
  printf style, i.e., a format string and some parameters, such as
  log_e("error %s detected", errorstring). Generating the complete
  log message depends on allocating a new memory buffer to write
  the message to. This may take too much stack space in some tasks
  (notably, the system event task, which by default has a stack size
  of 2304 bytes), so we do this in the ringbufWriter task instead.

  ---

  The vlogEvent, logAccess, and logLogEvent functions, along with the
  TimeStamper class, are responsible for adding the message type and
  timestamp to the messages and sending them over to enqueueLogMessage
  and venqueueLogMessage. The ringbufWriter function processes the
  queue, interprets the format string to render the complete message,
  and saves the messages in the Ringbuf class, which stores the latest
  messages in memory; the ringbufReader function uses the Logfile
  class, which reads messages from the ringbuf and writes logs to disk,
  as well as rotates the file being written to. The LogManager class is
  responsible for uploading the files; the rest of the code is used to
  receive and process the log messages in a thread-safe manner.
*/


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

// Divide the log files in a few subdirectories to avoid filesystem
// performance issues.
#define NUM_SUBDIRS 10

#define RINGBUF_SIZE 5000

namespace LOGNS {

    // We use a FreeRTOS queue to process log messages
    typedef struct {
        const char* type; // "LOGGING", "SYSTEM", or "ACCESS"
        const char* timestamp;
        const char* format;
        va_list* ap_ptr;
        TaskHandle_t taskID;
    } QueueMessage;

#   define QUEUE_LENGTH 10
#   define QUEUE_ITEM_SIZE sizeof(QueueMessage)

    QueueHandle_t logQueue;

    bool logToDisk = false;

    int IRAM_ATTR venqueueLogMessage(const char* type,
                                      const char* timestamp,
                                      const char* format, va_list& ap) {

        QueueMessage msg = {type, timestamp, format, &ap,
                            xTaskGetCurrentTaskHandle()};

        BaseType_t result = xQueueSendToBack(logQueue,
                                             (void*) &msg,
                                             pdMS_TO_TICKS(400)); // 400ms

        // this message will be lost; shouldn't really happen
        if (result != pdTRUE) { return 0; }

        // We should wait forever, otherwise the other thread may access
        // memory that is no longer valid. However, waiting forever may
        // make the system freeze if something goes wrong, so we just
        // wait for a "long" time (4s).
        uint32_t count;
        xTaskNotifyWait(0, 0, &count, pdMS_TO_TICKS(4000));
        return count;
    }

    int IRAM_ATTR enqueueLogMessage(const char* type,
                                     const char* timestamp,
                                     const char* format, ...) {

        va_list ap;
        va_start(ap, format);
        int count = venqueueLogMessage(type, timestamp, format, ap);
        va_end(ap);
        return count;
    }


    int logLogEvent(const char* format, ...);


    // A ring buffer for null-terminated text messages. If the buffer
    // fills up, older messages are dropped.
    //
    // This may be written to and read from at the same time with no
    // locking (but only one reader and one writer at a time). If the
    // buffer fills up, there may be a race condition as the reader
    // processes a message that the writer has dropped. In that case,
    // read() detects there was a problem and returns false to indicate
    // that whatever was written out is garbage.
    class Ringbuf {
        public:
            Ringbuf() {
                first = last = 0;
                thebuf[0] = '\0';
            }

            inline bool empty() { return last == first; };
            inline void write(const char* message);
            bool read(char* buf); // always check empty() before calling this
        private:
            int first;
            int last;
            char thebuf[RINGBUF_SIZE];
            void addchar(char a);
    };

    inline void Ringbuf::write(const char* message) {
        if (message == NULL or *message == '\0') { return; };

        const char* i = message;
        while (*i != '\0') { addchar(*i++); };
        addchar('\0');
    }

    void Ringbuf::addchar(char a) {
        thebuf[last++] = a;

        if (last >= RINGBUF_SIZE) { last = 0; };

        // The buffer is full; let's drop the first message
        if (last == first) {
            while (thebuf[first++] != '\0') {
                if (first >= RINGBUF_SIZE) { first = 0; };
            };
            if (first >= RINGBUF_SIZE) { first = 0; };
        }
    }

    // There are three race conditions here:
    //
    // 1. If the buffer fills up, the contents of "thebuf" may be
    //    overwritten by the writing thread while we are processing it,
    //    so we use "oldfirst" to detect that
    //
    // 2. We may update "first" after addchar() has already updated it.
    //    That is not a problem because we would set it to the same
    //    value that addchar() did, which is the beginning of the next
    //    message (unless there were two overwritten messages, but that
    //    is not likely)
    //
    // 3. We may update "first" after addchar() has tested for its value.
    //    That would not be a problem because, like in the case above,
    //    addchar() would simply update "first" again to the same value.
    //    More importantly, this should never happen in practice because
    //    addchar() is always run from a higher-priority thread.
    bool Ringbuf::read(char* outbuf) {
        int i = first;
        int oldfirst = i;
        int j = 0;

        while (thebuf[i] != '\0') {
            outbuf[j++] = thebuf[i++];
            if (i >= RINGBUF_SIZE) { i = 0; }
        }

        outbuf[j] = '\0';
        ++i;
        if (i >= RINGBUF_SIZE) { i = 0; }

        if (oldfirst != first) { return false; }

        first = i;

        return true;
    }

    Ringbuf ringbuf;


    class TimeStamper {
        public:
            inline void init();
            int stamp(char* buf);
        private:
            inline void getBootcountFromNVS();
            inline int countStamp(char* buf); // always uses bootcount
            inline int clockStamp(char* buf); // always uses current time
            uint32_t bootcount;
            bool timeAlreadySet;
    };

    inline void TimeStamper::init() {
        getBootcountFromNVS();
    }

    inline void TimeStamper::getBootcountFromNVS() {
        bootcount = 0;

        logLogEvent("Initializing NVS");
        esp_err_t err = nvs_flash_init();
        if  ( err == ESP_ERR_NVS_NO_FREE_PAGES
           || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {

            // NVS partition was truncated and needs to be erased
            ESP_ERROR_CHECK(nvs_flash_erase());
            err = nvs_flash_init();
        }
        ESP_ERROR_CHECK( err );

        logLogEvent("Opening NVS");
        nvs_handle_t nvsHandle;
        err = nvs_open("storage", NVS_READWRITE, &nvsHandle);
        if (err != ESP_OK) {
            logLogEvent("Error (%s) opening NVS handle!",
                        esp_err_to_name(err));

            return;
        }

        logLogEvent("NVS OK!");

        err = nvs_get_u32(nvsHandle, "bootcount", &bootcount);
        switch (err) {
            case ESP_OK:
                ++bootcount;
                break;
            case ESP_ERR_NVS_NOT_FOUND:
                bootcount = 1;
                logLogEvent("No bootcount in NVS (this is the first boot)" );
                break;
            default:
                bootcount = 0;
                logLogEvent("Error (%s) reading bootcount from NVS",
                            esp_err_to_name(err));
                nvs_close(nvsHandle);
                return;
        }

        logLogEvent("This is boot #%u", bootcount);

        err = nvs_set_u32(nvsHandle, "bootcount", bootcount);
        if (ESP_OK != err) {
            logLogEvent("Error (%s) writing bootcount to NVS",
                        esp_err_to_name(err));
            nvs_close(nvsHandle);
            return;
        }

        logLogEvent("Commiting updates to NVS");
        err = nvs_commit(nvsHandle);
        if (ESP_OK != err) {
                logLogEvent("Error (%s) commiting bootcount to NVS",
                            esp_err_to_name(err));
                nvs_close(nvsHandle);
                return;
        }

        nvs_close(nvsHandle);
    }

    // There is a race condition here: timeAlreadySet may be set to true
    // by more than one log message and, therefore, the "switched to
    // clock time" message may be logged more than once. That is harmless.
    int TimeStamper::stamp(char* buf) {
        if (timeAlreadySet) { return clockStamp(buf); }

        if (timeIsValid()) {
            timeAlreadySet = true;

            logLogEvent("disk log for boot %u switched to clock time "
                        "at %lu millis", bootcount, millis());

            return clockStamp(buf);
        }

        return countStamp(buf);
    }

    inline int TimeStamper::countStamp(char* buf) {
        return snprintf(buf, 30, "BOOT#%u-%lu", bootcount, millis());
    }

    inline int TimeStamper::clockStamp(char* buf) {
        return snprintf(buf, 30, "%lu", getTime());
    }

    TimeStamper timestamper;


    class Logfile {
        public:
            inline void init();
            inline void log(const char* message);
            inline bool isMyCurrentName(const char* name);
            inline void rotate();
            bool shouldRotate;
            void createNewFile();
        private:
            char filename[24]; // "/logs/XY/12345678.log" uses 21+1 chars
            int numberOfRecords;
            File file;

            inline bool doesNotFit(const char* nextLogMessage);
            inline void chooseNewFileName();
    };

    inline void Logfile::init() {

        // Create the log subdirectories if they do not already exist
        SD.mkdir("/logs");
        char buf[10];
        for (int i = 0; i < NUM_SUBDIRS; ++i) {
            snprintf(buf, 10, "/logs/%.2d", i);
            SD.mkdir(buf);
        }

        filename[0] = '\0';
        numberOfRecords = 0;
        shouldRotate = false;
        createNewFile();
    }

    // TODO: either make sure we never go over a specific maximum file
    //       size OR instead of calling createNewFile() here just set
    //       shouldRotate to true.
    inline void Logfile::log(const char* message) {
        Serial.println(message);

        if (doesNotFit(message)) { createNewFile(); }

        file.print(message);
        file.flush();
        ++numberOfRecords;
    }

    inline bool Logfile::isMyCurrentName(const char* name) {
        if (strcmp(filename, name) == 0) { return true; } // check full path

        const char* myname = filename +9; // skip the initial "/logs/XY/"
        return strcmp(myname, name) == 0;
    }

    inline void Logfile::rotate() {
        shouldRotate = true;
    }

    inline bool Logfile::doesNotFit(const char* nextLogMessage) {
        if (numberOfRecords +1 > MAX_RECORDS) { return true; }

        int size = file.size() + strlen(nextLogMessage);
        if (size +1 > MAX_LOG_FILE_SIZE) { return true; }

        return false;
    }

    void Logfile::createNewFile() {
        char buf[100];
        int n;

        if (file) {
            // TODO with the new code, no deadlocks may occur; should
            //      we switch to ordinary log messages to make sure
            //      no file ever exceeds MAX_LOG_FILE_SIZE?
            // Using ordinary log functions here may deadlock.
            // XXX Doing this here means we might end up with
            // a file larger than MAX_LOG_FILE_SIZE!
            n = timestamper.stamp(buf);
            snprintf(buf +n, 100 -n,
                     " |%d| (LOGGING): Closing logfile: %s",
                      doorID, filename);
            file.print(buf);
            file.flush();
            file.close();
        }

        chooseNewFileName();

        file = SD.open(filename, FILE_WRITE);

        // TODO should we use ordinary logging here and
        //      forfeit this guarantee?  
        // No logfile is ever empty because of this, so
        // we never need to check for an empty file.
        n = timestamper.stamp(buf);
        snprintf(buf +n, 100 -n,
                 " |%d| (LOGGING): Created new logfile: %s",
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
            snprintf(filename, 24, "/logs/%.2d/%.8lu.log",
                                   n % NUM_SUBDIRS,
                                   n % 100000000); // 8 chars max

            if (SD.exists(filename)) {
                ++n;
            } else {
                validName = true;
            }
        }
    }

    Logfile logfile;


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
        if (!logToDisk) { return; }

        if (currentMillis - lastLogCheckTime < LOG_SEARCH_INTERVAL) { return; }

        lastLogCheckTime = currentMillis;

        // If we are already sending a file or are offline,
        // we should wait before sending anything else
        if (sendingLogfile || !isClientConnected()) { return; }

        if (sendNextLogfile()) {
            lastLogSentTime = currentMillis;
        } else {
            // There was no file to send. If it's been too long since
            // we sent anything, force the logfile to be rotated so
            // we do send something on the next iteration.
            if (currentMillis - lastLogSentTime > MAX_IDLE_TIME) {
                logfile.rotate();
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
    //      need to do something. A simple idea is to put this in
    //      a low-priority task.
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

    // Using readdir() instead of SD.openNextFile() here allows us to
    // use only one file descriptor (the directory) instead of two (the
    // directory and the file, necessary to get the filename).
    bool LogManager::findFileToSend() {
        bool found = false;

        for (int i = 0; i < NUM_SUBDIRS && ! found; ++i) {
            char dirnamebuf[15];
            snprintf(dirnamebuf, 15, "/sd/logs/%.2d", i);

            DIR* dir = opendir(dirnamebuf);

            while (!found) {
                struct dirent* entry = readdir(dir);

                if (NULL == entry) { break; }

                int result = fnmatch("*.log", entry->d_name, 0);

                // Skip stuff like ".", ".." etc.
                if (result == FNM_NOMATCH) { continue; }

                if (result != 0) {
                    log_w("Something wrong happened while "
                          "searching for a logfile to send");
                    break;
                }

                // "+3" means "skip the initial '/sd' "
                char filenamebuf[30];
                snprintf(filenamebuf, 30, "%s/%s", dirnamebuf +3,
                         entry->d_name);

                if(! logfile.isMyCurrentName(filenamebuf)) {
                    strncpy(inTransitFilename, filenamebuf, 30);
                    found = true;
                }
            }

            closedir(dir);
        }

        return found;
    }

    LogManager manager;


    uint8_t queueStorage[QUEUE_LENGTH * QUEUE_ITEM_SIZE];
    StaticQueue_t queueBuffer;

    StaticTask_t readerTaskBuffer;
    StackType_t readerTaskStackStorage[5120];
    TaskHandle_t readerTask;

    StaticTask_t writerTaskBuffer;
    StackType_t writerTaskStackStorage[4096];
    TaskHandle_t writerTask;

    void ringbufReader(void* params) {
        char buf[1024];

        for (;;) {
            if (ringbuf.empty()) {
                if (logfile.shouldRotate) { logfile.createNewFile(); }
                xTaskNotifyWait(0, 0, NULL, pdMS_TO_TICKS(400)); // 400ms
            }

            if (!ringbuf.empty() and ringbuf.read(buf)) { logfile.log(buf); }
        }
    }
   
    // It would be better if xQueueReceive would block indefinitely if
    // there are no messages in the queue, but apparently this behavior
    // is not enabled with ESP32 (INCLUDE_vTaskSuspend is not 1), so
    // we set an arbitrary timeout and check for the return status.
    void ringbufWriter(void* params) {
        QueueMessage received;
        char buf[1024];

        for(;;) {
            if (pdTRUE == xQueueReceive(logQueue, &received,
                                        pdMS_TO_TICKS(10000))) { // 10s

                buf[0] = 0;
                uint32_t n = snprintf(buf, 1024, "%s |%d| (%s): ",
                                      received.timestamp,
                                      doorID,
                                      received.type);

                n += vsnprintf(buf +n, 1024 -n, received.format,
                               *(received.ap_ptr));

                ringbuf.write(buf);
                xTaskNotify(received.taskID, n, eSetValueWithOverwrite);
                if (logToDisk) { xTaskNotify(readerTask, 0, eNoAction); }
            }
        }
    }


    // vlogEvent() receives a va_list, this receives "..."
    int IRAM_ATTR logLogEvent(const char* format, ...) {
        char buf[30];
        timestamper.stamp(buf);
        va_list ap;
        va_start(ap, format);
        int count = venqueueLogMessage("LOGGING", buf, format, ap);
        va_end(ap);
        return count;
    }

    int logAccess(const char* readerID, unsigned long cardID,
                                             bool authorized) {

        const char* status;
        if (authorized) {
            status = "authorized";
        } else {
            status = "not authorized";
        }

        char buf[30];
        int count = timestamper.stamp(buf);
        count += enqueueLogMessage("ACCESS", buf, "reader %s, ID %lu %s",
                                   readerID, cardID, status);

        return count;
    }

    int IRAM_ATTR vlogEvent(const char* format, va_list ap) {
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
        char buf[30];
        int count = timestamper.stamp(buf);
        count += venqueueLogMessage("SYSTEM", buf, format, ap);
        return count;
    }

    void init() {
        logQueue = xQueueCreateStatic(QUEUE_LENGTH, QUEUE_ITEM_SIZE,
                                        queueStorage, &queueBuffer);

        writerTask = xTaskCreateStaticPinnedToCore(
                                    ringbufWriter,
                                    "writerTask",
                                    4096, // stack size
                                    (void*) 1, // params, we are not using this
                                    (UBaseType_t) 6, // priority; the MQTT task uses 5
                                    writerTaskStackStorage,
                                    &writerTaskBuffer,
                                    tskNO_AFFINITY);

        timestamper.init();
    }

    void initDiskLog() {
        logfile.init();

        readerTask = xTaskCreateStaticPinnedToCore(
                                    ringbufReader,
                                    "readerTask",
                                    5120, // stack size
                                    (void*) 1, // params, we are not using this
                                    (UBaseType_t) 5, // priority
                                    readerTaskStackStorage,
                                    &readerTaskBuffer,
                                    tskNO_AFFINITY);

        logToDisk = true;
    }
}

void initLog() {
    LOGNS::init();

    // Do not change this! Instead, define the desired level in log_conf.h
    esp_log_level_set("*", ESP_LOG_VERBOSE);

    // Log messages will be processed by the function defined above
    esp_log_set_vprintf(LOGNS::vlogEvent);
}

void initDiskLog() { LOGNS::initDiskLog(); }

void logAccess(const char* readerID, unsigned long cardID,
                    bool authorized) {
    LOGNS::logAccess(readerID, cardID, authorized);
}

void uploadLogs() {
    LOGNS::manager.uploadLogs();
}

void flushSentLogfile() {
    LOGNS::manager.flushSentLogfile();
}
