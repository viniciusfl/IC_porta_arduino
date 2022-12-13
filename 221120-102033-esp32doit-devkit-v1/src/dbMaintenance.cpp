#include <WiFi.h>
#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include <sqlite3.h>
#include <networkConnection.h>
#include <common.h>
#include <timeKeeping.h>
#include <dbMaintenance.h>

#define RETRY_DOWNLOAD_TIME 60000

#define DOWNLOAD_INTERVAL 20000

WiFiClient client;

char SERVER[] = {"10.0.2.106"};

// This should be called from setup()
void dataBase::init(){
    db = NULL; // check the comment near dataBase::close()

    if (!SD.begin()){
        Serial.println("Card Mount Failed, aborting");
        Serial.flush();
        while (true) delay(10);
    }
    else{
        Serial.println("SD connected.");
    }

    // TODO: Why is this here?
    delay(300);

    sqlite3_initialize();

    // TODO: this is weird, we want to be able to survive crashes
    // reset both timestamps and then update one BD so we don't have trouble when we reset program
    //resetTimestampFiles();
    chooseInitialDB();
}

// This should be called from loop()
// At each call, we determine the current state we are in, perform
// a small chunk of work, and return. This means we do not hog the
// processor and can pursue other tasks while updating the DB.
void dataBase::update(){
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

void dataBase::checkCurrentCard() {
    if (searching) search();
}

void dataBase::startDownload(){
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
    netLineBuffer[0] = 0;
    position = 0;
    previous = 0;

    client.println("GET /arduino.txt HTTP/1.1");
    client.println("Host: 10.0.2.106");
    client.println("Connection: close");
    client.println();

    // Hack alert! This is a dirty way of saying "not the current DB"
    newDB = 0;
    if (currentDB == 0) newDB = 1;

    // remove old DB files
    SD.remove(timestampfiles[newDB]);
    SD.remove(dbNames[newDB]);

#ifdef DEBUG
    Serial.print("Writing to ");
    Serial.println(dbNames[newDB]);
#endif
}

void dataBase::finishDownload(){

    client.flush();
    client.stop();
    downloading = false;

    // Out with the old, in with the new

    // FIXME: we should only save the timestamp etc.
    //        if the download was successful
    // QUESTION:how can i know that?

    File f = SD.open(dbNames[newDB], FILE_WRITE);
    f.println(1);
    f.close();

    f = SD.open(dbNames[currentDB], FILE_WRITE);
    f.println(0);
    f.close();

    lastDownloadTime = currentMillis;
    Serial.println("Disconnecting from server and finishing db update.");

    currentDB = newDB;
    newDB = -1; // invalid

    closeDB();
    openDB();
}

void dataBase::processDownload(){
    if (!client.available()) return;

    char c = client.read();

    if (headerDone) {
        // TODO: This works line-by-line, but we are downloading a
        //       binary file, we might exhaust the device memory.
        if (c == '\r')
            return;
        if (c == '\n') {
            #ifdef DEBUG
                Serial.println("Writing " + String(netLineBuffer) + " to DB file");
            #endif
            insert(netLineBuffer);
            position = 0;
            netLineBuffer[position] = 0;
            return;
        }
        netLineBuffer[position] = c;
        netLineBuffer[position + 1] = 0;
        ++position;
    } else {
        if (c == '\n') {
            if (beginningOfLine and previous == '\r')
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

void dataBase::chooseInitialDB(){
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

// TODO: I think this should not exist
// reset both timestamp files to zero
void dataBase::resetTimestampFiles()
{
    File f;
    for (int i = 0; i < 2; i++)
    {
        SD.remove(timestampfiles[i]);
        f = SD.open(timestampfiles[i], FILE_WRITE);
        int a = 0;
        f.println(a);
        f.close();
    }
}

int dataBase::openDB() {
    int rc = sqlite3_open(dbNames[currentDB], &db);
    if (rc) {
        Serial.printf("Can't open database: %s\n", sqlite3_errmsg(db));
    } else {
        Serial.printf("Opened database successfully\n");
    }

    return rc;
}

static int callback(void *action, int argc, char **argv, char **azColName){
    switch (*((CBAction*) action)) {
        case CHECK_CARD:
            if (atoi(argv[0]) == 1)
                Serial.println("Exists in db.");
            else
                Serial.println("Doesn't exist in db.");
            break;
        case IGNORE:
            break;
        default:
            ;
    }

    return 0;
}


// search element through current database
bool dataBase::search(){
    Serial.print("Card reader ");
    Serial.print(currentCardReader);
    Serial.println(" was used.");
    Serial.print("We received -> ");
    Serial.println(currentCardID);

    // Make query and execute it
    char searchDB[300];
    sprintf(searchDB, "SELECT EXISTS(SELECT * FROM %s WHERE cartao='%lu')", dbNames[0], currentCardID);
    exec(searchDB, CHECK_CARD);

    searching = false;

    //generateLog(currentCardID);

    return true; // FIXME: not used yet
}

void dataBase::generateLog(unsigned long int id){ // FIXME: we should generate log with name/RA

    DateTime moment = DateTime(hwclock.unixTime());
    // FIXME: generate log for both people allowed and not allowed
    Serial.println("generating log");
    SD.remove("/log.txt");
    File log = SD.open("/log.txt", FILE_APPEND);
    if (!log){
            Serial.println(" couldnt open log file...");
    }
    char daysOfTheWeek[15][15] = {"domingo", "segunda", "terça", "quarta", "quinta", "sexta", "sabado"};
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
int dataBase::exec(const char *sql, CBAction action){
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
void dataBase::insert(char* element){
    int rc;
    char insertMsg[100];
    sprintf(insertMsg, "INSERT INTO %s (cartao) VALUES ('%s')", dbNames[0], element);
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
// here and in dataBase::init() just in case.
void dataBase::closeDB(){
    sqlite3_close(db);
    db = NULL;
}

