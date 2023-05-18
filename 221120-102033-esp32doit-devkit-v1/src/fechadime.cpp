static const char* TAG = "main";

#include <common.h>
#include <networkmanager.h>
#include <timemanager.h>
#include <dbmanager.h>
#include <authorizer.h>
#include <cardreader.h>
#include <log_conf.h>
#include <mqttmanager.h>
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <sqlite3.h>

#define HEAP_CHECK_INTERVAL 5000
#define PRINT_HEAP
unsigned long lastHeapCheck;

int doorID = 1;

unsigned long currentMillis;
bool sdPresent = false;


void setup() {
    Serial.begin(115200);
    // wait for serial port to connect. Needed for native USB port only
    while (!Serial) { ; }
    delay(2000);

    initLogSystem(); // Doesn't really make a difference if this is called here
    log_v("Start program");

    if (!SD.begin()) { 
        log_e("Card Mount Failed...");
    } else {
        log_v("SD connected.");
        sdPresent = true;
    }

    if (initTimeOffline()) {
        initLog(); // We know the current time, so we can name the logfile
    } else {
        initOfflineLogger();
    }

    sqlite3_initialize();

    currentMillis = millis();
    lastHeapCheck = currentMillis;
    initCardReaders();
    initWiFi();
    initMqtt();
    initTime();
    // If initTimeOffline() failed before, we need to do this here,
    // when knowing the current time allows us to name the logfile
    initLog();
    initDBMan();
}

void loop() {
    currentMillis = millis();
    processLogs();
#   ifdef PRINT_HEAP
    if(currentMillis - lastHeapCheck > HEAP_CHECK_INTERVAL){
        lastHeapCheck = currentMillis;
        log_i("[APP] Free memory: %d bytes", esp_get_free_heap_size());
    }
#   endif
    checkNetConnection();
    updateDB();
    checkTimeSync();
    const char* lastReaderID;
    unsigned long int lastCardID;
    if (checkCardReaders(lastReaderID, lastCardID)) {
        bool authorized = userAuthorized(lastReaderID, lastCardID);
        logAccess(lastReaderID, lastCardID, authorized);
        if (authorized) {
            openDoor(lastReaderID);
        } else {
            denyToOpenDoor(lastReaderID);
        }
    }
}
