#include <WiFi.h>
#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include <sqlite3.h>
#include <common.h>
#include <RTClib.h>
#include <dbmanager.h>

#define RETRY_DOWNLOAD_TIME 60000

#define DOWNLOAD_INTERVAL 20000

#define DEBUG

#include <sqlite3.h>

namespace DBNS {

    class DBManager{
        public:
            void init();
            void update();
            bool userAuthorized(int readerID, unsigned long cardID);

        private:
            sqlite3 *db;
            sqlite3_stmt *dbquery;
            File file;
            const char *dbNames[100] = {"/sd/bancoA.db", "/sd/bancoB.db"};
            const char *timestampfiles[100] = {"/TSA.TXT", "/TSB.TXT"};
            int currentDB = -1; // invalid
            int newDB = -1;
            unsigned long lastDownloadTime = 0;
            bool headerDone = false;
            bool beginningOfLine = true;

            String netLineBuffer;
            char previous;
            int position = 0;
            int netLineBufferSize = 40;

            void startDownload();
            void finishDownload();
            void processDownload();
            void chooseInitialDB();
            void generateLog(unsigned long int id);
            int openDB();
            void closeDB();
            void insert(char *element);
    };

    bool downloading = false; // Is there an ongoing DB update?

    bool authorized = false;

    WiFiClient client;

    char SERVER[] = {"10.0.2.106"};

    // This should be called from setup()
    void DBManager::init() {
        db = NULL; // check the comment near DBManager::close()
        dbquery = NULL;

        if (!SD.begin()) {
            Serial.println("Card Mount Failed, aborting");
            Serial.flush();
            while (true) delay(10);
#       ifdef DEBUG
        } else {
            Serial.println("SD connected.");
#       endif
        }

        sqlite3_initialize();

        chooseInitialDB();
    }

    // This should be called from loop()
    // At each call, we determine the current state we are in, perform
    // a small chunk of work, and return. This means we do not hog the
    // processor and can pursue other tasks while updating the DB.
    void DBManager::update() {
        // We start a download only if we are not already downloading
        if (!downloading) {
            // millis() wraps every ~49 days, but
            // wrapping does not cause problems here
            // TODO: we might prefer to use dates,
            //       such as "at midnight"
            //
            // ANWSER: we can use an alarm with RTC 1307
            // https://robojax.com/learn/arduino/?vid=robojax_DS1307-clock-alarm
            // I believe the nodeMCU RTC might also have this feature

            if (currentMillis - lastDownloadTime > DOWNLOAD_INTERVAL)
                startDownload();
            return;
        }

        // If we did not return above, we are downloading
        if (!client.available() && !client.connected()) {
            finishDownload();
            return;
        }

        // If we did not disconnect above, we are connected
        processDownload();
    }

    void DBManager::startDownload() {
        client.connect(SERVER, 80);

#       ifdef DEBUG
        if (client.connected()) {
            Serial.println(F("Connected to server."));
        } else {
            Serial.println(F("Connection to server failed."));
        }
#       endif

        // If connection failed, pretend nothing
        // ever happened and try again later
        if (!client.connected()) {
            lastDownloadTime = lastDownloadTime + RETRY_DOWNLOAD_TIME;
            client.stop();
            return;
        }

        downloading = true;
        headerDone = false;
        beginningOfLine = true;
        netLineBuffer = "";
        position = 0;
        previous = 0;

        // Hack alert! This is a dirty way of saying "not the current DB"
        newDB = 0;
        if (currentDB == 0) newDB = 1;

        client.println((String) "GET /banco.db HTTP/1.1");
        client.println("Host: 10.0.2.106");
        client.println("Connection: close");
        client.println();


        // remove old DB files
        SD.remove(timestampfiles[newDB]);
        SD.remove(dbNames[newDB]);

        file = SD.open(dbNames[newDB], FILE_WRITE);

#       ifdef DEBUG
        Serial.print(F("Writing to "));
        Serial.println(dbNames[newDB]);
#       endif
    }

    void DBManager::finishDownload() {

        client.flush();
        client.stop();
        file.print(netLineBuffer);
        downloading = false;

        file.close();

        // Out with the old, in with the new

        // FIXME: we should only save the timestamp etc.
        //        if the download was successful
        // QUESTION: how can i know that?

        file = SD.open(timestampfiles[newDB], FILE_WRITE);
        file.println(1);
        file.close();

        file = SD.open(timestampfiles[currentDB], FILE_WRITE);
        file.println(0);
        file.close();

        lastDownloadTime = currentMillis;

#       ifdef DEBUG
        Serial.println(F("Disconnecting from server and finishing db update."));
#       endif

        currentDB = newDB;
        newDB = -1; // invalid

        closeDB();
        openDB();
    }

    void DBManager::processDownload() {
        if (!client.available()) return;
        char c = client.read();

        if (headerDone) {
            netLineBuffer = netLineBuffer + c;
            if(netLineBuffer.length() >= netLineBufferSize) {
#               ifdef DEBUG
                Serial.println((String) "Writing " + netLineBuffer.length() + " bytes to db....");
#               endif
                file.print(netLineBuffer);
                netLineBuffer = "";
                return;
            }
            return;
        } else {
            if (c == '\n') {
                if (beginningOfLine && previous == '\r') {
                    headerDone = true;
#                   ifdef DEBUG
                    Serial.println(F("Header done!"));
#                   endif
                } else {
                    previous = 0;
                }
                beginningOfLine = true;
            } else {
                previous = c;
                if (c != '\r')
                    beginningOfLine = false;
            }
        }
    }

    void DBManager::chooseInitialDB() {
        currentDB = -1; // invalid
        int max = -1;
        for (char i = 0; i < 2; ++i) { // 2 is the number of DBs
            File f = SD.open(timestampfiles[i]);
            if (!f) {
#               ifdef DEBUG
                Serial.print(dbNames[i]);
                Serial.println(" not available");
#               endif
            } else {
                int t = f.parseInt(); // If reading fails, this returns 0

#               ifdef DEBUG
                Serial.print(dbNames[i]);
                Serial.print(" timestamp: ");
                Serial.println(t);
#               endif

                if (t > max) {
                    max = t;
                    currentDB = i;
                }
            }
            f.close();
        }

        if (currentDB < 0) {
            currentDB = 0;
            startDownload();
#           ifdef DEBUG
            Serial.printf("Downloading DB for the first time...");
#           endif
        } else {
#           ifdef DEBUG
            Serial.printf("Choosing %s as current DB.\n", dbNames[currentDB]);
#           endif
            openDB();
        }
    }

    int DBManager::openDB() {
        if (db != NULL) return 0;

        int rc = sqlite3_open(dbNames[currentDB], &db);
        if (rc != SQLITE_OK) {
            Serial.printf("Can't open database: %s\n", sqlite3_errmsg(db));
        } else {
#           ifdef DEBUG
            Serial.printf("Opened database successfully\n");
#           endif

            rc = sqlite3_prepare_v2(db,
                    "SELECT EXISTS(SELECT * FROM ? WHERE cartao='?')",
                    -1, &dbquery, NULL);

            if (rc != SQLITE_OK) {
                Serial.printf("Can't generate prepared statement: %s\n",
                              sqlite3_errmsg(db));
#           ifdef DEBUG
            } else {
                Serial.printf("Prepared statement created\n");
#           endif
            }
        }

        return rc;
    }

    // search element through current database
    bool DBManager::userAuthorized(int readerID, unsigned long cardID) {
        if (db == NULL) return false;

#       ifdef DEBUG
        Serial.print("Card reader ");
        Serial.print(readerID);
        Serial.println(" was used.");
        Serial.print("We received -> ");
        Serial.println(cardID);
#       endif

        sqlite3_reset(dbquery);
        sqlite3_bind_text(dbquery, 1, "bancoA", -1, SQLITE_STATIC);
        sqlite3_bind_int(dbquery, 2, cardID);

        int rc = sqlite3_step(dbquery);
        while (rc == SQLITE_ROW) {
            if (1 == sqlite3_column_int(dbquery, 0))
                authorized = true;
            rc = sqlite3_step(dbquery);
        }

        if (rc != SQLITE_DONE) {
            Serial.printf("Error querying DB: %s\n", sqlite3_errmsg(db));
        }

        //generateLog(cardID);

        if (authorized) {
            authorized = false;
            return true;
        } else {
            return false;
        }
    }

    void DBManager::generateLog(unsigned long int id) {
        // TODO: we should generate log with name/RA

        DateTime moment = DateTime(time(NULL));
        // TODO: generate log for both people allowed and not allowed
        Serial.println("generating log");
        SD.remove("/log.txt");
        File log = SD.open("/log.txt", FILE_APPEND);
        if (!log) {
                Serial.println(" couldnt open log file...");
        }
        char daysOfTheWeek[15][15] = {"domingo", "segunda", "terça",
                                      "quarta", "quinta", "sexta", "sabado"};

        log.print(id);
        log.print(" entered ");
        log.print(moment.year(), DEC);
        log.print('/');
        log.print(moment.month(), DEC);
        log.print('/');
        log.print(moment.day(), DEC);
        log.print(" (");
        log.print(daysOfTheWeek[moment.dayOfTheWeek()]);
        log.print(") ");
        log.print(moment.hour(), DEC);
        log.print(':');
        log.print(moment.minute(), DEC);
        log.print(':');
        log.print(moment.second(), DEC);
        log.println();
        log.close();
        Serial.println("finished log....");
    }

    // insert element on current db
    void DBManager::insert(char* element) {
        if (db == NULL) return;

        char insertMsg[100];
        sprintf(insertMsg, "INSERT INTO %s (cartao) VALUES ('%s')",
                "bancoA", element);

        int rc;
        char *zErrMsg;
        rc = sqlite3_exec(db, insertMsg, NULL, NULL, &zErrMsg);
        if (rc != SQLITE_OK) {
            Serial.printf("SQL error: %s\n", zErrMsg);
            sqlite3_free(zErrMsg);
            return;
        } else {
            Serial.printf("Operation done successfully\n");
        }
    }

    // In some rare situations, we might call this twice or call this before we
    // ever initialize the db first (which means the pointer is in an undefined
    // state). The sqlite3 docs say "The C parameter to sqlite3_close(C) and
    // sqlite3_close_v2(C) must be either a NULL pointer or an sqlite3 object
    // pointer [...] and not previously closed". So, we explicitly make it NULL
    // here and in DBManager::init() just in case.
    void DBManager::closeDB() {
        sqlite3_finalize(dbquery);
        dbquery = NULL;
        sqlite3_close(db);
        db = NULL;
    }

    DBManager db;
}

void initDB() {
    DBNS::db.init();
}

void updateDB() {
    DBNS::db.update();
}

bool userAuthorized(int readerID, unsigned long cardID) {
    return DBNS::db.userAuthorized(readerID, cardID);
}
