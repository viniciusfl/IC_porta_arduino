static const char *TAG = "dbman";

#include <tramela.h>

#include <Arduino.h>

#ifdef USE_SD
#include "SPI.h"
#include "SD.h"
#else
#include "FFat.h"
#endif

#include <sqlite3.h> // Just to check for SQLITE_OK

#include <dbmanager.h>
#include <authorizer.h> // Notify that the DB changed with openDB/closeDB
#include <mqttmanager.h> // We may need to call "forceDBDownload"

namespace DBNS {

    // We should continue working normally even when we are downloading
    // a new DB file. So, we use this class to keep track of two files
    // with predefined filenames ("DB_A" and "DB_B"): one is the current
    // DB file and the other is a temporary file (either the previous
    // file in use or the file we are currently downloading). When
    // downloading finishes, we swap them, i.e., we call closeDB/openDB
    // to notify the Authorizer that the current DB has changed. So,
    // sometimes the current DB file is "DB_A", sometimes it is "DB_B".
    // During startup, we choose which of the files is the current one
    // by means of th "status" files.

    class UpdateDBManager {
    public:
        inline void init();

        inline bool startDBDownload(); // Open the "other" file for writing
        inline ssize_t writeToDatabaseFile(const char* data, int data_len);
        inline void finishDBDownload(); // Close downloaded file, swap DB file
        inline void cancelDBDownload();

    private:
        bool downloading;
        const char *currentFile;
        const char *otherFile;

        bool fileOK(const char *); // Is this file ok?
        inline bool findValidDB(); // Is there *any* OK file?
        // The answer, my friend, is blowing in the "status" files
        const char *currentFileStatus;
        const char *otherFileStatus;

        void chooseInitialFile(); // If necessary, force downloading
        void swapFiles(); // current <-- other, other <-- current
        inline void activateNewDBFile(); // closeDB, swapFiles, openDB
        void clearAllDBFiles(); // erase everything and start over

        File file;
    };  

    // This should be called from setup()
    inline void UpdateDBManager::init() {
        if (sdPresent) {
            chooseInitialFile();
        }
    }

    inline bool UpdateDBManager::startDBDownload() {
        log_d("Starting DB download");

        if (DISK.exists(otherFileStatus)) { DISK.remove(otherFileStatus); };
        if (DISK.exists(otherFile)) { DISK.remove(otherFile); };

        file = DISK.open(otherFile, FILE_WRITE, true);

        if(!file) {
            log_w("Error opening file, cancelling DB Download.");
            return false;
        }

        log_v("Writing to %s", otherFile);

        return true;
    }

    inline ssize_t UpdateDBManager::writeToDatabaseFile(const char* data,
                                                        int data_len) {

        if (!downloading) {
            if (!startDBDownload()) {
                // TODO: Do something smart here
                log_w("Cannot start download!");
                return -1;
            }
        }
        downloading = true;

        return file.write((byte *)data, data_len);
    }

    inline void UpdateDBManager::finishDBDownload() {
        downloading = false;
        file.close();
        log_d("Finished DB download");
        activateNewDBFile();
    }

    inline void UpdateDBManager::cancelDBDownload() {
        downloading = false;
        log_i("DB download cancelled");
        file.close();

        if (DISK.exists(otherFile)) { DISK.remove(otherFile); };
    }

    inline void UpdateDBManager::activateNewDBFile() {
        log_d("Activating newly downloaded DB file");

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
        tmp = currentFileStatus;
        currentFileStatus = otherFileStatus;
        otherFileStatus = tmp;

        // If we crash before these 5 lines, on restart we will continue
        // using the old version of the DB; if we crash after, we will
        // use the new version of the DB. If we crash in the middle (with
        // both files containing "1"), on restart we will use "DB_A.db",
        // which may be either.

        File f = DISK.open(currentFileStatus, FILE_WRITE, true);
        f.print(1);
        f.close();

        f = DISK.open(otherFileStatus, FILE_WRITE, true);
        f.print(0);
        f.close();
    }

    bool UpdateDBManager::fileOK(const char *tsfile) {
        // In some exceptional circumstances, we might end up writing
        // "1" to the file more than once; that's ok, 11 > 0 too :) .

        if (!DISK.exists(tsfile)) { return false; }
        File f = DISK.open(tsfile);
        if (!f) { return false; }

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
        currentFile = "/DB_A.db";
        otherFile = "/DB_B.db";
        currentFileStatus = "/STATUS_A.TXT";
        otherFileStatus = "/STATUS_B.TXT";

        bool success = false;

        while (!success) {
            if (!findValidDB()) {
                clearAllDBFiles();
                forceDBDownload();
            }

            while (!findValidDB()) {  checkDoor(); delay(100); currentMillis = millis();}

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
        if (fileOK(currentFileStatus)) {
            return true;
        } else if (fileOK(otherFileStatus)) {
            swapFiles();
            return true;
        }
        return false;
    }

    void UpdateDBManager::clearAllDBFiles() {
        if (DISK.exists(currentFile)) { DISK.remove(currentFile); };
        if (DISK.exists(otherFile)) { DISK.remove(otherFile); };
        if (DISK.exists(currentFileStatus)) { DISK.remove(currentFileStatus); };
        if (DISK.exists(otherFileStatus)) { DISK.remove(otherFileStatus); };

        File f = DISK.open(currentFileStatus, FILE_WRITE, true);
        f.print(0);
        f.close();

        f = DISK.open(otherFileStatus, FILE_WRITE, true);
        f.print(0);
        f.close();
    }

    UpdateDBManager updateDBManager;
}

void initDBMan() { DBNS::updateDBManager.init(); }

ssize_t writeToDatabaseFile(const char* data, int data_len) {
    return DBNS::updateDBManager.writeToDatabaseFile(data, data_len);
}

void finishDBDownload() { return DBNS::updateDBManager.finishDBDownload(); }

void cancelDBDownload() { return DBNS::updateDBManager.cancelDBDownload(); }
