#ifndef COMMON_H
#define COMMON_H

// On every loop, we check for the amount of time passed since we last
// did some operations. Instead of calling millis() everywhere, we call
// it only once per loop and store the value here. This *might* save
// some processing (or maybe not) and *might* prevent races if millis()
// changes within a single iteration.
extern unsigned long currentMillis;

#endif
