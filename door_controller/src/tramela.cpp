static const char* TAG = "main";

#include <tramela.h>

#include <Arduino.h>

#include <sqlite3.h> // sqlite3_initialize()

#include <networkmanager.h>
#include <timemanager.h>
#include <dbmanager.h>
#include <doormanager.h>
#include <cardreader.h>
#include <mqttmanager.h>
#include <diskmanager.h>
#include <firmwareOTA.h> // firmwareOKWatchdog()

int doorID = 1;

unsigned long currentMillis;

void setup() {
    // We always try to send logs to the serial port
    Serial.begin(115200);

    // wait for serial port to connect. Needed for native USB port only
    while (!Serial) { ; }
    delay(2000);

    initLog();

    currentMillis = millis();

    // Try to set the system time from the HW clock, so the timestamp for
    // subsequent log messages is correct. This may or may not succeed.
    initTimeOffline();

    log_v("Start program");

    bool diskOK = initDisk();

    if (diskOK) { initDiskLog(); }

    initWiFi(); // The sooner the better :), but after disk logging is up

    // So we can check for the master key during time initialization
    initDoor();
    initCardReaders();

    // Make sure we have the correct time before continuing. If we already
    // got the time from the HW clock above, great; if not, wait for NTP.
    int attempts = 0;
    while(!initTime()) { // Timeouts after 2s
        firmwareOKWatchdog();

        checkDoor(); // No DB yet, so this only works for the master key

        ++attempts;

        if (attempts > 60) { // We've been waiting for 2 minutes
            log_e("Failed to obtain time from both HW clock "
                  "and network too many times, restarting");
            delay(2000); // time to flush pending logs
            esp_restart(); // Desperate times call for desperate measures
        } else if (attempts % 20 == 0) { // 40 seconds, 80 seconds
            log_i("Failed to obtain time from both HW clock "
                  "and network, resetting network");

            netReset();
        }
    }

    if (diskOK) { sqlite3_initialize(); }
    initMqtt();
    if (diskOK) { initDBMan(); }
    firmwareOKWatchdog();
}

void loop() {
    currentMillis = millis();
    firmwareOKWatchdog();
    checkDoor();
    uploadLogs();
    checkDoor();
    checkNetConnection();
    checkDoor();
    checkTimeSync();
}
