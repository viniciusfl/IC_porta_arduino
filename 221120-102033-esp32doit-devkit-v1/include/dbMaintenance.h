#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include <sqlite3.h>
#include <networkConnection.h>
#include <common.h>

class dataBase{
    public:
        dataBase();
        inline void dbMaintenance();
        inline void startDownload();
        inline void finishDownload();
        inline void processDownload();
        inline void dbStartSearch(uint8_t* data, uint8_t bits, const char* message);
        char chooseCurrentDB();
        sqlite3 *db;
        File arquivo;
        const char dbNames[10][10] = {"bancoA", "bancoB"};
        const char timestampfiles[10][10] = {"/TSA.TXT", "/TSB.TXT"};
        char currentDB = -1; // invalid
        char newDB = -1;
        unsigned long lastDownloadTime = 0;
        bool headerDone = false;
        bool beginningOfLine = true;
        char netLineBuffer[11];
        char position = 0;
        char previous;
        inline void resetTimestampFiles();
        int openDb(const char *filename) ;
        int exec(const char *sql);
        void insert(char *element);
        void close();
        char *zErrMsg = 0;
        const char* data = "Callback function called";
};

static int callback(void *data, int argc, char **argv, char **azColName);