static const char* TAG = "main";

#include <common.h>
#include <networkmanager.h>
#include <timemanager.h>
#include <apiserver.h>
#include <dbmanager.h>
#include <authorizer.h>
#include <cardreader.h>
#include <log_conf.h>
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <sqlite3.h>

#define HEAP_CHECK_INTERVAL 1000
unsigned long lastHeapCheck;

int doorID = 1;

unsigned long currentMillis;
bool sdPresent = false;

void setup() {
    Serial.begin(115200);
    // wait for serial port to connect. Needed for native USB port only
    while (!Serial) { ; }
    delay(2000);

    log_v("Start program");

    if (!SD.begin()) { 
        log_e("Card Mount Failed...");
    } else {
        log_v("SD connected.");
        sdPresent = true;
    }

    sqlite3_initialize();

    currentMillis = millis();
    lastHeapCheck = currentMillis;

    initLog();
    initWiFi();
    initTime();
    initDBMan();
    initCardReaders();
}

void loop() {
    currentMillis = millis();
#   ifdef PRINT_HEAP
    if(currentMillis - lastHeapCheck > HEAP_CHECK_INTERVAL){
        lastHeapCheck = currentMillis;
        log_i("[APP] Free memory: %d bytes", esp_get_free_heap_size());
    }
#   endif
    checkNetConnection();
    updateDB();
    checkTimeSync();
    updateLogBackup();
    const char* lastReaderID;
    unsigned long int lastCardID;
    if (checkCardReaders(lastReaderID, lastCardID)) {
        bool authorized = userAuthorized(lastReaderID, lastCardID);
        generateLog(lastReaderID, lastCardID, authorized);
        if (authorized) {
            openDoor(lastReaderID);
        } else {
            denyToOpenDoor(lastReaderID);
        }
    }
}