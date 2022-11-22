#include <Wiegand.h>
#include <common.h>
#include <dbMaintenance.h>

// These are the pins connected to the Wiegand D0 and D1 signals.
// Ensure your board supports external Interruptions on these pins

void cardReader();

void cardMaintenance();

void pinStateChanged();

void stateChanged(bool plugged, const char* message);

inline void dbStartSearch(uint8_t* data, uint8_t bits, const char* message);