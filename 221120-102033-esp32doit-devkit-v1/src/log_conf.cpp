static const char *TAG = "log";

#include <common.h>
#include <Arduino.h>
#include <sqlite3.h>

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
            inline bool backupEnded();
            inline void processBackup();
            const char* filename = "/sd/log.db"; // FIXME: hardcoded?

            unsigned long lastBackupTime = 0; // TODO: remove after implement alarm
            bool doingBackup = false;
            int rc;
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
        sqlite3_initialize();

        // Do not change this! Instead, define the desired level in log_conf.h
        esp_log_level_set("*", ESP_LOG_VERBOSE);

        // Log messages will be processed by the function defined above
        esp_log_set_vprintf(logmessage);

        // This next section im not quite sure. 
        // Should
    }

    void Log::updateBackup(unsigned long time) {
        if (!doingBackup) {
            // TODO: Change to alarm
            if (currentMillis - lastBackupTime > BACKUP_INTERVAL) {
                startBackup(time);
                
            }
            return;
        }

        if (backupEnded()) {
            finishBackup();
            return;
        }

        processBackup();
    }

    inline void Log::startBackup(unsigned long time) {
        log_v("Started log DB backup");

        doingBackup = true;

        rc = sqlite3_open(filename, &sqlitelog);

        if (!rc == SQLITE_OK){
            log_e("Aborting update... Couldn't open log db: %s", sqlite3_errmsg(sqlitebackup));

            doingBackup = false;
            lastBackupTime += RETRY_TIME;
            return;
        }

        char buffer[50];
        sprintf(buffer, "/sd/%lu.db", time);

        rc = sqlite3_open_v2(buffer, &sqlitebackup, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);

        if (!rc == SQLITE_OK){
            log_e("Aborting update... Couldn't open backup db: %s", sqlite3_errmsg(sqlitebackup));

            doingBackup = false;
            lastBackupTime += RETRY_TIME;
            sqlite3_close(sqlitelog);
            return;
        }

        logBackup = sqlite3_backup_init(sqlitebackup, "main", sqlitelog, "main");

        if (!logBackup) {
            log_v("Problem with backup init");
            
            doingBackup = false;
            lastBackupTime += RETRY_TIME;

            sqlite3_close(sqlitelog);
            sqlite3_close(sqlitebackup);

            return;
        }
    }

    inline void Log::finishBackup() {
        (void)sqlite3_backup_finish(logBackup);
        (void)sqlite3_close(sqlitebackup);

        log_e("Finished log DB backup");
        doingBackup = false;

        sqlite3_close(sqlitelog);
        sqlite3_close(sqlitebackup);

        lastBackupTime = currentMillis;
    }

    inline void Log::processBackup() {
        log_e("Processing...");
        rc = sqlite3_backup_step(logBackup, 5);
    }

    inline bool Log::backupEnded() {
        if (rc == SQLITE_OK || rc == SQLITE_BUSY || rc == SQLITE_LOCKED) 
            return false;
        return true;
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