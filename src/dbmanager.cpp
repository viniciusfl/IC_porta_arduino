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
    // by means of the "status" files.

    class UpdateDBManager {
    public:
        inline void init(bool diskOK);

        inline bool startDBDownload(); // Open the "other" file for writing
        inline ssize_t writeToDatabaseFile(const char* data, int data_len);
        inline void finishDBDownload(); // Close downloaded file, swap DB file
        inline void cancelDBDownload();

    private:
        bool diskOK;
        bool downloading;
        const char *currentFile;
        const char *otherFile;

        bool fileOK(const char *); // Is this file ok?
        inline bool findValidDB(); // Is there *any* OK file?
        // The answer, my friend, is blowing in the "status" files
        const char *currentFileStatus;
        const char *otherFileStatus;

        void swapFiles(); // current <-- other, other <-- current
        inline void activateDBFile(); // closeDB, swapFiles, openDB
        inline void clearCurrentDBFile(); // Something is wrong with it, delete
        inline void clearAllDBFiles(); // erase everything and start over

        File file;
    };  

    // This should be called from setup()
    inline void UpdateDBManager::init(bool diskOK) {
        this->diskOK = diskOK;
        if (not diskOK) { return; }

        currentFile = "/DB_A.db";
        otherFile = "/DB_B.db";
        currentFileStatus = "/STATUS_A.TXT";
        otherFileStatus = "/STATUS_B.TXT";

        if (findValidDB()) {
            activateDBFile();
        } else {
            log_e("No valid DB file, downloading a fresh one");
            forceDBDownload();  // When this succeeds, the DB is activated
        }
    }

    inline bool UpdateDBManager::startDBDownload() {
        if (not diskOK) {
            log_d("Ignoring attempt to download DB -- no disk available");
            return true;
        }
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

        if (not diskOK) { return data_len; }

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
        if (not diskOK) { return; }

        if (not downloading) { return; }

        downloading = false;
        file.close();
        log_d("Finished DB download");

        closeDB();
        swapFiles();
        activateDBFile();
    }

    inline void UpdateDBManager::cancelDBDownload() {
        if (not diskOK) { return; }

        if (not downloading) { return; }

        downloading = false;
        log_i("DB download cancelled");
        file.close();

        if (DISK.exists(otherFile)) { DISK.remove(otherFile); };
    }

    inline void UpdateDBManager::activateDBFile() {
        log_d("Activating DB file");

        if (openDB(currentFile) != SQLITE_OK) {
            log_w("Error opening the updated DB, reverting to old one");
            closeDB();
            clearCurrentDBFile();
            swapFiles();
            if (openDB(currentFile) != SQLITE_OK) {
                log_w("Reverting to old DB failed, downloading a fresh file");
                closeDB();
                clearAllDBFiles();
                forceDBDownload();
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

    inline bool UpdateDBManager::findValidDB() {
        if (fileOK(currentFileStatus)) {
            return true;
        } else if (fileOK(otherFileStatus)) {
            swapFiles();
            return true;
        }
        return false;
    }

    inline void UpdateDBManager::clearCurrentDBFile() {
        if (DISK.exists(currentFile)) { DISK.remove(currentFile); };
        if (DISK.exists(currentFileStatus)) { DISK.remove(currentFileStatus); };
    }

    inline void UpdateDBManager::clearAllDBFiles() {
        clearCurrentDBFile();
        if (DISK.exists(otherFile)) { DISK.remove(otherFile); };
        if (DISK.exists(otherFileStatus)) { DISK.remove(otherFileStatus); };
    }

    UpdateDBManager updateDBManager;
}

void initDBMan(bool diskOK) { DBNS::updateDBManager.init(diskOK); }

ssize_t writeToDatabaseFile(const char* data, int data_len) {
    return DBNS::updateDBManager.writeToDatabaseFile(data, data_len);
}

void finishDBDownload() { return DBNS::updateDBManager.finishDBDownload(); }

void cancelDBDownload() { return DBNS::updateDBManager.cancelDBDownload(); }
