#ifndef TIME_MANAGER_H
#define TIME_MANAGER_H

#include <RTClib.h>

class TimeManager {
    public:
        void init();
        void checkSync();
    private:
        RTC_DS1307 rtc;
        unsigned long lastClockAdjustment; // variable that holds the last time we adjusted the clock
        void update();
};

#endif
