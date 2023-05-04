#ifndef TIME_MANAGER_H
#define TIME_MANAGER_H

#include <RTClib.h>

bool initTimeOffline();
void initTime();
void configNTP();
void checkTimeSync();
unsigned long getTime();

#endif
