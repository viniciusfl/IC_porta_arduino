static const char* TAG = "door";

#include <tramela.h>
#include <Arduino.h>
#include <cardreader.h>
#include <authorizer.h>

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

const char* readerID;
unsigned long int cardID;

void checkDoor() {
    if (checkCardReaders(readerID, cardID)) {
        char cardHash[65]; // 64 chars + '\0'
        calculate_hash(cardID, cardHash);
        bool authorized = userAuthorized(readerID, cardHash);
        logAccess(readerID, cardHash, authorized);
        if (authorized) {
            openDoor(readerID);
        } else {
            denyToOpenDoor(readerID);
        }
        refreshQuery(); // after we open the door, so things go faster
    }
}
