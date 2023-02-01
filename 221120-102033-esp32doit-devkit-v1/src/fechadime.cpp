static const char* TAG = "main";

#include <common.h>
#include <networkmanager.h>
#include <timemanager.h>
#include <dbmanager.h>
#include <authorizer.h>
#include <cardreader.h>
#include <log_conf.h>
#include <Arduino.h>
#include <sqlite3.h>

unsigned long currentMillis;

int doorID;

void setup() {
    Serial.begin(115200);
    // wait for serial port to connect. Needed for native USB port only
    while (!Serial) { ; }
    delay(100);

    log_v("Start program");

    sqlite3_initialize();

    currentMillis = millis();

    initWiFi();
    initLog();
    initDBMan();
    initTime();
    initCardReaders();
}

void loop() {
    currentMillis = millis();
    checkNetConnection();
    //updateDB();
    //checkTimeSync();
    updateLogBackup(getTime());
    const char* lastReaderID;
    unsigned long int lastCardID;
    if (checkCardReaders(lastReaderID, lastCardID)) {
        bool authorized = userAuthorized(lastReaderID, lastCardID);
        if (authorized) {
            Serial.println("Exists in db.");
            blinkOk(lastReaderID);
        } else {
            Serial.println("Doesn't exist in db.");
            blinkFail(lastReaderID);
        }
        generateLog(lastReaderID, lastCardID, authorized, getTime());
    }
}
