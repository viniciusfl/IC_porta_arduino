#ifndef DB_MAINTENANCE_H
#define DB_MAINTENANCE_H

#include <sqlite3.h>

enum CBAction {
    CHECK_CARD,
    IGNORE
};

class dataBase{
    public:
        void init();
        void update();
        void checkCurrentCard();

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

        void startDownload();
        void finishDownload();
        void processDownload();
        void chooseInitialDB();
        void resetTimestampFiles();
        void generateLog(unsigned long int id);
        int openDB();
        void closeDB();
        int exec(const char *sql, CBAction);
        void insert(char *element);
        bool search();
};

#endif
