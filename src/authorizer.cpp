static const char *TAG = "auth";

#include <tramela.h>

#include <Arduino.h>
#include <sqlite3.h>

// This is a wrapper around SQLite which allows us
// to query whether a user is authorized to enter.
class Authorizer {
    public:
        int openDB(const char *filename);
        inline void closeDB();
        inline bool userAuthorized(const char* readerID, unsigned long cardID);
    private:
        // check the comment near Authorizer::closeDB()
        sqlite3 *sqlitedb = NULL;
        sqlite3_stmt *dbquery = NULL;
};

int Authorizer::openDB(const char *filename) {
    closeDB();
    char name[50];
    snprintf(name, 50, "/sd%s", filename);

    int rc = sqlite3_open(name, &sqlitedb);
    if (rc != SQLITE_OK)
    {
        log_e("Can't open database: %s", sqlite3_errmsg(sqlitedb));
    } else {
        log_v("Opened database successfully %s", filename);
        rc = sqlite3_prepare_v2(sqlitedb,
                                "SELECT EXISTS(SELECT * FROM auth "
                                "WHERE userID=? AND doorID=?)",
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
inline bool Authorizer::userAuthorized(const char* readerID,
                                       unsigned long cardID) {

    // MASTER's ID is defined in tramela.h.
    // Maybe there is a better way to manage this.
    if (cardID == MASTER_KEY) {
        log_w("MASTER card used, openning door.");
        return true;
    }

    if (!sdPresent) {
        log_e("Cannot read SD, denying access");
        return false;
    }

    if (sqlitedb == NULL) {
        log_e("Cannot read DB, denying access");
        return false;
    }

    log_v("Card reader %s was used. Received card ID %lu", readerID, cardID);

    sqlite3_int64 card = cardID;
    sqlite3_reset(dbquery);
    sqlite3_bind_int64(dbquery, 1, card);
    sqlite3_bind_int(dbquery, 2, doorID);

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

    return authorized;
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

Authorizer authorizer;


int openDB(const char* filename) { return authorizer.openDB(filename); }

void closeDB() { authorizer.closeDB(); }

bool userAuthorized(const char* readerID, unsigned long cardID) {
    return authorizer.userAuthorized(readerID, cardID);
}