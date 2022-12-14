#include <common.h>
#include <networkConnection.h>
#include <timemanager.h>
#include <dbMaintenance.h>
#include <cardReader.h>
#include <Arduino.h>

TimeManager hwclock;
unsigned long currentMillis;
dataBase db;
cardData theCardData;

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
    if (checkCardReaders(&theCardData)) {
        db.checkCurrentCard(theCardData.readerID, theCardData.cardID);
    }
}
