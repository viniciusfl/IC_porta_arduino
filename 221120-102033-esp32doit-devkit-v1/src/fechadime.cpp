#include <common.h>
#include <networkmanager.h>
#include <timemanager.h>
#include <dbmanager.h>
#include <cardreader.h>
#include <Arduino.h>

unsigned long currentMillis;

int doorID;

void setup() {
    Serial.begin(115200);
    // wait for serial port to connect. Needed for native USB port only
    while (!Serial) { ; }

#   ifdef DEBUG
    Serial.println("Start program");
#   endif

    currentMillis = millis();
    initWiFi();
    initTime();
    initDB();
    initCardReaders();
}

void loop() {
    currentMillis = millis();
    checkNetConnection();
    updateDB();
    //checkTimeSync();
    int lastReaderID;
    unsigned long int lastCardID;
    if (checkCardReaders(lastReaderID, lastCardID)) {
        if (userAuthorized(lastReaderID, lastCardID)) {
            Serial.println("Exists in db.");
            blinkOk(lastReaderID);
        } else {
            Serial.println("Doesn't exist in db.");
            blinkFail(lastReaderID);
        }
    }
}
