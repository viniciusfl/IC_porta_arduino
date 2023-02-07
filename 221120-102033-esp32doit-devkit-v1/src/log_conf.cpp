static const char *TAG = "log";

#include <common.h>
#include <Arduino.h>
#include <sqlite3.h>
#include <SD.h>
#include "esp_tls.h"

#define BACKUP_INTERVAL 60000

#define RETRY_TIME 10000 // TODO: if we switch to alarms instead of
                         //       time intervals, how to handle this?

namespace LOGNS {
    class Log { // TODO: better name
        public:
            inline void init();
            void updateBackup(unsigned long time);
            void generateLog(const char* readerID, unsigned long cardID,
                            bool authorized, unsigned long time);

        private:
            inline void startBackup(unsigned long time);
            inline void finishBackup();
            inline bool processBackup();
            bool doingBackup = false;
            const char* filename = "/sd/log.db"; // FIXME: hardcoded?
            unsigned long lastBackupTime = 0; // TODO: remove after implement alarm
            char backupFilename[50];

            inline void startChecksum();
            inline void processChecksum();
            inline void finishChecksum();
            bool doingChecksum;
            mbedtls_md_context_t ctx;
            mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;

            File f;
            sqlite3_backup *backupHandler;
            sqlite3 *backupdb;

            int openlogDB();
            sqlite3 *logdb;
            sqlite3_stmt *logquery;

    };

    int logmessage(const char* format, va_list ap);

    inline void Log::init() {
        // check the comment near Authorizer::closeDB()
        logdb = NULL;
        logquery = NULL;
        backupHandler = NULL;
        backupdb = NULL;

        int rc = openlogDB();
        if (rc != SQLITE_OK){
            log_e("Couldn't open log db: %s, aborting...",
                  sqlite3_errmsg(backupdb));
            while (true) delay(10);
        }
        log_v("Openned log db");

        // Do not change this! Instead, define the desired level in log_conf.h
        esp_log_level_set("*", ESP_LOG_VERBOSE);

        // Log messages will be processed by the function defined above
        esp_log_set_vprintf(logmessage);
    }

    void Log::updateBackup(unsigned long time) {
        if (!doingBackup && !doingChecksum) {
            // TODO: Change to alarm
            if (currentMillis - lastBackupTime > BACKUP_INTERVAL) {
                startBackup(time);
            }
            return;
        }

        if(doingBackup) {
            if (processBackup()) {
                startChecksum();
            }
            return;
        }

        processChecksum();
    }

    inline void Log::startBackup(unsigned long time) {
        log_v("Started log DB backup");

        sprintf(backupFilename, "/sd/%lu.db", time);

        int rc = sqlite3_open_v2(backupFilename, &backupdb,
                 SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);

        if (rc != SQLITE_OK) {
            log_w("Aborting update... Couldn't open backup db: %s",
                  sqlite3_errmsg(backupdb));

            lastBackupTime += RETRY_TIME;
            finishBackup();
            return;
        }

        backupHandler = sqlite3_backup_init(backupdb, "main",
                                        logdb, "main");

        if (!backupHandler) {
            log_w("Problem with backup init");

            lastBackupTime += RETRY_TIME;
            finishBackup();
            return;
        }

        doingBackup = true;
    }

    inline void Log::finishBackup() {
        sqlite3_backup_finish(backupHandler);
        sqlite3_close(backupdb);
        backupHandler = NULL;
        backupdb = NULL;
        doingBackup = false;
    }

    inline bool Log::processBackup() {
        int rc = sqlite3_backup_step(backupHandler, 5);

        if (rc == SQLITE_OK || rc == SQLITE_BUSY || rc == SQLITE_LOCKED) {
            return false; // all is good, continue on the next iteration
        }

        finishBackup();

        if (rc == SQLITE_DONE) {
            log_v("Finished log DB backup");
            lastBackupTime = currentMillis;

            f = SD.open("/lastBackup", FILE_WRITE);
            f.print(backupFilename);
            f.close();

            return true;
        } else {
            log_w("Error generating log backup!");
            lastBackupTime += RETRY_TIME;
            return false;
        }
    }

    inline void Log::startChecksum() {
        log_v("Started logDB checksum");
        doingChecksum = true;

        f = SD.open(backupFilename);

        mbedtls_md_init(&ctx);
        mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 0);
        mbedtls_md_starts(&ctx);
    }

    inline void Log::processChecksum() {
        unsigned char buffer[512];
        int size = f.read(buffer, 512);
        mbedtls_md_update(&ctx, (const unsigned char *) buffer, size);

        if (f.available() <= 0) {
            finishChecksum();
        }
    }

    inline void Log::finishChecksum() {
        doingChecksum = false;
        log_v("Finished logDB checksum");

        byte shaResult[32];
        mbedtls_md_finish(&ctx, shaResult);
        mbedtls_md_free(&ctx);
        f.close();

        char calculatedHash[65];
        for (int i = 0; i < 32; ++i) {
            snprintf(calculatedHash + 2*i, 3,"%02hhx", shaResult[i]);
        }

        f = SD.open("/checksumBackup", FILE_WRITE);
        f.print(calculatedHash);
        f.close();

        log_i("Checksum: %s", calculatedHash);
    }


    int Log::openlogDB() {
        int rc = sqlite3_open(filename, &logdb);

        if (rc != SQLITE_OK) {
            log_e("Can't open database: %s", sqlite3_errmsg(logdb));
        } else {
            log_v("Opened database successfully");

            rc = sqlite3_prepare_v2(logdb,
                                    "INSERT INTO log(cardID, doorID, readerID, unixTimestamp, authorized) VALUES(?, ?, ?, ?, ?)",
                                    -1, &logquery, NULL);

            if (rc != SQLITE_OK) {
                log_e("Can't generate prepared statement for log DB: %s",
                              sqlite3_errmsg(logdb));
            } else {
                log_v("Prepared statement created for log DB");
            }
        }
        return rc;
    }

    void Log::generateLog(const char* readerID, unsigned long cardID,
                                 bool authorized, unsigned long time) {
        //TODO: create error column in db

        sqlite3_int64 card = cardID;
        sqlite3_int64 unixTime = time;

        sqlite3_reset(logquery);
        sqlite3_bind_int64(logquery, 1, card);
        sqlite3_bind_int(logquery, 2, doorID);
        sqlite3_bind_text(logquery, 3, readerID, -1, SQLITE_STATIC);
        sqlite3_bind_int64(logquery, 4, unixTime);
        sqlite3_bind_int(logquery, 5, authorized);

        int rc = sqlite3_step(logquery);
        while (rc == SQLITE_ROW) {
            rc = sqlite3_step(logquery);
        }

        if (rc != SQLITE_DONE) {
            log_e("Error querying DB: %s", sqlite3_errmsg(logdb));
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

    Log logging; // TODO: better name
}

void initLog() {
    LOGNS::logging.init();
}

void updateLogBackup(unsigned long time) {
    LOGNS::logging.updateBackup(time);
}

void generateLog(const char* readerID, unsigned long cardID,
                    bool authorized, unsigned long time) {
    LOGNS::logging.generateLog(readerID, cardID, authorized, time);
}
