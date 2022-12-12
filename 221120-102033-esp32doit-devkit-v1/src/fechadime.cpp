
#include <../include/common.h>
#include <../include/timeKeeping.h>
#include <../include/dbMaintenance.h>
#include <../include/cardReader.h>
#include <Arduino.h>


RTC clk = RTC();

dataBase db = dataBase();

void setup() {
    Serial.begin(115200);
    // wait for serial port to connect. Needed for native USB port only
    while (!Serial) { ; }
    Serial.println(F("Start program"));
    WiFiInit();
    clk.initRTC(); 
    db.initDataBase();
    initCardReader();
    currentMillis = millis();
}

void loop() {
    currentMillis = millis();
    db.dbMaintenance(DateTime(clk.unixTime()));
    clk.checkRTCsync();
    cardMaintenance();
}
