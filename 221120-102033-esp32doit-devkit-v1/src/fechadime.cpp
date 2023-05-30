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

int doorID = 1;

unsigned long currentMillis;
bool sdPresent = false;

const char* lastReaderID;
unsigned long int lastCardID;

void checkDoor() {
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

void setup() {
    // Cannot log anything to disk before SD.begin(), so we send to Serial
    Serial.begin(115200);

    // wait for serial port to connect. Needed for native USB port only
    while (!Serial) { ; }
    delay(2000);

    currentMillis = millis();

    // Try to set the system time from the HW clock, so the timestamp for
    // subsequent log messages is correct. This may or may not succeed.
    initTimeOffline();

    log_v("Start program");

    if (!SD.begin()) {
        log_e("Card Mount Failed...");
    } else {
        log_v("SD connected.");
        sdPresent = true;
    }

    initLog(); // Send logs to disk, after initTimeOffline() and SD.begin()

    initWiFi(); // The sooner the better :), but after disk logging is up

    // So we can check for the master key during time initialization
    initCardReaders();

    // Make sure we have the correct time before continuing. If we already
    // got the time from the HW clock above, great; if not, wait for NTP.
    int attempts = 0;
    while(!initTime()) { // Timeouts after 2s
        checkDoor(); // No DB yet, so this only works for the master key

        ++attempts;

        if (attempts > 60) { // We've been waiting for 2 minutes
            log_e("Failed to obtain time from both HW clock "
                  "and network too many times, restarting");
            ESP.restart(); // Desperate times call for desperate measures
        } else if (attempts > 20) { // 40 seconds
            log_i("Failed to obtain time from both HW clock "
                  "and network, resetting network");

            netReset();
        }
    }

    initLog(); // Rotate logfile to use updated time
    sqlite3_initialize();
    initMqtt();
    initDBMan();
}

void loop() {
    currentMillis = millis();
    processLogs();
    checkNetConnection();
    checkTimeSync();
    checkDoor();
}
