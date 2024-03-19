static const char* TAG = "door";

#include <tramela.h>
#include <Arduino.h>
#include <cardreader.h>

#define DOOR_OPEN 13

void initDoor() {
    pinMode(DOOR_OPEN, OUTPUT);
    digitalWrite(DOOR_OPEN, LOW);
}

void openDoor(const char* reader = NULL) {
    log_v("Opened door");
    unsigned long start = millis();
    digitalWrite(DOOR_OPEN, HIGH);
    if (NULL != reader) { blinkOk(reader); }
    delay(700 - (millis() - start));
    digitalWrite(DOOR_OPEN, LOW);
}

void denyToOpenDoor(const char* reader) {
    log_v("Denied to open door");
    if (NULL != reader) { blinkDeny(reader); }
}
