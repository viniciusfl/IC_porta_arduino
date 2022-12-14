#include <Arduino.h>
#include <Wiegand.h>
#include <cardreader.h>
#include <common.h>

// Note that, with more than one reader, trying to read two cards at
// exactly the same time will probably fail (we use a single data buffer
// for all readers). For our use case at least, that is irrelevant.

void captureIncomingData(uint8_t* data, uint8_t bits, const char* reader);

unsigned long bitsToNumber(volatile const uint8_t*, volatile const uint8_t);

void stateChanged(bool plugged, const char* message);

void receivedDataError(Wiegand::DataError error, uint8_t* rawData,
                       uint8_t rawBits, const char* message);

// pins for card reader 1
#define READER1_D0 26
#define READER1_D1 27
Wiegand wiegand1;

// pins for card reader 2
#define READER2_D0 33
#define READER2_D1 25
Wiegand wiegand2;

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
unsigned long bitsToNumber(volatile const uint8_t* data,
                           volatile const uint8_t bits){

    String number = "";
    
    uint8_t bytes = (bits+7)/8;

    // concatenate each byte from hex
    for (int i = 0; i < bytes; ++i){
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

    // It would be possible to avoid copying, but that could break
    // if something changes in the wiegand lib implementation.
    uint8_t bytes = (bits+7)/8;
    for (int i = 0; i < bytes; ++i){
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
void initCardReaders(){

    // Install listeners and initialize first Wiegand reader
    wiegand1.onReceive(captureIncomingData, "1");
    wiegand1.onReceiveError(receivedDataError, "Card reader 1 error: ");
    wiegand1.onStateChange(stateChanged, "Card reader 1 state changed: ");
    wiegand1.begin(Wiegand::LENGTH_ANY, true);

    // Install listeners and initialize second Wiegand reader
    wiegand2.onReceive(captureIncomingData, "2");
    wiegand2.onReceiveError(receivedDataError, "Card reader 2 error: ");
    wiegand2.onStateChange(stateChanged, "Card reader 2 state changed: ");
    wiegand2.begin(Wiegand::LENGTH_ANY, true);

    // Initialize pins for first Wiegand reader as INPUT
    pinMode(READER1_D0, INPUT);
    pinMode(READER1_D1, INPUT);

    // Initialize pins for second Wiegand reader as INPUT
    pinMode(READER2_D0, INPUT);
    pinMode(READER2_D1, INPUT);

    // Ideally, we should define the interrupt handlers with ESP_INTR_FLAG_IRAM
    // and IRAM_ATTR (or at least with only IRAM_ATTR):
    // https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/intr_alloc.html
    // https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/memory-types.html
    // However, this is probably overkill and would be complicated because
    // we would need to apply that to setPinXState & friends too.
    //
    // We use lambda functions to call the correct object from
    // the wiegand library with the appropriate parameter

    // Initialize interrupt handler for first Wiegand reader pins
    attachInterrupt(digitalPinToInterrupt(READER1_D0),
                    [](){wiegand1.setPin0State(digitalRead(READER1_D0));},
                    CHANGE);

    attachInterrupt(digitalPinToInterrupt(READER1_D1),
                    [](){wiegand1.setPin1State(digitalRead(READER1_D1));},
                    CHANGE);

    // Initialize interrupt handler for second Wiegand reader pins
    attachInterrupt(digitalPinToInterrupt(READER2_D0),
                    [](){wiegand2.setPin0State(digitalRead(READER2_D0));},
                    CHANGE);

    attachInterrupt(digitalPinToInterrupt(READER2_D1),
                    [](){wiegand2.setPin1State(digitalRead(READER2_D1));},
                    CHANGE);

    // Register the initial pin state for first Wiegand reader pins
    wiegand1.setPin0State(digitalRead(READER1_D0));
    wiegand1.setPin1State(digitalRead(READER1_D1));

    // Register the initial pin state for second Wiegand reader pins
    wiegand2.setPin0State(digitalRead(READER2_D0));
    wiegand2.setPin1State(digitalRead(READER2_D1));
}

unsigned long lastFlush = 0;

bool checkCardReaders(int& returnReaderID, unsigned long int& returnCardID){
    // We could run this on every loop, but since we
    // disable interrupts it might be better not to.
    if (currentMillis - lastFlush < 20) return false;

    lastFlush = currentMillis;

    // Only very recent versions of the arduino framework
    // for ESP32 support interrupts()/noInterrupts()
    portDISABLE_INTERRUPTS();
    wiegand2.flush();
    wiegand1.flush();
    portENABLE_INTERRUPTS();

    if (!newAccess) return false;

    returnReaderID = atoi(readerID);
    returnCardID = bitsToNumber(cardIDRaw, cardIDBits);

    Serial.print("Card reader ");
    Serial.print(returnReaderID);
    Serial.println(" was used.");
    Serial.print("We received -> ");
    Serial.println(returnCardID);

    newAccess = false;

    return true;
}