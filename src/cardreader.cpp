static const char* TAG = "card";

#include <tramela.h>
#include <Arduino.h>
#include <Wiegand.h>
#include <cardreader.h>

#define DOOR_OPEN 13

// pins for card reader 1 (external)
#define EXTERNAL_D0  35
#define EXTERNAL_D1  34
#define EXTERNAL_BEEP 4
#define EXTERNAL_LED 2

#ifdef TWO_READERS
// pins for card reader 2 (internal)
#define INTERNAL_D0  33
#define INTERNAL_D1  25
#define INTERNAL_BEEP 32
#define INTERNAL_LED 0
#endif

// Note that, with more than one reader, trying to read two cards at
// exactly the same time will probably fail (we use a single data buffer
// for all readers). For our use case at least, that is irrelevant.

namespace ReaderNS {

    // We should not log things inside a callback,
    // so we store the message and log it later
    char connectedMsg[192];
    char disconnectedMsg[192];
    char readErrorMsg[192];

    void IRAM_ATTR captureIncomingData(uint8_t* data, uint8_t bits,
                                       const char* reader);

    inline unsigned long bitsToNumber(volatile const uint8_t* data,
                                      volatile const uint8_t bits);

    void IRAM_ATTR stateChanged(bool plugged, const char* message);

    void IRAM_ATTR receivedDataError(Wiegand::DataError error,
                                     uint8_t* rawData, uint8_t rawBits,
                                     const char* message);

    Wiegand external;

#   ifdef TWO_READERS
    Wiegand internal;
#   endif
    // We read the Wiegand data in a callback with interrupts disabled; to make
    // this callback as short as possible and pass this data to the "normal"
    // program flow, we use these:

    // Is there a user trying to open a door?
    volatile bool newAccess;

    // If so, it came from this reader
    const char* volatile readerID;

    // And this is the unprocessed card ID; just
    // a copy of what the wiegand lib gives us
    volatile uint8_t cardIDRaw[Wiegand::MAX_BYTES];

    // This is the bit length of the card ID, also
    // a copy of  what the wiegand lib gives us
    volatile uint8_t cardIDBits;

    // This reads the bitstream provided by the wiegand reader and converts
    // it to a single number.
    inline unsigned long bitsToNumber(volatile const uint8_t* data,
                                      volatile const uint8_t bits) {

        // Convert to hexadecimal
        char buf[17]; // 64 bits, way more than enough
        uint8_t bytes = (bits+7)/8;
        for (int i = 0; i < bytes; ++i) {
            snprintf(buf + 2*i, 3, "%02hhx", data[i]);
         }

        // Convert to decimal
        return strtoul(buf, NULL, 16);
    }

    // Function that is called when card is read; we do not call
    // bitsToNumber() here to make it as fast as possible (this
    // is called with interrupts disabled).
    void IRAM_ATTR captureIncomingData(uint8_t* data, uint8_t bits,
                                       const char* reader) {
        newAccess = true;
        readerID = reader;
        cardIDBits = bits;

        // It would be possible to avoid copying, but that could break
        // if something changes in the wiegand lib implementation.
        uint8_t bytes = (bits+7)/8;
        for (int i = 0; i < bytes; ++i) {
            cardIDRaw[i] = data[i];
        }
    }

    // Notifies when a reader has been connected or disconnected.
    // The second parameter can be anything we want --
    // Whatever is specified on `wiegand.onStateChange()`
    void IRAM_ATTR stateChanged(bool plugged, const char* reader) {
        if (plugged) {
            snprintf(connectedMsg, 192, "%s card reader state changed: "
                                        "CONNECTED", reader);
        } else {
            snprintf(disconnectedMsg, 192, "%s card reader state changed: "
                                           "DISCONNECTED", reader);
        }
    }

    void IRAM_ATTR receivedDataError(Wiegand::DataError error,
                                     uint8_t* rawData, uint8_t rawBits,
                                     const char* reader) {

        //Print value in HEX
        char buf[17]; // 64 bits, way more than enough
        uint8_t bytes = (rawBits+7)/8;
        for (int i = 0; i < bytes; ++i) {
            snprintf(buf + 2*i, 3, "%02hhx", rawData[i]);
        }

        snprintf(readErrorMsg, 192, "%s reader error: %s - Raw data: "
                "%u bits / %s", reader, Wiegand::DataErrorStr(error),
                rawBits, buf);
    }

    void IRAM_ATTR setExternal0PinState() {
        external.setPin0State(digitalRead(EXTERNAL_D0));
    }

    void IRAM_ATTR setExternal1PinState() {
        external.setPin1State(digitalRead(EXTERNAL_D1));
    }

#   ifdef TWO_READERS
    void IRAM_ATTR setInternal0PinState() {
        internal.setPin0State(digitalRead(INTERNAL_D0));
    }

    void IRAM_ATTR setInternal1PinState() {
        internal.setPin1State(digitalRead(INTERNAL_D1));
    }
#   endif

    // This should be called from setup()
    inline void initCardReaders() {

        pinMode(DOOR_OPEN, OUTPUT);
        digitalWrite(DOOR_OPEN, LOW);

        connectedMsg[0] = 0;
        disconnectedMsg[0] = 0;
        readErrorMsg[0] = 0;

        // Initialize pins for first Wiegand reader (external) as INPUT
        pinMode(EXTERNAL_D0, INPUT);
        pinMode(EXTERNAL_D1, INPUT);
        pinMode(EXTERNAL_LED, OUTPUT);
        pinMode(EXTERNAL_BEEP, OUTPUT);
        digitalWrite(EXTERNAL_BEEP, HIGH);
        digitalWrite(EXTERNAL_LED, HIGH);
        // Install listeners and initialize first Wiegand reader
        external.onReceive(captureIncomingData, "external");
        external.onReceiveError(receivedDataError, "external");
        external.onStateChange(stateChanged, "external");
        external.begin(34, true);

#       ifdef TWO_READERS
        // Initialize pins for second Wiegand reader (internal) as INPUT
        pinMode(INTERNAL_D0, INPUT);
        pinMode(INTERNAL_D1, INPUT);
        pinMode(INTERNAL_LED, OUTPUT);
        pinMode(INTERNAL_BEEP, OUTPUT);
        digitalWrite(INTERNAL_BEEP, HIGH);
        digitalWrite(INTERNAL_LED, HIGH);
        // Install listeners and initialize second Wiegand reader
        internal.onReceive(captureIncomingData, "internal");
        internal.onReceiveError(receivedDataError, "internal");
        internal.onStateChange(stateChanged, "internal");
        internal.begin(34, true);
#       endif

        // We define the interrupt handlers with IRAM_ATTR; it is not really
        // necessary to use ESP_INTR_FLAG_IRAM:
        // https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/intr_alloc.html
        // https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/memory-types.html
        // This is why we had to incorporate the Wiegand lib and modify it.

        // Initialize interrupt handler for first Wiegand reader pins
        attachInterrupt(digitalPinToInterrupt(EXTERNAL_D0),
                        &setExternal0PinState, CHANGE);

        attachInterrupt(digitalPinToInterrupt(EXTERNAL_D1),
                        &setExternal1PinState, CHANGE);

        // Register the initial pin state for first Wiegand reader pins
        external.setPin0State(digitalRead(EXTERNAL_D0));
        external.setPin1State(digitalRead(EXTERNAL_D1));

#       ifdef TWO_READERS
        // Initialize interrupt handler for second Wiegand reader pins
        attachInterrupt(digitalPinToInterrupt(INTERNAL_D0),
                        &setInternal0PinState, CHANGE);

        attachInterrupt(digitalPinToInterrupt(INTERNAL_D1),
                        &setInternal1PinState, CHANGE);

        // Register the initial pin state for second Wiegand reader pins
        internal.setPin0State(digitalRead(INTERNAL_D0));
        internal.setPin1State(digitalRead(INTERNAL_D1));
#       endif
    }

    void blinkError(const char* reader) {
        int beepPin;
        int ledPin;

#       ifdef TWO_READERS
        if (!strcmp(reader, "internal")) {
            beepPin = INTERNAL_BEEP;
            ledPin = INTERNAL_LED;
        } else {
            beepPin = EXTERNAL_BEEP;
            ledPin = EXTERNAL_LED;
        }
#       else
        beepPin = EXTERNAL_BEEP;
        ledPin = EXTERNAL_LED;
#       endif

        //digitalWrite(ledPin, LOW);
        digitalWrite(beepPin, LOW);
        delay(200);
        digitalWrite(beepPin, HIGH);
        delay(200);
        digitalWrite(beepPin, LOW);
        delay(200);
        digitalWrite(beepPin, HIGH);
        delay(200);
        digitalWrite(beepPin, LOW);
        delay(200);
        digitalWrite(beepPin, HIGH);
        //digitalWrite(ledPin, HIGH);
    }

    unsigned long lastFlush = 0;

    inline bool checkCardReaders(const char*& returnReaderID,
                                 unsigned long int& returnCardID) {

        if (newAccess) {
            returnReaderID = readerID;
            returnCardID = bitsToNumber(cardIDRaw, cardIDBits);
            newAccess = false;
            lastFlush = currentMillis;
            return true;
        }

        // We could run this "flush" on every loop, but since we
        // disable interrupts it is better not to. This forces
        // callback processing but, since we do not use LENGTH_ANY,
        // that is only needed to process read errors.
        if (currentMillis - lastFlush < 20) { return false; }

        lastFlush = currentMillis;

        // Only very recent versions of the arduino framework
        // for ESP32 support interrupts()/noInterrupts()
        portDISABLE_INTERRUPTS();
#       ifdef TWO_READERS
        internal.flush();
#       endif
        external.flush();
        portENABLE_INTERRUPTS();

        if (connectedMsg[0] != 0) {
            log_i("%s", connectedMsg);
            connectedMsg[0] = 0;
        }

        if (disconnectedMsg[0] != 0) {
            log_i("%s", disconnectedMsg);
            disconnectedMsg[0] = 0;
        }

        if (readErrorMsg[0] != 0) {
            log_i("%s", readErrorMsg);
            readErrorMsg[0] = 0;
            blinkError(readerID);
        }

        return false;
    }
}

void initCardReaders() { ReaderNS::initCardReaders(); }

bool checkCardReaders(const char*& readerID, unsigned long int& cardID) {
    return ReaderNS::checkCardReaders(readerID, cardID);
}

// TODO: these depend heavily on the actual model of the Wiegand readers
void blinkOk(const char* reader) {
    int beepPin;
    int ledPin;

#   ifdef TWO_READERS
    if (!strcmp(reader, "internal")) {
        beepPin = INTERNAL_BEEP;
        ledPin = INTERNAL_LED;
    } else {
        beepPin = EXTERNAL_BEEP;
        ledPin = EXTERNAL_LED;
    }
#   else
    beepPin = EXTERNAL_BEEP;
    ledPin = EXTERNAL_LED;
#   endif

    digitalWrite(ledPin, LOW);

    digitalWrite(beepPin, LOW);       // beep
    unsigned long start = millis();
    while (millis() - start < 50) { }
    digitalWrite(beepPin, HIGH);      // stop
    start = millis();
    while (millis() - start < 25) { } // wait
    digitalWrite(beepPin, LOW);       // beep
    start = millis();
    while (millis() - start < 50) { }
    digitalWrite(beepPin, HIGH);      // stop

    digitalWrite(ledPin, HIGH);
};

void blinkDeny(const char* reader) {
    int beepPin;
    int ledPin;

#   ifdef TWO_READERS
    if (!strcmp(reader, "internal")) {
        beepPin = INTERNAL_BEEP;
        ledPin = INTERNAL_LED;
    } else {
        beepPin = EXTERNAL_BEEP;
        ledPin = EXTERNAL_LED;
    }
#   else
    beepPin = EXTERNAL_BEEP;
    ledPin = EXTERNAL_LED;
#   endif

    //digitalWrite(ledPin, LOW);
    digitalWrite(beepPin, LOW);
    delay(600);
    digitalWrite(beepPin, HIGH);
    //digitalWrite(ledPin, HIGH);
};

void openDoorCommand() { openDoor(NULL); }

bool openDoor(const char* reader) {
    log_v("Opened door");
    digitalWrite(DOOR_OPEN, HIGH);
    if (NULL != reader) { blinkOk(reader); }
    delay(500);
    digitalWrite(DOOR_OPEN, LOW);
    return true;
}

bool denyToOpenDoor(const char* reader) {
    log_v("Denied to open door");
    if (NULL != reader) { blinkDeny(reader); }
    return true;
}
