#include <common.h>
#include <networkConnection.h>
#include <timeKeeping.h>
#include <dbMaintenance.h>
#include <cardReader.h>
#include <Arduino.h>

RTC hwclock = RTC();
unsigned long currentMillis;
dataBase db = dataBase();
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
    initCardReader();
}

void loop() {
    currentMillis = millis();
    db.update();
    hwclock.checkSync();
    if (cardMaintenance(&theCardData)) {
        db.checkCurrentCard(theCardData.readerID, theCardData.cardID);
    }
}
