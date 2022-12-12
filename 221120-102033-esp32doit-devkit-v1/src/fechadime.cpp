#include <../include/common.h>
#include <../include/timeKeeping.h>
#include <../include/dbMaintenance.h>
#include <../include/cardReader.h>
#include <Arduino.h>

RTC hwclock = RTC();

dataBase db = dataBase();

void setup() {
    Serial.begin(115200);
    // wait for serial port to connect. Needed for native USB port only
    while (!Serial) { ; }
    Serial.println(F("Start program"));
    WiFiInit();
    hwclock.init();
    db.initDataBase();
    initCardReader();
    currentMillis = millis();
}

void loop() {
    currentMillis = millis();
    db.dbMaintenance(DateTime(hwclock.unixTime()));
    hwclock.checkSync();
    cardMaintenance();
}
