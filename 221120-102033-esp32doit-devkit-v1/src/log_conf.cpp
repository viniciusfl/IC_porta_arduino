static const char *TAG = "log";

#include <common.h>
#include <Arduino.h>
#include <sqlite3.h>
#include <SD.h>
#include "esp_tls.h"

#define BACKUP_INTERVAL 60000

#define RETRY_TIME 10000
namespace DBNS {
    class Log { // TODO: better name
        public:
            inline void initLog();
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
            unsigned long logCreationTime;

            inline void startChecksum();
            inline void processChecksum();
            inline void finishChecksum();
            bool doingChecksum;
            unsigned char buffer[512];
            byte shaResult[32];
            mbedtls_md_context_t ctx;
            mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;

            File f;
            sqlite3_backup *logBackup;
            sqlite3 *sqlitebackup;

            int openlogDB();
            inline void closelogDB();
            sqlite3 *sqlitelog;
            sqlite3_stmt *logquery;

        };
    int logmessage(const char* format, va_list ap);

    inline void Log::initLog() {
        sqlitelog = NULL;
        logquery = NULL;

        int rc = openlogDB();
        if (!rc == SQLITE_OK){
            log_e("Couldn't open log db: %s, aborting...", sqlite3_errmsg(sqlitebackup));
            while (true) delay(10);
        }
        log_e("Openned log db");

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
                finishBackup();
                startChecksum();
            }
            return;
        }

        if (f.available() <= 0) {
            finishChecksum();
            return;
        }
        processChecksum();
    }

    inline void Log::startBackup(unsigned long time) {
        log_v("Started log DB backup");

        doingBackup = true;
        logCreationTime = time;
        char buffer[50];
        sprintf(buffer, "/sd/%lu.db", logCreationTime);

        int rc = sqlite3_open_v2(buffer, &sqlitebackup, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);

        if (!rc == SQLITE_OK){
            log_e("Aborting update... Couldn't open backup db: %s", sqlite3_errmsg(sqlitebackup));

            doingBackup = false;
            lastBackupTime += RETRY_TIME;
            return;
        }

        logBackup = sqlite3_backup_init(sqlitebackup, "main", sqlitelog, "main");

        if (!logBackup) {
            log_v("Problem with backup init");

            doingBackup = false;
            lastBackupTime += RETRY_TIME;

            sqlite3_close(sqlitebackup);

            return;
        }
    }

    inline void Log::finishBackup() {
        (void)sqlite3_backup_finish(logBackup);
        (void)sqlite3_close(sqlitebackup);

        log_e("Finished log DB backup");
        doingBackup = false;

        sqlite3_close(sqlitebackup);

        f = SD.open("/lastBackup", FILE_WRITE);
        char buffer[50];
        sprintf(buffer, "%lu", logCreationTime);
        f.print(buffer);
        f.close();
    }

    inline bool Log::processBackup() {
        int rc = sqlite3_backup_step(logBackup, 5);

        if (rc == SQLITE_OK || rc == SQLITE_BUSY || rc == SQLITE_LOCKED) {
            return false; // all is good, continue on the next iteration
        }

        if (rc == SQLITE_DONE) {
            log_v("Finished log DB backup");
            lastBackupTime = currentMillis;
        } else {
            log_w("Error generating log backup!");
            lastBackupTime += RETRY_TIME;
        }

        return true;
    }

    inline void Log::startChecksum() {
        log_v("Started logDB checksum");
        doingChecksum = true;

        char buffer[50];
        sprintf(buffer, "/%lu.db", logCreationTime);

        f = SD.open(buffer);

        mbedtls_md_init(&ctx);
        mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 0);
        mbedtls_md_starts(&ctx);
    }

    inline void Log::processChecksum() {
        int size = f.read(buffer, 512);
        mbedtls_md_update(&ctx, (const unsigned char *) buffer, size);
    }

    inline void Log::finishChecksum() {
        doingChecksum = false;
        log_e("Finished logDB checksum");
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

        log_e("Checksum: %s", calculatedHash);
    }


    int Log::openlogDB() {
        int rc = sqlite3_open(filename, &sqlitelog);

        if (rc != SQLITE_OK) {
            log_e("Can't open database: %s", sqlite3_errmsg(sqlitelog));
        } else {
            log_v("Opened database successfully");

            rc = sqlite3_prepare_v2(sqlitelog,
                                    "INSERT INTO log(cardID, doorID, readerID, unixTimestamp, authorized) VALUES(?, ?, ?, ?, ?)",
                                    -1, &logquery, NULL);

            if (rc != SQLITE_OK) {
                log_e("Can't generate prepared statement for log DB: %s",
                              sqlite3_errmsg(sqlitelog));
            } else {
                log_v("Prepared statement created for log DB");
            }
        }
        return rc;
    }

    inline void Log::closelogDB(){
        sqlite3_finalize(logquery);
        logquery = NULL;
    }


    void Log::generateLog(const char* readerID, unsigned long cardID, 
                                 bool authorized, unsigned long time) {
        //TODO: create error column in db 
        openlogDB();

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
            log_e("Error querying DB: %s", sqlite3_errmsg(sqlitelog));
        }

        closelogDB();
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
    DBNS::logging.initLog(); 
}

void updateLogBackup(unsigned long time) { 
    DBNS::logging.updateBackup(time); 
}

void generateLog(const char* readerID, unsigned long cardID, 
                    bool authorized, unsigned long time) {
    DBNS::logging.generateLog(readerID, cardID, authorized, time);
}
