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
        void update();
        inline void writeToDatabaseFile(const char* data, int data_len);
    private:
        const char *currentFile;
        const char *otherFile;
        const char *currentTimestampFile;
        const char *otherTimestampFile;

        inline void chooseInitialFile();
        void swapFiles();
        bool checkFileFreshness(const char *);

        unsigned long lastDownloadTime = 0;

        bool downloadingDB = false;
        void startDBDownload();
        inline void finishDBDownload();
        inline bool activateNewDBFile();

        File file;
    };  

    // This should be called from setup()
    inline void UpdateDBManager::init() {
        if (sdPresent) {
            chooseInitialFile();
        }
    }

    // This should be called from loop()
    // At each call, we determine the current state we are in, perform
    // a small chunk of work, and return. This means we do not hog the
    // processor and can pursue other tasks while updating the DB.
    void UpdateDBManager::update() {
        if (!sdPresent || !connected() || !isClientConnected())
            return;
        // We start a download only if we are not already downloading
        if (!downloadingDB) {
            // millis() wraps every ~49 days, but
            // wrapping does not cause problems here
            // TODO: we might prefer to use dates,
            //       such as "at midnight"
            //
            // ANWSER: we can use an alarm with RTC 1307
            // https://robojax.com/learn/arduino/?vid=robojax_DS1307-clock-alarm
            // I believe the nodeMCU RTC might also have this feature

            if (currentMillis - lastDownloadTime > DOWNLOAD_INTERVAL) {
                startDBDownload();
            }
            return;
        }
        // If we did not return above, we are still downloading the DB

        // If we finished downloading the DB, check whether the
        // download was successful; if so and the hash matches,
        // start using the new DB
        if (didDownloadFinish()) {
            finishDBDownload();
            // Both downloads successful, update the timestamp
            if (activateNewDBFile()) {
                lastDownloadTime = currentMillis;
            }
        }
        // still downloading the DB

    }

    void UpdateDBManager::startDBDownload() {
        if (!sdPresent) 
            return;

        log_v("Starting DB download");

        if (!connected() || !isClientConnected()) {
            lastDownloadTime += RETRY_DOWNLOAD_TIME;
            log_i("Internet failure, cancelling DB download");
            return;
        }

        SD.remove(otherTimestampFile);
        SD.remove(otherFile);

        file = SD.open(otherFile, FILE_WRITE);
        if(!file) {
            log_e("Error openning file, cancelling DB Download.");
            return;
        }
        log_v("Writing to %s", otherFile);

        if (!startDownload()) {
            file.close();
            return;
        }

        downloadingDB = true;

        log_v("Started DB download");
        return;
    }

    inline void UpdateDBManager::finishDBDownload() {
        file.close();
        downloadingDB = false;
        log_v("Finished DB download");
    }

    inline void UpdateDBManager::writeToDatabaseFile(const char* data, int data_len) { 
            // NOTE (maybe FIXME): When downloading retained messages, just the first
            // block of data comes with "topic", and the others blocks have empty topic.    
            file.write((byte *)data, data_len);
    }

    inline bool UpdateDBManager::activateNewDBFile() {
        bool ok = true;
        swapFiles();
        closeDB();

        if (openDB(currentFile) != SQLITE_OK) {
            ok = false;
            log_w("Error opening the updated DB, reverting to old one");
            swapFiles();
            if (openDB(currentFile) != SQLITE_OK) {
                // FIXME: in the unlikely event that this fails too, we are doomed
                chooseInitialFile();
            }
        }
    
        if (!ok) {
            lastDownloadTime += RETRY_DOWNLOAD_TIME;
        }

        downloadingDB = false;
        return ok;
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
        file = SD.open(currentTimestampFile, FILE_WRITE);
        file.print(1);
        file.close();

        file = SD.open(otherTimestampFile, FILE_WRITE);
        file.print(0);
        file.close();
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

    inline void UpdateDBManager::chooseInitialFile() {
        if (!sdPresent)
            return;

        START_OF_FUNCTION: // We will use goto further below
        currentFile = "/bancoA.db";
        otherFile = "/bancoB.db";
        currentTimestampFile = "/TSA.TXT";
        otherTimestampFile = "/TSB.TXT";
        bool dbFileOK = false;

        while (!dbFileOK) {
            if (checkFileFreshness(currentTimestampFile)) {
                dbFileOK = true;
            } else if (checkFileFreshness(otherTimestampFile)) {
                swapFiles();
                dbFileOK = true;
            } else {
                if (downloadingDB) {
                    update();
                } else if (connected() && isClientConnected()) {
                    startDBDownload();
                }
            }
        }

        // If we were forced to download a new file above, it was already
        // opened by update(), but there is no harm in doing it here again
        log_d("Choosing %s as current DB.", currentFile);
        if (openDB(currentFile) != SQLITE_OK) {
            log_e("Something bad happen with the DB file! "
                  "Downloading a fresh one");

            // start over
            closeDB();
            SD.remove(currentFile);
            SD.remove(otherFile);
            SD.remove(currentTimestampFile);
            SD.remove(otherTimestampFile);
            goto START_OF_FUNCTION;
        }
    }

    UpdateDBManager updateDBManager;
}

void initDBMan() { DBNS::updateDBManager.init(); }

void updateDB() { DBNS::updateDBManager.update(); }

void writeToDatabaseFile(const char* data, int data_len) { DBNS::updateDBManager.writeToDatabaseFile(data, data_len); }
