#include <Wiegand.h>
#include <common.h>

void processCardData(uint8_t* data, uint8_t bits);

void dbStartSearch1(uint8_t* data, uint8_t bits, const char* message);

void dbStartSearch2(uint8_t* data, uint8_t bits, const char* message);

void pinStateChanged();

void stateChanged(bool plugged, const char* message);

void receivedDataError(Wiegand::DataError error, uint8_t* rawData, uint8_t rawBits, const char* message);

void initCardReader();

void cardMaintenance();
