
#include <../include/common.h>
#include <../include/timeKeeping.h>
#include <../include/dbMaintenance.h>
#include <Arduino.h>
#include <Wiegand.h>
// #include <SPI.h>

//#define TESTING
//#define DEBUG
//#define USE_WIFI

// pins for card reader 1
#define PIN_D0 26
#define PIN_D1 27 

// The object that handles the wiegand protocol

void cardReader();

void cardMaintenance();

Wiegand wiegand;

RTC relogio = RTC();

dataBase db = dataBase();

void setup() {

    Serial.begin(115200);

    // wait for serial port to connect. Needed for native USB port only
    while (!Serial) { ; }

    Serial.println(F("Start program"));

    WiFiInit();

    relogio.initRTC();
    
    relogio = RTC();

    db.initDataBase();
    
    currentMillis = millis();
    
    cardReader();

}

void loop() {
    //cardMaintenance();
    currentMillis = millis();
    //checkWiFiConnection();
    //relogio.checkRTCsync();
    db.dbMaintenance();
    cardMaintenance();
}

// Initialize Wiegand reader
// This should be called from setup()
// Function that is called when card is read 
void dbStartSearch(uint8_t* data, uint8_t bits, const char* message){

    String card = "";
    
    uint8_t bytes = (bits+7)/8;

    // concatenate each byte from hex
    for (int i=0; i<bytes; i++){
      card += String(data[i] >> 4, HEX);
      card += String(data[i] & 0xF, HEX);
    }
    
    // convert hex into dec
    db.input = strtoul(card.c_str(), NULL, 16);
    isSearching = true; // maybe put this inside db class
  
}



// When any of the pins have changed, update the state of the wiegand library
void pinStateChanged() {
  wiegand.setPin0State(digitalRead(PIN_D0));
  wiegand.setPin1State(digitalRead(PIN_D1));
}

// Notifies when a reader has been connected or disconnected.
// Instead of a message, the seconds parameter can be anything you want -- Whatever you specify on `wiegand.onStateChange()`
void stateChanged(bool plugged, const char* message) {
    Serial.print(message);
    Serial.println(plugged ? "CONNECTED" : "DISCONNECTED");
}


void cardReader(){
  
  //Install listeners and initialize Wiegand reader
  wiegand.onReceive(dbStartSearch, "");
  //wiegand.onReceiveError(receivedDataError, "Card read error: ");
  wiegand.onStateChange(stateChanged, "Card reader state changed: ");
  wiegand.begin(Wiegand::LENGTH_ANY, true);

  //initialize pins as INPUT and attaches interruptions
  pinMode(PIN_D0, INPUT);
  pinMode(PIN_D1, INPUT);

  attachInterrupt(digitalPinToInterrupt(PIN_D0), pinStateChanged, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_D1), pinStateChanged, CHANGE);
  
  //Sends the initial pin state to the Wiegand library
  pinStateChanged();
}


void cardMaintenance(){
  noInterrupts();
  wiegand.flush();
  interrupts();
}



