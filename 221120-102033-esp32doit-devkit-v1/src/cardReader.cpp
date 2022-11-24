#include <../include/cardReader.h>
#include <Arduino.h>
// pins for card reader 1
#define PIN_D0 33
#define PIN_D1 25 
Wiegand wiegand1;

// pins for card reader 2
#define PIN2_D0 26
#define PIN2_D1 27 
Wiegand wiegand2;

// Initialize Wiegand reader
// This should be called from setup()
// Function that is called when card is read 

void processCardData(uint8_t* data, uint8_t bits){
    String card = "";
    
    uint8_t bytes = (bits+7)/8;

    // concatenate each byte from hex
    for (int i=0; i<bytes; i++){
      card += String(data[i] >> 4, HEX);
      card += String(data[i] & 0xF, HEX);
    }
    
    
    // convert hex into dec
    input = strtoul(card.c_str(), NULL, 16);
    Serial.print("We received -> ");
    Serial.println(input);

    // start search
    isSearching = true; 
}

void dbStartSearch1(uint8_t* data, uint8_t bits, const char* message){
    processCardData(data, bits);
    Serial.println("Card reader 1 was used.");
    currentCardReader = 1;
}


void dbStartSearch2(uint8_t* data, uint8_t bits, const char* message){
    processCardData(data, bits);
    Serial.println("Card reader 2 was used."); 
    currentCardReader = 2;
}


// When any of the pins have changed, update the state of the wiegand library
void pinStateChanged() {
    wiegand1.setPin0State(digitalRead(PIN_D0));
    wiegand1.setPin1State(digitalRead(PIN_D1));
    wiegand2.setPin0State(digitalRead(PIN2_D0));
    wiegand2.setPin1State(digitalRead(PIN2_D1));
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

void initCardReader(){
  
    //Install listeners and initialize Wiegand reader
    wiegand1.onReceive(dbStartSearch1, "");
    wiegand1.onReceiveError(receivedDataError, "Card reader 1 error: ");
    wiegand1.onStateChange(stateChanged, "Card reader 1 state changed: ");
    wiegand1.begin(Wiegand::LENGTH_ANY, true);

    //initialize pins as INPUT and attaches interruptions
    pinMode(PIN_D0, INPUT);
    pinMode(PIN_D1, INPUT);

    //Install listeners and initialize Wiegand reader
    wiegand2.onReceive(dbStartSearch2, "");
    wiegand2.onReceiveError(receivedDataError, "Card reader 2 error: ");
    wiegand2.onStateChange(stateChanged, "Card reader 2 state changed: ");
    wiegand2.begin(Wiegand::LENGTH_ANY, true);

    //initialize pins as INPUT and attaches interruptions
    pinMode(PIN2_D0, INPUT);
    pinMode(PIN2_D1, INPUT);
    
    attachInterrupt(digitalPinToInterrupt(PIN_D0), pinStateChanged, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_D1), pinStateChanged, CHANGE);
    
    attachInterrupt(digitalPinToInterrupt(PIN2_D0), pinStateChanged, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN2_D1), pinStateChanged, CHANGE);

    //Sends the initial pin state to the Wiegand library
    pinStateChanged();
}


void cardMaintenance(){
    noInterrupts();
    wiegand1.flush();
    wiegand2.flush();
    interrupts();
}



