#ifndef DB_MANAGER_H
#define DB_MANAGER_H

// TODO: consider using the pimpl idiom to remove private items
//       from here, which includes sqlite3.h and CBAction.

#include <sqlite3.h>

// TODO: this is actually a hack, we should use
//       one specific callback for each thing
enum CBAction {
    CHECK_CARD,
    IGNORE
};

class DBManager{
    public:
        void init();
        void update();
        bool checkCard(int readerID, unsigned long cardID);

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
};

#endif
