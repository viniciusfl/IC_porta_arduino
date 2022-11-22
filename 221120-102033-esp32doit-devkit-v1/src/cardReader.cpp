#include <../include/cardReader.h>

#define PIN_D0 26
#define PIN_D1 27 

dataBase db;

// The object that handles the wiegand protocol
Wiegand wiegand;

// Initialize Wiegand reader
// This should be called from setup()
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



// Function that is called when card is read 
inline void dbStartSearch(uint8_t* data, uint8_t bits, const char* message){
    String card = "";
    isSearching = true;
    Serial.print("\nWe received -> ");
    Serial.print(bits);
    Serial.print("bits / ");
    uint8_t bytes = (bits+7)/8;

    // concatenate each byte from hex
    for (int i=0; i<bytes; i++){
      card += String(data[i] >> 4, HEX);
      card += String(data[i] & 0xF, HEX);
    }

    // convert hex into dec
    input = strtoul(card.c_str(), NULL, 16);
    
    // Open database if its not opened
    if(!downloading)
      if (db.openDb("/sd/banco.db")) return;
    
    // Make query and execute it
    char searchDB[100];
    sprintf(searchDB, "SELECT EXISTS(SELECT * FROM %s WHERE cartao='%lu')", db.dbNames[db.currentDB], input); //FIXME
    db.exec(searchDB);

    // Close db
    isSearching = false;
    if(!downloading) db.close();
}
