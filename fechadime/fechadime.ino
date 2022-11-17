#define USE_WIFI
//#define TESTING
#include <RTClib.h> // Work around bug in the arduino IDE
//#define DEBUG
unsigned long currentMillis = 0;

void setup() {
    //Serial.begin(9600);
    Serial.begin(115200);
    // wait for serial port to connect. Needed for native USB port only
    while (!Serial) { ; }
    Serial.println(F("Start program"));

    initNetwork();
    
    initDisk();
    
    initCardReader();

    currentMillis = millis();

    //initRTC();

}

void loop() {
    cardMaintenance();
    currentMillis = millis();
    checkNetConnection();
    //checkRTCsync();
    dbMaintenance();
    
}
