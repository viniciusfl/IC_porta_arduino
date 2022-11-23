#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include <sqlite3.h>
#include <networkConnection.h>
#include <common.h>

class dataBase{
    public:
        dataBase();
        void initDataBase();
        void dbMaintenance();
        void startDownload();
        void finishDownload();
        void processDownload();
        void dbStartSearch(uint8_t* data, uint8_t bits, const char* message);
        void chooseCurrentDB();
        void resetTimestampFiles();
        int openDb(const char *filename) ;
        int exec(const char *sql);
        void insert(char *element);
        void close();
        bool search();
        sqlite3 *db;
        File arquivo;
        unsigned long input; 
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
};

static int callback(void *data, int argc, char **argv, char **azColName);