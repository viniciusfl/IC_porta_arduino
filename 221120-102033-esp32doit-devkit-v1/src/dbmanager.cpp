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
            bool userAuthorized(int readerID, unsigned long cardID);

        private:
            sqlite3 *db;
            File file;
            const char *dbNames[100] = {"/bancoA.db", "/bancoB.db"};
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
            int exec(const char *sql, CBAction);
            void insert(char *element);
    };


    bool downloading = false; // Is there an ongoing DB update?

    bool authorized = false;

    WiFiClient client;

    char SERVER[] = {"10.0.2.106"};

    // This should be called from setup()
    void DBManager::init(){
        db = NULL; // check the comment near DBManager::close()

        if (!SD.begin()){
            Serial.println("Card Mount Failed, aborting");
            Serial.flush();
            while (true) delay(10);
        }
        else{
            Serial.println("SD connected.");
        }

        sqlite3_initialize();

        chooseInitialDB();
    }

    // This should be called from loop()
    // At each call, we determine the current state we are in, perform
    // a small chunk of work, and return. This means we do not hog the
    // processor and can pursue other tasks while updating the DB.
    void DBManager::update(){
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

    void DBManager::startDownload(){
        client.connect(SERVER, 80);
        if (client.connected()) {
            Serial.println(F("Connected to server."));
        } else {
            Serial.println(F("Connection to server failed."));
        }

        // If connection failed, pretend nothing
        // ever happened and try again later
        if (!client.connected()){
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

    #ifdef DEBUG
        Serial.print("Writing to ");
        Serial.println(dbNames[newDB]);
    #endif
    }

    void DBManager::finishDownload(){

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
        Serial.println("Disconnecting from server and finishing db update.");

        currentDB = newDB;
        newDB = -1; // invalid

        closeDB();
        openDB();
    }

    void DBManager::processDownload(){
        if (!client.available()) return;
        char c = client.read();

        if (headerDone) {
            netLineBuffer = netLineBuffer + c;
            if(netLineBuffer.length() >= netLineBufferSize){
                Serial.println((String) "Writing " + netLineBuffer.length() + " bytes to db....");
                file.print(netLineBuffer);
                netLineBuffer = "";
                return;
            }
            return;
        } else {
            if (c == '\n') {
                if (beginningOfLine && previous == '\r')
                {
                    headerDone = true;
                #ifdef DEBUG
                    Serial.println(F("Header done!"));
                #endif
                }
                else {
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

    void DBManager::chooseInitialDB(){
        currentDB = -1; // invalid
        int max = -1;
        for (char i = 0; i < 2; ++i){ // 2 is the number of DBs
            File f = SD.open(timestampfiles[i]);
            if (!f){
                Serial.print(dbNames[i]);
                Serial.println(" not available");
            }
            else{
                int t = f.parseInt(); // If reading fails, this returns 0

                Serial.print(dbNames[i]);
                Serial.print(" timestamp: ");
                Serial.println(t);

                if (t > max)
                {
                    max = t;
                    currentDB = i;
                }
            }
            f.close();
        }

        if (currentDB < 0) {
            currentDB = 0;
            startDownload();
            Serial.printf("Downloading DB for the first time...");
        } else {
            Serial.printf("Choosing %s as current DB.\n", dbNames[currentDB]);
            openDB();
        }
    }

    int DBManager::openDB() {
        if (db != NULL) return 0;

        int rc = sqlite3_open(dbNames[currentDB], &db);
        if (rc) {
            Serial.printf("Can't open database: %s\n", sqlite3_errmsg(db));
        } else {
            Serial.printf("Opened database successfully\n");
        }

        return rc;
    }

    static int callback(void *action, int argc, char **argv, char **azColName) {
        switch (*((CBAction*) action)) {
            case CHECK_CARD:
                if (atoi(argv[0]) >= 1) authorized = true;
                break;
            case IGNORE:
                break;
            default:
                ;
        }

        return 0;
    }

    // search element through current database
    // TODO: there is some problem with the wiegand reader and open DB files
    bool DBManager::userAuthorized(int readerID, unsigned long cardID) {
        if (db == NULL) return false;

        Serial.print("Card reader ");
        Serial.print(readerID);
        Serial.println(" was used.");
        Serial.print("We received -> ");
        Serial.println(cardID);

        // Make query and execute it
        char searchDB[300];
        sprintf(searchDB, "SELECT EXISTS(SELECT * FROM %s WHERE cartao='%lu')",
                dbNames[0], cardID);

        exec(searchDB, CHECK_CARD);

        //generateLog(cardID);

        if (authorized) {
            authorized = false;
            return true;
        } else {
            return false;
        }
    }

    void DBManager::generateLog(unsigned long int id){
        // TODO: we should generate log with name/RA

        DateTime moment = DateTime(time(NULL));
        // TODO: generate log for both people allowed and not allowed
        Serial.println("generating log");
        SD.remove("/log.txt");
        File log = SD.open("/log.txt", FILE_APPEND);
        if (!log){
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

    // receive sql query and execute it
    int DBManager::exec(const char *sql, CBAction action){
        Serial.println(sql);
        char *zErrMsg;
        long start = micros();
        int rc = sqlite3_exec(db, sql, callback, (void *) (&action), &zErrMsg);
        if (rc != SQLITE_OK)
        {
            Serial.printf("SQL error: %s\n", zErrMsg);
            sqlite3_free(zErrMsg);
        }
        else
        {
            Serial.printf("Operation done successfully\n");
        }
        Serial.print(F("Time taken:"));
        Serial.println(micros() - start);
        return rc;
    }

    // insert element on current db
    void DBManager::insert(char* element){
        if (db == NULL) return;

        int rc;
        char insertMsg[100];
        sprintf(insertMsg, "INSERT INTO %s (cartao) VALUES ('%s')",
                dbNames[0], element);

        rc = exec(insertMsg, IGNORE);
        if (rc != SQLITE_OK)
        {
            sqlite3_close(db);
            db = NULL;
            return;
        }
    }

    // In some rare situations, we might call this twice or call this before we
    // ever initialize the db first (which means the pointer is in an undefined
    // state). The sqlite3 docs say "The C parameter to sqlite3_close(C) and
    // sqlite3_close_v2(C) must be either a NULL pointer or an sqlite3 object
    // pointer [...] and not previously closed". So, we explicitly make it NULL
    // here and in DBManager::init() just in case.
    void DBManager::closeDB(){
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
