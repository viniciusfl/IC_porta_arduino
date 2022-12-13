#ifndef TIME_KEEPING_H
#define TIME_KEEPING_H

#include <RTClib.h>

class RTC{
    public:
        void init();
        void checkSync();
    private:
        RTC_DS1307 rtc;
        unsigned long lastClockAdjustment; // variable that holds the last time we adjusted the clock
        void update();
};

#endif
