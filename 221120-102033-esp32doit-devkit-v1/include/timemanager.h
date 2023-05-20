#ifndef TIME_MANAGER_H
#define TIME_MANAGER_H

#include <RTClib.h>

bool initTimeOffline();
bool timeIsValid();
bool initTime();
void configNTP();
void checkTimeSync();
unsigned long getTime();

#endif
