static const char* TAG = "card";

#include <common.h>
#include <Arduino.h>
#include <Wiegand.h>
#include <cardreader.h>

// Interrupts sometimes fail with the ControlID reader,
// so let's use polling instead.

// pins for card reader 1 (external)
#define EXTERNAL_D0  26
#define EXTERNAL_D1  27
#define EXTERNAL_BEEP 16

// pins for card reader 2 (internal)
#define INTERNAL_D0  33
#define INTERNAL_D1  25
#define INTERNAL_BEEP 32

#define USE_INTERRUPTS

// Note that, with more than one reader, trying to read two cards at
// exactly the same time will probably fail (we use a single data buffer
// for all readers). For our use case at least, that is irrelevant.

namespace ReaderNS {

    void IRAM_ATTR captureIncomingData(uint8_t* data, uint8_t bits,
                                       const char* reader);

    inline unsigned long bitsToNumber(volatile const uint8_t* data,
                                      volatile const uint8_t bits);

    void IRAM_ATTR stateChanged(bool plugged, const char* message);

    void IRAM_ATTR receivedDataError(Wiegand::DataError error,
                                     uint8_t* rawData, uint8_t rawBits,
                                     const char* message);

    Wiegand external;

    Wiegand internal;

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
        doorID = 1; //FIXME:

        // It would be possible to avoid copying, but that could break
        // if something changes in the wiegand lib implementation.
        uint8_t bytes = (bits+7)/8;
        for (int i = 0; i < bytes; ++i) {
            cardIDRaw[i] = data[i];
        }
    }

    // Notifies when a reader has been connected or disconnected.
    // Instead of a message, the seconds parameter can be anything you want --
    // Whatever you specify on `wiegand.onStateChange()`
    void IRAM_ATTR stateChanged(bool plugged, const char* message) {
        log_i("%s %s", message, plugged ? "CONNECTED" : "DISCONNECTED");
    }

    void IRAM_ATTR receivedDataError(Wiegand::DataError error,
                                     uint8_t* rawData, uint8_t rawBits,
                                     const char* message) {

        //Print value in HEX
        char buf[17]; // 64 bits, way more than enough
        uint8_t bytes = (rawBits+7)/8;
        for (int i = 0; i < bytes; ++i) {
            snprintf(buf + 2*i, 3, "%02hhx", rawData[i]);
        }

        log_i("%s %s - Raw data: %u bits / %s", message,
              Wiegand::DataErrorStr(error), rawBits, buf);
    }

void IRAM_ATTR setExternal0PinState() {
    external.setPin0State(digitalRead(EXTERNAL_D0));
}

void IRAM_ATTR setExternal1PinState() {
    external.setPin1State(digitalRead(EXTERNAL_D1));
}

void IRAM_ATTR setInternal0PinState() {
    internal.setPin0State(digitalRead(INTERNAL_D0));
}

void IRAM_ATTR setInternal1PinState() {
    internal.setPin1State(digitalRead(INTERNAL_D1));
}


    // This should be called from setup()
    void initCardReaders() {

        // Install listeners and initialize first Wiegand reader
        external.onReceive(captureIncomingData, "external");
        external.onReceiveError(receivedDataError, "External card reader error: ");
        external.onStateChange(stateChanged, "External card reader state changed: ");
        external.begin(Wiegand::LENGTH_ANY, true);

        // Install listeners and initialize second Wiegand reader
        internal.onReceive(captureIncomingData, "internal");
        internal.onReceiveError(receivedDataError, "Internal card reader error: ");
        internal.onStateChange(stateChanged, "Internal card reader state changed: ");
        internal.begin(Wiegand::LENGTH_ANY, true);

        // Initialize pins for first Wiegand reader (external) as INPUT
        pinMode(EXTERNAL_D0, INPUT);
        pinMode(EXTERNAL_D1, INPUT);
        pinMode(INTERNAL_BEEP, OUTPUT);
        digitalWrite(INTERNAL_BEEP, HIGH);

        // Initialize pins for second Wiegand reader (internal) as INPUT
        pinMode(INTERNAL_D0, INPUT);
        pinMode(INTERNAL_D1, INPUT);
        pinMode(EXTERNAL_BEEP, OUTPUT);
        digitalWrite(EXTERNAL_BEEP, HIGH);

        // Ideally, we should define the interrupt handlers with
        // ESP_INTR_FLAG_IRAM and IRAM_ATTR (or at least with only IRAM_ATTR):
        // https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/intr_alloc.html
        // https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/memory-types.html
        // However, this is probably overkill and would be complicated because
        // we would need to apply that to setPinXState & friends too.
        //
        // We use lambda functions to call the correct object from
        // the wiegand library with the appropriate parameter

#       ifdef USE_INTERRUPTS
        // Initialize interrupt handler for first Wiegand reader pins

        attachInterrupt(digitalPinToInterrupt(EXTERNAL_D0),
                        &setExternal0PinState,
                        CHANGE);

        attachInterrupt(digitalPinToInterrupt(EXTERNAL_D1),
                        &setExternal1PinState,
                        CHANGE);

        // Initialize interrupt handler for second Wiegand reader pins
        attachInterrupt(digitalPinToInterrupt(INTERNAL_D0),
                        &setInternal0PinState,
                        CHANGE);

        attachInterrupt(digitalPinToInterrupt(INTERNAL_D1),
                        &setInternal1PinState,
                        CHANGE);
#       endif

        // Register the initial pin state for first Wiegand reader pins
        external.setPin0State(digitalRead(EXTERNAL_D0));
        external.setPin1State(digitalRead(EXTERNAL_D1));

        // Register the initial pin state for second Wiegand reader pins
        internal.setPin0State(digitalRead(INTERNAL_D0));
        internal.setPin1State(digitalRead(INTERNAL_D1));
    }

    unsigned long lastFlush = 0;

    inline bool checkCardReaders(const char*& returnReaderID,
                                 unsigned long int& returnCardID) {

#       ifdef USE_INTERRUPTS

        // We could run this on every loop, but since we
        // disable interrupts it might be better not to.
        if (currentMillis - lastFlush < 20) return false;

        lastFlush = currentMillis;

        // Only very recent versions of the arduino framework
        // for ESP32 support interrupts()/noInterrupts()
        portDISABLE_INTERRUPTS();
        internal.flush();
        external.flush();
        portENABLE_INTERRUPTS();

#       else

        external.flush();
        internal.flush();
        external.setPin0State(digitalRead(EXTERNAL_D0));
        external.setPin1State(digitalRead(EXTERNAL_D1));
        internal.setPin0State(digitalRead(INTERNAL_D0));
        internal.setPin1State(digitalRead(INTERNAL_D1));

#       endif

        if (!newAccess) return false;

        returnReaderID = readerID;
        returnCardID = bitsToNumber(cardIDRaw, cardIDBits);

        // No idea why, but this almost eliminates some
        // spurious errors with the ControlID reader
        // TODO is this still necessary?
        external.reset();
        internal.reset();

        log_v("Card reader %s was used. Received card ID %lu",
              returnReaderID, returnCardID);

        newAccess = false;

        return true;
    }
}

void initCardReaders() {
    ReaderNS::initCardReaders();
}

bool checkCardReaders(const char*& readerID, unsigned long int& cardID) {
    return ReaderNS::checkCardReaders(readerID, cardID);
}

//FIXME: intelbras card reader has one cable for beep 
// and other for activating led. 
// controlid should have these two as well, but beep cable
// activate both sound and led, while led pin doesnt do nothing

// another "problem" i found is that intelbras beep fails a little if 
// turned on for 200 ms <
void blinkOk(const char* reader) {

    int pin;
    if (strcmp(reader, "internal")) {
        pin = INTERNAL_BEEP;
    } else {
        pin = EXTERNAL_BEEP;
    }

    // first beep
    digitalWrite(pin, LOW);
    delay(150);

    // pause
    digitalWrite(pin, HIGH);
    delay(150);

    // second beep
    digitalWrite(pin, LOW);
    delay(150);

    // stop beeping
    digitalWrite(pin, HIGH);
};

void blinkFail(const char* reader) {
    int pin;
    if (strcmp(reader, "internal")) {
        pin = INTERNAL_BEEP;
    } else {
        pin = EXTERNAL_BEEP;
    }

    digitalWrite(pin, LOW);
    delay(600);
    digitalWrite(pin, HIGH);
};
