/*
 * Example on how to use the Wiegand reader library with interruptions.
 */

#include <Wiegand.h>

// These are the pins connected to the Wiegand D0 and D1 signals.
// Ensure your board supports external Interruptions on these pins
#define PIN_D0 26
#define PIN_D1 27 

// The object that handles the wiegand protocol
Wiegand wiegand;

// Initialize Wiegand reader
void initCardReader(){
  //Install listeners and initialize Wiegand reader
  wiegand.onReceive(dbStartSearch, "Card read error: ");
  //wiegand.onReceiveError(receivedDataError, "Card read error: ");
  wiegand.onStateChange(stateChanged, "State changed: ");
  wiegand.begin(Wiegand::LENGTH_ANY, true);

  //initialize pins as INPUT and attaches interruptions
  pinMode(PIN_D0, INPUT);
  pinMode(PIN_D1, INPUT);

  //Sends the initial pin state to the Wiegand library
  pinStateChanged();
}


void cardMaintenance(){
  wiegand.flush();
  // Check for changes on the the wiegand input pins
  wiegand.setPin0State(digitalRead(PIN_D0));
  wiegand.setPin1State(digitalRead(PIN_D1));
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
