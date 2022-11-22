
#include <../include/common.h>
#include <../include/timeKeeping.h>
#include <../include/cardReader.h>
#include <Arduino.h>
// #include <SPI.h>

//#define TESTING
//#define DEBUG
#define USE_WIFI

RTC relogio = RTC();
//RTC relogio;
//RTC relogio;
void setup() {

    Serial.begin(115200);

    // wait for serial port to connect. Needed for native USB port only
    while (!Serial) { ; }

    Serial.println(F("Start program"));

    WiFiInit();

    relogio.initRTC();
    //relogio = RTC();

    //db = dataBase();
    
    currentMillis = millis();


}

void loop() {
    //cardMaintenance();
    currentMillis = millis();
    checkWiFiConnection();
    relogio.checkRTCsync();
    //db.dbMaintenance();
    
}
