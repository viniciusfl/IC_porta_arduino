static const char *TAG = "authorizer";

#include <common.h>
#include <Arduino.h>
#include <sqlite3.h>

namespace DBNS {
    // This is a wrapper around SQLite which allows us
    // to query whether a user is authorized to enter.
    class Authorizer {
    public:
        int openDB(const char *filename);
        inline void closeDB();
        inline bool userAuthorized(const char* readerID, unsigned long cardID);
        inline bool openDoor(); // TODO: I added something like this to
                                //       cardreader.cpp, let's think about this
    private:
        // check the comment near Authorizer::closeDB()
        sqlite3 *sqlitedb = NULL;
        sqlite3_stmt *dbquery = NULL;
    };

    int Authorizer::openDB(const char *filename) {
        closeDB();
        char name[50];
        sprintf(name, "/sd%s", filename);

        int rc = sqlite3_open(name, &sqlitedb);
        if (rc != SQLITE_OK)
        {
            log_e("Can't open database: %s", sqlite3_errmsg(sqlitedb));
        } else {

            log_v("Opened database successfully %s", filename);
            rc = sqlite3_prepare_v2(sqlitedb,
                                    "SELECT EXISTS(SELECT * FROM auth WHERE userID=? AND doorID=?)",
                                    -1, &dbquery, NULL);

            if (rc != SQLITE_OK) {
                log_e("Can't generate prepared statement: %s: %s",
                      sqlite3_errstr(sqlite3_extended_errcode(sqlitedb)),
                      sqlite3_errmsg(sqlitedb));
            } else {
                log_v("Prepared statement created");
            }
        }

        return rc;
    }

    // search element through current database
    inline bool Authorizer::userAuthorized(const char* readerID, unsigned long cardID) {

        if (sqlitedb == NULL)
            return false;

        log_v("Card reader %s was used. Received card ID %lu",
              readerID, cardID);

        sqlite3_int64 card = cardID;
        sqlite3_reset(dbquery);
        sqlite3_bind_int64(dbquery, 1, card);
        sqlite3_bind_int(dbquery, 2, doorID); 

        // should i verify errors while binding?

        bool authorized = false;
        int rc = sqlite3_step(dbquery);
        while (rc == SQLITE_ROW) {
            if (1 == sqlite3_column_int(dbquery, 0))
                authorized = true;
            rc = sqlite3_step(dbquery);
        }

        if (rc != SQLITE_DONE) {
            log_e("Error querying DB: %s", sqlite3_errmsg(sqlitedb));
        }

        if (authorized) {
            return true;
        } else {
            return false;
        }
    }


    // The sqlite3 docs say "The C parameter to sqlite3_close(C) and
    // sqlite3_close_v2(C) must be either a NULL pointer or an sqlite3
    // object pointer [...] and not previously closed". So, we always
    // make it NULL here to avoid closing a pointer previously closed.
    inline void Authorizer::closeDB() {
        sqlite3_finalize(dbquery);
        dbquery = NULL;
        sqlite3_close_v2(sqlitedb);
        sqlitedb = NULL;
    }

    inline bool Authorizer::openDoor() {
        log_v("Opened door...");
        return true;
    }

    Authorizer authorizer;
}


int openDB(const char* filename) {
    return DBNS::authorizer.openDB(filename);
}

void closeDB() {
    DBNS::authorizer.closeDB();
}

bool userAuthorized(const char* readerID, unsigned long cardID) {
    return DBNS::authorizer.userAuthorized(readerID, cardID);
}

bool openDoor() {
    return DBNS::authorizer.openDoor();
}
