#include <common.h>
#include <networkmanager.h>
#include <timemanager.h>
#include <dbmanager.h>
#include <cardreader.h>
#include <Arduino.h>

TimeManager hwclock;
unsigned long currentMillis;
DBManager db;
int lastReaderID;
unsigned long int lastCardID;

void setup() {
    Serial.begin(115200);
    // wait for serial port to connect. Needed for native USB port only
    while (!Serial) { ; }
    Serial.println(F("Start program"));
    currentMillis = millis();
    WiFiInit();
    hwclock.init();
    db.init();
    initCardReaders();
}

void loop() {
    currentMillis = millis();
    db.update();
    hwclock.checkSync();
    if (checkCardReaders(lastReaderID, lastCardID)) {
        if (db.checkCard(lastReaderID, lastCardID)) {
            blinkOk(lastReaderID);
            Serial.println("Exists in db.");
        } else {
            blinkFail(lastReaderID);
            Serial.println("Doesn't exist in db.");
        }
    }
}
