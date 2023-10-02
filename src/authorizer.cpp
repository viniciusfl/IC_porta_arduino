static const char *TAG = "auth";

#include <tramela.h>
#include "mbedtls/md.h"
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
        inline char* encrypt(int cardID);
        sqlite3 *sqlitedb = NULL;
        sqlite3_stmt *dbquery = NULL;
};

int Authorizer::openDB(const char *filename) {
    closeDB();
    char name[50];
#   ifdef USE_SD
    snprintf(name, 50, "/sd%s", filename);
#   else
    snprintf(name, 50, "/ffat%s", filename);
#   endif

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
    for (int i = 0; i < sizeof(master_keys)/sizeof(int); i++) {
        if (cardID == master_keys[i]) {
            log_w("MASTER card used, openning door.");
            return true;
        }
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

    char* key = encrypt(cardID);
    sqlite3_reset(dbquery);
    sqlite3_bind_text(dbquery, 1, key, sizeof(key), NULL);
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

inline char* Authorizer::encrypt(int cardID) {
    char const* payload = (char*) std::to_string(cardID).c_str();
    byte shaResult[32];

    mbedtls_md_context_t ctx;
    mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;

    const size_t payloadLength = strlen(payload);

    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 0);
    mbedtls_md_starts(&ctx);
    mbedtls_md_update(&ctx, (const unsigned char *) payload, payloadLength);
    mbedtls_md_finish(&ctx, shaResult);
    mbedtls_md_free(&ctx);

    Serial.print("Hash: ");

    char str[3*sizeof(shaResult)];
    for(int i= 0; i < sizeof(shaResult); i++){
        sprintf(str+i, "%02x", (int)shaResult[i]);
    }

    return str;
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
