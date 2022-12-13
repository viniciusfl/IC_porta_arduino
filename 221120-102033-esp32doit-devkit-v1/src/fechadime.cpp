#include <common.h>
#include <networkConnection.h>
#include <timeKeeping.h>
#include <dbMaintenance.h>
#include <cardReader.h>
#include <Arduino.h>

dataBase db = dataBase();

void setup() {
    Serial.begin(115200);
    // wait for serial port to connect. Needed for native USB port only
    while (!Serial) { ; }
    Serial.println(F("Start program"));
    WiFiInit();
    hwclock.init();
    db.init();
    initCardReader();
    currentMillis = millis();
}

void loop() {
    currentMillis = millis();
    db.update();
    hwclock.checkSync();
    cardMaintenance();
    db.checkCurrentCard();
}
