static const char *TAG = "auth";

#include <tramela.h>
#include "mbedtls/md.h"
#include <Arduino.h>
#include <sqlite3.h>

const char* master_keys[] = {
    "3acce68667c2d4bafedb366ef9c221ebdf3ca9df1838b655603ee107d968f3c4",
    "19b20713d9ad41dc020114f5c2101083a1f8791bcc450a3fc2fb2cbee2d560d1"
};

// This is a wrapper around SQLite which allows us
// to query whether a user is authorized to enter.
class Authorizer {
    public:
        int openDB(const char *filename);
        inline void closeDB();
        inline bool userAuthorized(const char* readerID, const char* cardHash);
        inline void refreshQuery();
        inline void calculate_hash(unsigned long cardID, char* hashBuf);
    private:
        // check the comment near Authorizer::closeDB()
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
        log_d("Opened database successfully %s", filename);
        rc = sqlite3_prepare_v2(sqlitedb,
                                "SELECT EXISTS(SELECT * FROM auth "
                                "WHERE userID=? AND doorID=?)",
                                -1, &dbquery, NULL);

        if (rc != SQLITE_OK) {
            log_e("Can't generate prepared statement: %s: %s",
                  sqlite3_errstr(sqlite3_extended_errcode(sqlitedb)),
                  sqlite3_errmsg(sqlitedb));
        } else {
            log_d("Prepared statement created");
        }
    }

    return rc;
}

// search element through current database
inline bool Authorizer::userAuthorized(const char* readerID,
                                       const char* cardHash) {

    // MASTER IDs are defined at the beginning of this file.
    for (int i = 0; i < sizeof(master_keys)/sizeof(master_keys[0]); ++i) {
        if (!strcmp(cardHash, master_keys[i])) {
            log_w("MASTER card used, openning door.");
            return true;
        }
    }

    // This is necessary because someone might open the door during
    // our DB file swap. We wait for up to 500 milliseconds for the
    // swap to complete.
    unsigned long start = millis();
    while (sqlitedb == NULL && millis() - start < 500) { delay(20); }

    if (sqlitedb == NULL) {
        log_e("Cannot read DB, denying access");
        return false;
    }

    log_d("Card reader %s was used. Received card hash %s",
           readerID, cardHash);

    sqlite3_bind_text(dbquery, 1, cardHash, strlen(cardHash), NULL);
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

inline void Authorizer::calculate_hash(unsigned long cardID,
                                       char* hashBuf) {

    // Initialize the sha256 generator
    mbedtls_md_context_t ctx;
    mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 0);
    mbedtls_md_starts(&ctx);

    // Convert the cardID to a string (10-digit number) and hash it
    char buf[11]; // 10 digits + '\0'
    sprintf(buf, "%10lu", cardID);
    mbedtls_md_update(&ctx, (const unsigned char *) buf, 10);

    // Save the resulting binary hash and close the sha256 generator
    byte binHash[32];
    mbedtls_md_finish(&ctx, binHash);
    mbedtls_md_free(&ctx);

    // Convert the binary hash to a string (hexadecimal representation)
    for(int i = 0; i < 32; ++i){
        snprintf(hashBuf + 2*i, 3, "%02hhx", binHash[i]);
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

inline void Authorizer::refreshQuery() { sqlite3_reset(dbquery); }

Authorizer authorizer;


int openDB(const char* filename) { return authorizer.openDB(filename); }

void closeDB() { authorizer.closeDB(); }

bool userAuthorized(const char* readerID, const char* cardHash) {
    return authorizer.userAuthorized(readerID, cardHash);
}

void calculate_hash(unsigned long cardID, char* hashBuf) {
    authorizer.calculate_hash(cardID, hashBuf);
}

void refreshQuery() { authorizer.refreshQuery(); }
