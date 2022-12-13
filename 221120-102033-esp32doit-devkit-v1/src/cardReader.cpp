#include <Arduino.h>
#include <Wiegand.h>
#include <cardReader.h>
#include <common.h>

void exposeCardData(uint8_t* data, uint8_t bits, const char* reader_id);

unsigned long bitsToNumber(uint8_t* data, uint8_t bits);

void pinStateChanged();

void stateChanged(bool plugged, const char* message);

void receivedDataError(Wiegand::DataError error, uint8_t* rawData, uint8_t rawBits, const char* message);

// pins for card reader 1
#define READER1_D0 26
#define READER1_D1 27
Wiegand wiegand1;

// pins for card reader 2
#define READER2_D0 33
#define READER2_D1 25
Wiegand wiegand2;

// We read the Wiegand data in a callback; to pass this data to the
// "normal" program flow, we use these globals:
bool newAccess; // Is there a user trying to open a door?
unsigned long int cardID; // If so, this is his ID number
int readerID; // And it came from this reader

// This reads the bitstream provided by the wiegand reader and converts
// to a single number.
unsigned long bitsToNumber(uint8_t* data, uint8_t bits){
    String number = "";
    
    uint8_t bytes = (bits+7)/8;

    // concatenate each byte from hex
    for (int i = 0; i < bytes; ++i){
      number += String(data[i] >> 4, HEX);
      number += String(data[i] & 0xF, HEX);
    }
    
    return strtoul(number.c_str(), NULL, 16);
}

// Function that is called when card is read
void exposeCardData(uint8_t* data, uint8_t bits, const char* reader_id) {
    newAccess = true;
    readerID = atoi(reader_id);
    cardID = bitsToNumber(data, bits);

    Serial.print("Card reader ");
    Serial.print(readerID);
    Serial.println(" was used.");
    Serial.print("We received -> ");
    Serial.println(cardID);
}

// When any of the pins have changed, update the state of the wiegand library
void pinStateChanged() {
    wiegand1.setPin0State(digitalRead(READER1_D0));
    wiegand1.setPin1State(digitalRead(READER1_D1));
    wiegand2.setPin0State(digitalRead(READER2_D0));
    wiegand2.setPin1State(digitalRead(READER2_D1));
}

// Notifies when a reader has been connected or disconnected.
// Instead of a message, the seconds parameter can be anything you want -- Whatever you specify on `wiegand.onStateChange()`
void stateChanged(bool plugged, const char* message) {
    Serial.print(message);
    Serial.println(plugged ? "CONNECTED" : "DISCONNECTED");
}

void receivedDataError(Wiegand::DataError error, uint8_t* rawData, uint8_t rawBits, const char* message) {
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
void initCardReader(){

    //Install listeners and initialize Wiegand reader
    wiegand1.onReceive(exposeCardData, "1");
    wiegand1.onReceiveError(receivedDataError, "Card reader 1 error: ");
    wiegand1.onStateChange(stateChanged, "Card reader 1 state changed: ");
    wiegand1.begin(Wiegand::LENGTH_ANY, true);

    //initialize pins as INPUT and attaches interruptions
    pinMode(READER1_D0, INPUT);
    pinMode(READER1_D1, INPUT);

    attachInterrupt(digitalPinToInterrupt(READER1_D0), pinStateChanged, CHANGE);
    attachInterrupt(digitalPinToInterrupt(READER1_D1), pinStateChanged, CHANGE);

    //Install listeners and initialize Wiegand reader
    wiegand2.onReceive(exposeCardData, "2");
    wiegand2.onReceiveError(receivedDataError, "Card reader 2 error: ");
    wiegand2.onStateChange(stateChanged, "Card reader 2 state changed: ");
    wiegand2.begin(Wiegand::LENGTH_ANY, true);


    //initialize pins as INPUT and attaches interruptions
    pinMode(READER2_D0, INPUT);
    pinMode(READER2_D1, INPUT);

    attachInterrupt(digitalPinToInterrupt(READER2_D0), pinStateChanged, CHANGE);
    attachInterrupt(digitalPinToInterrupt(READER2_D1), pinStateChanged, CHANGE);

    //Sends the initial pin state to the Wiegand library
    pinStateChanged();
}

unsigned long lastFlush = 0;

bool cardMaintenance(cardData* returnVal){
    // We could run this on every loop, but since we
    // disable interrupts it might be better not to.
    if (currentMillis - lastFlush < 20) return false;

    lastFlush = currentMillis;

    // Only very recent versions of the arduino framework for ESP32
    // support interrupts()/noInterrupts()
    portDISABLE_INTERRUPTS();
    wiegand2.flush();
    wiegand1.flush();
    portENABLE_INTERRUPTS();

    if (newAccess) {
        newAccess = false;
        returnVal->cardID = cardID;
        returnVal->readerID = readerID;
        return true;
    } else {
        return false;
    }
}
