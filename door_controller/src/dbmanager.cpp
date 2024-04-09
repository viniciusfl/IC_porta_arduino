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

        inline bool findPreferredDB();
        bool checkIf(const char *); // Check if file is valid/preferred
        const char *currentFileIsValid;
        const char *otherFileIsValid;
        const char *currentFileIsPreferred;
        const char *otherFileIsPreferred;

        // This swaps current <-- other, other <-- current and sets
        // "current" as preferred, but only if "other" is valid
        bool swapFiles();

        // openDB(); if that fails, revert files and try again
        void activateDBFile();

        // Something is wrong, delete the file
        inline void clearCurrentDBFile();

        File file;
    };  

    // This should be called from setup()
    inline void UpdateDBManager::init(bool diskOK) {
        this->diskOK = diskOK;
        if (not diskOK) { return; }

        currentFile = "/DB_A.db";
        otherFile = "/DB_B.db";
        currentFileIsValid = "/VALID_A.TXT";
        otherFileIsValid = "/VALID_B.TXT";
        currentFileIsPreferred = "/PREF_A.TXT";
        otherFileIsPreferred = "/PREF_B.TXT";

        if (findPreferredDB()) {
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

        if (DISK.exists(otherFileIsValid)) { DISK.remove(otherFileIsValid); };
        if (DISK.exists(otherFileIsPreferred))
                                        { DISK.remove(otherFileIsPreferred); };
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

        File f = DISK.open(otherFileIsValid, FILE_WRITE, true);
        f.print(1);
        f.close();

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

    void UpdateDBManager::activateDBFile() {
        log_d("Activating DB file");

        if (openDB(currentFile) == SQLITE_OK) { return; }

        closeDB();
        log_w("Error opening the updated DB, reverting to old one");
        clearCurrentDBFile();

        if (not swapFiles()) {
            log_w("Cannot revert to old DB, downloading a fresh file");
            forceDBDownload();
            return;
        }

        if (openDB(currentFile) == SQLITE_OK) { return; }

        closeDB();
        log_w("Reverting to old DB failed, downloading a fresh file");
        clearCurrentDBFile();
        forceDBDownload();
    }

    bool UpdateDBManager::swapFiles() {
        if (not checkIf(otherFileIsValid)) { return false; }

        const char *tmp = currentFile;
        currentFile = otherFile;
        otherFile = tmp;
        tmp = currentFileIsPreferred;
        currentFileIsPreferred = otherFileIsPreferred;
        otherFileIsPreferred = tmp;
        tmp = currentFileIsValid;
        currentFileIsValid = otherFileIsValid;
        otherFileIsValid = tmp;

        // If we crash before these 5 lines, on restart we will continue
        // using the old version of the DB; if we crash after, we will
        // use the new version of the DB. If we crash in the middle (with
        // both files containing "1"), on restart we will use "DB_A.db",
        // which may be either.

        File f = DISK.open(currentFileIsPreferred, FILE_WRITE, true);
        f.print(1);
        f.close();

        f = DISK.open(otherFileIsPreferred, FILE_WRITE, true);
        f.print(0);
        f.close();

        return true;
    }

    bool UpdateDBManager::checkIf(const char *filename) {
        // In some exceptional circumstances, we might end up writing
        // "1" to the file more than once; that's ok, 11 > 0 too :) .

        if (!DISK.exists(filename)) { return false; }
        File f = DISK.open(filename);
        if (!f) { return false; }

        int t = 0;
        if (f.available()) {
            t = f.parseInt(); // If reading fails, this returns 0
        }
        f.close();
        return t > 0;
    }

    inline bool UpdateDBManager::findPreferredDB() {
        if (not checkIf(currentFileIsValid)) { return swapFiles(); }

        if (not checkIf(otherFileIsValid)) { return true; }

        if (checkIf(currentFileIsPreferred)) {
            return true;
        } else {
            return swapFiles(); // always true
        }
    }

    inline void UpdateDBManager::clearCurrentDBFile() {
        if (DISK.exists(currentFile)) { DISK.remove(currentFile); };
        if (DISK.exists(currentFileIsPreferred))
                                        { DISK.remove(currentFileIsPreferred); };
        if (DISK.exists(currentFileIsValid))
                                        { DISK.remove(currentFileIsValid); };
    }


    UpdateDBManager updateDBManager;
}

void initDBMan(bool diskOK) { DBNS::updateDBManager.init(diskOK); }

ssize_t writeToDatabaseFile(const char* data, int data_len) {
    return DBNS::updateDBManager.writeToDatabaseFile(data, data_len);
}

void finishDBDownload() { return DBNS::updateDBManager.finishDBDownload(); }

void cancelDBDownload() { return DBNS::updateDBManager.cancelDBDownload(); }

bool wipeDBFiles() {
    log_i("Wiping all DB files");
    closeDB();
    if (not DISK.remove("/DB_A.db")) {
        log_w("Something wrong happened while removing DB file /DB_A.db");
        return false;
    }

    if (not DISK.remove("/DB_B.db")) {
        log_w("Something wrong happened while removing DB file /DB_B.db");
        return false;
    }

    if (not DISK.remove("/VALID_A.TXT")) {
        log_w("Something wrong happened while removing DB file /VALID_A.TXT");
        return false;
    }

    if (not DISK.remove("/VALID_B.TXT")) {
        log_w("Something wrong happened while removing DB file /VALID_B.TXT");
        return false;
    }

    if (not DISK.remove("/PREF_A.TXT")) {
        log_w("Something wrong happened while removing DB file /PREF_A.TXT");
        return false;
    }

    if (not DISK.remove("/PREF_B.TXT")) {
        log_w("Something wrong happened while removing DB file /PREF_B.TXT");
        return false;
    }

    return true;
}
