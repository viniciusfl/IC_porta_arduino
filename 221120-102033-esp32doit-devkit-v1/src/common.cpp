#include <common.h>

unsigned long currentMillis;

bool searching = false;
unsigned long int currentCardID;
int currentCardReader;

RTC hwclock = RTC();
