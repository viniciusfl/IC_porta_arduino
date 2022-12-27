#include <Arduino.h>
#include <Wiegand.h>
#include <cardreader.h>
#include <common.h>

// Interrupts sometimes fail with the ControlID reader,
// so let's use polling instead.
//#define USE_INTERRUPTS

// pins for card reader 1 (external)
#define EXTERNAL_D0 26
#define EXTERNAL_D1 27

// pins for card reader 2 (internal)
#define INTERNAL_D0 33
#define INTERNAL_D1 25

// Note that, with more than one reader, trying to read two cards at
// exactly the same time will probably fail (we use a single data buffer
// for all readers). For our use case at least, that is irrelevant.

namespace ReaderNS {

    void captureIncomingData(uint8_t* data, uint8_t bits, const char* reader);

    inline unsigned long bitsToNumber(volatile const uint8_t* data,
                                      volatile const uint8_t bits);

    void stateChanged(bool plugged, const char* message);

    void receivedDataError(Wiegand::DataError error, uint8_t* rawData,
                           uint8_t rawBits, const char* message);

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

        String number = "";

        uint8_t bytes = (bits+7)/8;

        // concatenate each byte from hex
        for (int i = 0; i < bytes; ++i) {
          number += String(data[i] >> 4, HEX);
          number += String(data[i] & 0xF, HEX);
        }

        return strtoul(number.c_str(), NULL, 16);
    }

    // Function that is called when card is read; we do not call
    // bitsToNumber() here to make it as fast as possible (this
    // is called with interrupts disabled).
    void captureIncomingData(uint8_t* data, uint8_t bits, const char* reader) {
        newAccess = true;
        readerID = reader;
        cardIDBits = bits;
        doorID = 1;

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
    void stateChanged(bool plugged, const char* message) {
        Serial.print(message);
        Serial.println(plugged ? "CONNECTED" : "DISCONNECTED");
    }

    void receivedDataError(Wiegand::DataError error, uint8_t* rawData,
                           uint8_t rawBits, const char* message) {

        Serial.print(message);
        Serial.print(Wiegand::DataErrorStr(error));
        Serial.print(" - Raw data: ");
        Serial.print(rawBits);
        Serial.print("bits / ");

        //Print value in HEX
        uint8_t bytes = (rawBits+7)/8;
        for (int i=0; i<bytes; i++) {
            Serial.print(rawData[i] >> 4, 16);
            Serial.print(rawData[i] & 0xF, 16);
        }
        Serial.println();
    }

    // This should be called from setup()
    void initCardReaders() {

        // Install listeners and initialize first Wiegand reader
        external.onReceive(captureIncomingData, "1");
        external.onReceiveError(receivedDataError, "External card reader error: ");
        external.onStateChange(stateChanged, "External card reader state changed: ");
        external.begin(Wiegand::LENGTH_ANY, true);

        // Install listeners and initialize second Wiegand reader
        internal.onReceive(captureIncomingData, "2");
        internal.onReceiveError(receivedDataError, "Internal card reader error: ");
        internal.onStateChange(stateChanged, "Internal card reader state changed: ");
        internal.begin(Wiegand::LENGTH_ANY, true);

        // Initialize pins for first Wiegand reader (external) as INPUT
        pinMode(EXTERNAL_D0, INPUT);
        pinMode(EXTERNAL_D1, INPUT);

        // Initialize pins for second Wiegand reader (internal) as INPUT
        pinMode(INTERNAL_D0, INPUT);
        pinMode(INTERNAL_D1, INPUT);

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
                        [](){external.setPin0State(digitalRead(EXTERNAL_D0));},
                        CHANGE);

        attachInterrupt(digitalPinToInterrupt(EXTERNAL_D1),
                        [](){external.setPin1State(digitalRead(EXTERNAL_D1));},
                        CHANGE);

        // Initialize interrupt handler for second Wiegand reader pins
        attachInterrupt(digitalPinToInterrupt(INTERNAL_D0),
                        [](){internal.setPin0State(digitalRead(INTERNAL_D0));},
                        CHANGE);

        attachInterrupt(digitalPinToInterrupt(INTERNAL_D1),
                        [](){internal.setPin1State(digitalRead(INTERNAL_D1));},
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

    inline bool checkCardReaders(int& returnReaderID,
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

        internal.flush();
        external.flush();
        external.setPin0State(digitalRead(EXTERNAL_D0));
        external.setPin1State(digitalRead(EXTERNAL_D1));
        internal.setPin0State(digitalRead(INTERNAL_D0));
        internal.setPin1State(digitalRead(INTERNAL_D1));

#       endif

        if (!newAccess) return false;

        returnReaderID = atoi(readerID);
        returnCardID = bitsToNumber(cardIDRaw, cardIDBits);

        // No idea why, but this almost eliminates some
        // spurious errors with the ControlID reader
        external.reset();
        internal.reset();

#       ifdef DEBUG
        if (returnReaderID == 1) {
            Serial.print("External ");
        } else {
            Serial.print("Internal ");
        }
        Serial.print("card reader was used.");
        Serial.print("We received card ID -> ");
        Serial.println(returnCardID);
#       endif

        newAccess = false;

        return true;
    }
}

void initCardReaders() {
    ReaderNS::initCardReaders();
}

bool checkCardReaders(int& readerID, unsigned long int& cardID) {
    return ReaderNS::checkCardReaders(readerID, cardID);
}

// TODO: implement this :)
void blinkOk(int reader) {

};

void blinkFail(int reader) {};
