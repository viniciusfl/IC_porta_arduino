static const char *TAG = "authorizer";

#include <common.h>
#include <Arduino.h>
#include <sqlite3.h>

namespace DBNS {
    // This is a wrapper around SQLite which allows us
    // to query whether a user is authorized to enter.
    class Authorizer {
    public:
        inline void init();
        int openDB(const char *filename);
        inline void closeDB();
        inline bool userAuthorized(const char* readerID, unsigned long cardID);

    private:
        sqlite3 *sqlitedb;
        sqlite3_stmt *dbquery;
        sqlite3 *sqlitelog;
        sqlite3_stmt *logquery;
        void generateLog(unsigned long cardID, const char* readerID,
                         bool authorized);
        int openlogDB();
        inline void closelogDB();
    };


    // This should be called from setup()
    inline void Authorizer::init() {
        sqlitedb = NULL; // check the comment near Authorizer::closeDB()
        dbquery = NULL;
        sqlitelog = NULL;
        logquery = NULL;
        sqlite3_initialize();
    }

    int Authorizer::openDB(const char *filename) {
        closeDB();
        String name = (String) "/sd" + filename; // FIXME: 

        int rc = sqlite3_open(name.c_str(), &sqlitedb);
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

    int Authorizer::openlogDB() {
        const char* filename = "/sd/log.db"; // FIXME: 
        int rc = sqlite3_open(filename, &sqlitelog);

        if (rc != SQLITE_OK) {
            log_e("Can't open database: %s", sqlite3_errmsg(sqlitelog));
        } else {
            log_v("Opened database successfully");

            // prepare query
            rc = sqlite3_prepare_v2(sqlitelog,
                                    "INSERT INTO log(cardID, doorID, readerID, unixTimestamp, authorized) VALUES(?, ?, ?, ?, ?)",
                                    -1, &logquery, NULL);

            if (rc != SQLITE_OK) {
                log_e("Can't generate prepared statement for log DB: %s",
                              sqlite3_errmsg(sqlitelog));
            } else {
                log_v("Prepared statement created for log DB");
            }
        }
        return rc;
    }


    // search element through current database
    inline bool Authorizer::userAuthorized(const char* readerID,
                                    unsigned long cardID) {

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

        generateLog(cardID, readerID, authorized);

        if (authorized) {
            return true;
        } else {
            return false;
        }
    }

    void Authorizer::generateLog(unsigned long cardID, const char* readerID,
                                 bool authorized) {

        //TODO: create error column in db 

        // get unix time
        time_t now;
        time(&now);
        unsigned long systemtime = now;

        openlogDB();

        // should i verify errors while binding? 

        sqlite3_int64 card = cardID;
        sqlite3_int64 unixTime = systemtime;

        sqlite3_reset(logquery);
        sqlite3_bind_int64(logquery, 1, card);
        sqlite3_bind_int(logquery, 2, doorID); 
        sqlite3_bind_text(logquery, 3, readerID, -1, SQLITE_STATIC);
        sqlite3_bind_int64(logquery, 4, unixTime); 
        sqlite3_bind_int(logquery, 5, authorized); 
        
        int rc = sqlite3_step(dbquery);
        while (rc == SQLITE_ROW) {
            rc = sqlite3_step(logquery);
        }

        if (rc != SQLITE_DONE) {
            log_e("Error querying DB: %s", sqlite3_errmsg(sqlitelog));
        }
        closelogDB();
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

    inline void Authorizer::closelogDB(){
        sqlite3_finalize(logquery);
        logquery = NULL;
        sqlite3_close_v2(sqlitelog);
        sqlitelog = NULL;
    }

    Authorizer authorizer;
}


void initDB() {
    DBNS::authorizer.init();
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
