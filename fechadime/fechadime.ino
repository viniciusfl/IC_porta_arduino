#define USE_WIFI
#define DEBUG
//#define TESTING
#include <RTClib.h> // Work around bug in the arduino IDE

unsigned long currentMillis = 0;

void setup() {
    //Serial.begin(9600);
    Serial.begin(115200);
    // wait for serial port to connect. Needed for native USB port only
    while (!Serial) { ; }
    Serial.println(F("Start program"));

    currentMillis = millis();

    // The SS pins for the SD and ethernet cards
    //pinMode(4, OUTPUT);
    //pinMode(10, OUTPUT);

    //digitalWrite(4, HIGH); // Disable SD reader during ethernet initialization
    initNetwork();

    initRTC();

    //digitalWrite(10, HIGH); // Disable ethernet during SD initialization
                            // This is probably unnecessary, as the ethernet
                            // library is already loaded and, therefore,
                            // handling this
    initDisk();
}

void loop() {
    currentMillis = millis();

    checkNetConnection();
    checkRTCsync();
    dbMaintenance();
    dbSearch();
}
