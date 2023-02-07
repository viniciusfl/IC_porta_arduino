static const char* TAG = "main";

#include <common.h>
#include <networkmanager.h>
#include <timemanager.h>
#include <apiserver.h>
#include <dbmanager.h>
#include <authorizer.h>
#include <cardreader.h>
#include <log_conf.h>
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <sqlite3.h>

unsigned long currentMillis;

int doorID = 1;

void setup() {
    Serial.begin(115200);
    // wait for serial port to connect. Needed for native USB port only
    while (!Serial) { ; }
    delay(100);

    log_v("Start program");

    if (!SD.begin()) {
        log_e("Card Mount Failed, aborting");
        while (true) delay(10);
    } else {
        log_v("SD connected.");
    }

    sqlite3_initialize();

    currentMillis = millis();

    // TODO: I think initTime() should come right after WiFi, otherwise
    //       any attempts to get the time during initialization of the
    //       DB, as well as any log messages with timestamps, will yield
    //       wrong results. Any reason for this order? At the same time,
    //       making initLog run right after sqlite3_initialize would give
    //       us a record of generated logs, even if their timestamps are
    //       wrong before initTime().
    initWiFi();
    initLog();
    initDBMan();
    initTime();
    initCardReaders();
    initServer();
}

void loop() {
    currentMillis = millis();
    checkNetConnection();
    //updateDB();
    //checkTimeSync();
    updateServer();
    updateLogBackup();
    
    const char* lastReaderID;
    unsigned long int lastCardID;
    if (checkCardReaders(lastReaderID, lastCardID)) {
        bool authorized = userAuthorized(lastReaderID, lastCardID);
        generateLog(lastReaderID, lastCardID, authorized);
        if (authorized) {
            openDoor(lastReaderID);
        } else {
            denyToOpenDoor(lastReaderID);
        }
    }
}
