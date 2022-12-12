#ifndef DB_MAITENANCE
#define DB_MAITENANCE

#include <WiFi.h>
#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include <sqlite3.h>
#include <networkConnection.h>
#include <common.h>
#include <timeKeeping.h>

class dataBase{
    public:
        void init();
        void dbMaintenance(DateTime moment);

    private:
        sqlite3 *db;
        const char *dbNames[100] = {"bancoA", "bancoB"};
        const char *timestampfiles[100] = {"/TSA.TXT", "/TSB.TXT"};
        int currentDB = -1; // invalid
        int newDB = -1;
        unsigned long lastDownloadTime = 0;
        bool headerDone = false;
        bool beginningOfLine = true;
        char netLineBuffer[11];
        char position = 0;
        char previous;
        char *zErrMsg = 0;
        const char* data = "Callback function called";

        void startDownload();
        void finishDownload();
        void processDownload();
        void chooseCurrentDB();
        void resetTimestampFiles();
        void generateLog(DateTime moment, unsigned long int id);
        int openDb(const char *filename) ;
        void close();
        int exec(const char *sql);
        void insert(char *element);
        bool search(DateTime moment);
};

#endif
