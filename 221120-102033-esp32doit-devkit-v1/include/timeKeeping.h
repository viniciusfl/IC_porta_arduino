#ifndef TIME_KEEPING_H
#define TIME_KEEPING_H

#include <common.h>
#include <SPI.h>
#include <stdlib.h>
#include <WiFiUdp.h>
#include <RTClib.h>
#include <time.h>

class RTC{
    public:
        RTC();
        void initRTC();
        void checkRTCsync();
        void updateRTC();
        void printDate(DateTime moment);
        unsigned long int unixTime();

    private:
        RTC_DS1307 rtc;
        unsigned long lastClockAdjustment; // variable that holds the last time we adjusted the clock
};

#endif