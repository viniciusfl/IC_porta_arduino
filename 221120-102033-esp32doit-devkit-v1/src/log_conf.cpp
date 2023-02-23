static const char *TAG = "log";

#include <common.h>
#include <Arduino.h>
#include <sqlite3.h>
#include <SD.h>
#include "esp_tls.h"
#include <timemanager.h>

#define BACKUP_INTERVAL 120000

#define RETRY_TIME 60000 // TODO: if we switch to alarms instead of
                         //       time intervals, how to handle this?

namespace LOGNS {
    class Logger {
        public:
            inline void init();
            void generateLog(const char* readerID, unsigned long cardID,
                            bool authorized);
            void update();

        private:
            int openlogDB();
            const static constexpr char* TAG = "Logger";
            const char* filename = "/log"; // FIXME: hardcoded?

            bool doingBackup = false;
            unsigned long lastBackupTime = 0; // TODO: remove after
                                              //       implementing alarms

            char bufferFileName[100];
            byte buffer[512];
            File logfile;
            File logbackup;
            File backup;

            inline void startBackup();
            inline void processBackup();
            inline void finishBackup();

            mbedtls_md_context_t ctx;
            mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;
    };

    inline void Logger::init(){}

    void Logger::generateLog(const char* readerID, unsigned long cardID,
                                 bool authorized) {
        //TODO: create error column in db
        logfile = SD.open(filename, FILE_APPEND);
        sprintf(bufferFileName, "%d %s %d %lu %lu\n", doorID, readerID, authorized, cardID, getTime());
        logfile.print(bufferFileName);
        logfile.close();
        log_d("Writing in log file: %s ", bufferFileName);
    }

    void Logger::update() {
        if (!sdPresent || !WiFiConnected) {
            return;
        }

        if (!doingBackup) {
            // TODO: Change to alarm
            if (currentMillis - lastBackupTime > BACKUP_INTERVAL) {
                startBackup();
            }
            return;
        }

        processBackup();
    }

    inline void Logger::startBackup() {
        log_d("Started log backup");
        doingBackup = true;
        logbackup = SD.open(filename, FILE_READ);
        sprintf(bufferFileName, "/%lu", getTime()); // FIXME: Better filename
        backup = SD.open(bufferFileName, FILE_WRITE);

        mbedtls_md_init(&ctx);
        mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 0);
        mbedtls_md_starts(&ctx);
    }

    inline void Logger::finishBackup() {
        log_d("Finished log backup");
        lastBackupTime = currentMillis;
        logbackup.close();
        backup.close();
        doingBackup = false;
    
        byte shaResult[32];
        mbedtls_md_finish(&ctx, shaResult);
        mbedtls_md_free(&ctx);

        char calculatedHash[65];
        for (int i = 0; i < 32; ++i) {
            snprintf(calculatedHash + 2*i, 3,"%02hhx", shaResult[i]);
        }
        log_d("Backup hash: %s", calculatedHash);
        backup = SD.open("/hashBackup", FILE_WRITE);
        backup.print(calculatedHash);
        backup.close();

        backup = SD.open("/latestBackup", FILE_WRITE);
        backup.print(bufferFileName);
        backup.close();
    }

    inline void Logger::processBackup() {
        int size = logbackup.read(buffer, 512);
        backup.write(buffer, size);

        mbedtls_md_update(&ctx, (const unsigned char *) buffer, size);

        if (logbackup.available() <= 0) {
            finishBackup();
        }
    }

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
        Serial.print(buf);
        return count;
    }

    Logger logger;
}

void initLog() {
    // Do not change this! Instead, define the desired level in log_conf.h
    esp_log_level_set("*", ESP_LOG_VERBOSE);

    // Log messages will be processed by the function defined above
    esp_log_set_vprintf(LOGNS::logmessage);

    LOGNS::logger.init();
}

void updateLogBackup() {
    LOGNS::logger.update();
}

void generateLog(const char* readerID, unsigned long cardID,
                    bool authorized) {
    LOGNS::logger.generateLog(readerID, cardID, authorized);
}
