static const char *TAG = "dbman";

#include <common.h>

#include <mqtt_client.h>

#include "SPI.h"
#include "SD.h"
#include <sqlite3.h> // Just to check for SQLITE_OK

#ifdef USE_SOCKETS
#include <WiFi.h>
#else
#include "esp_tls.h"
#endif

#include <networkmanager.h>
#include <authorizer.h>
#include <dbmanager.h>
#include <cardreader.h>
#include <mqttmanager.h>

#define RETRY_DOWNLOAD_TIME 60000

#define DOWNLOAD_INTERVAL 2400000

namespace DBNS {
    // This class periodically downloads a new version of the database
    // and sets this new version as the "active" db file in the Authorizer
    // when downloading is successful. It alternates between two filenames
    // to do its thing and, during startup, identifies which of them is
    // the current one by means of the "timestamp" file.
    class UpdateDBManager {
    public:
        inline void init();
        inline bool startDBDownload();
        inline ssize_t writeToDatabaseFile(const char* data, int data_len);
        inline void finishDBDownload();
        inline void cancelDBDownload();
    private:
        const char *currentFile;
        const char *otherFile;
        const char *currentTimestampFile;
        const char *otherTimestampFile;

        void chooseInitialFile();
        void swapFiles();
        bool checkFileFreshness(const char *);
        void clearAllDBFiles();
        inline void activateNewDBFile();
        inline bool findValidDB();

        File file;
    };  

    // This should be called from setup()
    inline void UpdateDBManager::init() {
        currentFile = "/bancoA.db";
        otherFile = "/bancoB.db";
        currentTimestampFile = "/TSA.TXT";
        otherTimestampFile = "/TSB.TXT";

        if (sdPresent) {
            chooseInitialFile();
        }
    }

    inline bool UpdateDBManager::startDBDownload() {
        log_v("Starting DB download");

        SD.remove(otherTimestampFile);
        SD.remove(otherFile);

        file = SD.open(otherFile, FILE_WRITE);
        if(!file) {
            log_e("Error opening file, cancelling DB Download.");
            return false;
        }

        log_v("Writing to %s", otherFile);

        return true;
    }

    inline ssize_t UpdateDBManager::writeToDatabaseFile(const char* data,
                                                        int data_len) {

        // NOTE (maybe FIXME): When downloading retained messages, just
        // the first block of data comes with "topic", and the other
        // blocks have empty topic.
        return file.write((byte *)data, data_len);
    }

    inline void UpdateDBManager::finishDBDownload() {
        file.close();
        log_v("Finished DB download");
        activateNewDBFile();
    }

    inline void UpdateDBManager::cancelDBDownload() {
        file.close();
        SD.remove(otherFile);
    }

    inline void UpdateDBManager::activateNewDBFile() {
        log_w("Activating newly downloaded DB file");

        closeDB();
        swapFiles();

        if (openDB(currentFile) != SQLITE_OK) {
            log_w("Error opening the updated DB, reverting to old one");
            closeDB();
            swapFiles();
            if (openDB(currentFile) != SQLITE_OK) {
                log_w("Reverting to old DB failed, downloading a fresh file");
                closeDB();
                clearAllDBFiles();
                chooseInitialFile();
            }
        }
    }

    void UpdateDBManager::swapFiles() {
        const char *tmp = currentFile;
        currentFile = otherFile;
        otherFile = tmp;
        tmp = currentTimestampFile;
        currentTimestampFile = otherTimestampFile;
        otherTimestampFile = tmp;

        // If we crash before these 5 lines, on restart we will continue
        // using the old version of the DB; if we crash after, we will
        // use the new version of the DB. If we crash in the middle (with
        // both files containing "1"), on restart we will use "bancoA.db",
        // which may be either.
        File f = SD.open(currentTimestampFile, FILE_WRITE);
        f.print(1);
        f.close();

        f = SD.open(otherTimestampFile, FILE_WRITE);
        f.print(0);
        f.close();
    }

    bool UpdateDBManager::checkFileFreshness(const char *tsfile) {
        // In some exceptional circumstances, we might end up writing
        // "1" to the file more than once; that's ok, 11 > 0 too :) .
        File f = SD.open(tsfile);

        if (!f) return false;

        int t = 0;
        if (f.available()) {
            t = f.parseInt(); // If reading fails, this returns 0
        }
        f.close();
        return t > 0;
    }

    void UpdateDBManager::chooseInitialFile() {
        if (!sdPresent)
            return;

        bool success = false;

        while (!success) {
            if (!findValidDB()) {
                clearAllDBFiles();
                forceDBDownload();
            }

            while (!findValidDB()) { delay(1000); }

            log_d("Choosing %s as current DB.", currentFile);
            if (openDB(currentFile) != SQLITE_OK) {
                closeDB();
                log_e("Something bad happen with the DB file! "
                      "Downloading a fresh one");
                clearAllDBFiles();
            } else {
                success = true;
            }
        }
    }

    inline bool UpdateDBManager::findValidDB() {
        if (checkFileFreshness(currentTimestampFile)) {
            return true;
        } else if (checkFileFreshness(otherTimestampFile)) {
            swapFiles();
            return true;
        }
        return false;
    }

    void UpdateDBManager::clearAllDBFiles() {
        SD.remove(currentFile);
        SD.remove(otherFile);
        SD.remove(currentTimestampFile);
        SD.remove(otherTimestampFile);

        File f = SD.open(currentTimestampFile, FILE_WRITE);
        f.print(0);
        f.close();
        f = SD.open(otherTimestampFile, FILE_WRITE);
        f.print(0);
        f.close();
    }

    UpdateDBManager updateDBManager;
}

void initDBMan() { DBNS::updateDBManager.init(); }

bool startDBDownload() { return DBNS::updateDBManager.startDBDownload(); }

ssize_t writeToDatabaseFile(const char* data, int data_len) {
    return DBNS::updateDBManager.writeToDatabaseFile(data, data_len);
}

void finishDBDownload() { return DBNS::updateDBManager.finishDBDownload(); }

void cancelDBDownload() { return DBNS::updateDBManager.cancelDBDownload(); }
