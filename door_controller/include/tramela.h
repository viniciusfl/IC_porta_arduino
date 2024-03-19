#ifndef COMMON_H
#define COMMON_H

//#define USE_SD


#ifdef USE_SD
#define DISK SD
#else
#define DISK FFat
#endif

// On every loop, we check for the amount of time passed since we last
// did some operations. Instead of calling millis() everywhere, we call
// it only once per loop and store the value here. This *might* save
// some processing (or maybe not) and *might* prevent races if millis()
// changes within a single iteration.
extern unsigned long currentMillis;

extern int doorID;

extern bool sdPresent;

void checkDoor();

#include <logmanager.h>

#endif
