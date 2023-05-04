static const char* TAG = "time";

#include <common.h>
#include <SPI.h>
#include <stdlib.h>
#include <RTClib.h>
#include <time.h>
#include <timemanager.h>
#include <networkmanager.h>

#define READJUST_CLOCK_INTERVAL 60000 // 10s, just for testing; a good
                                      // value is 10800 (3 hours)

// Instead of periodically checking for the time difference, as we do here,
// we might use sntp_set_time_sync_notification_cb(). However, polling is
// very simple and does not mess with the callback - who knows, maybe
// someone else needs it...

namespace TimeNS {

    const char* ntpServer = "a.ntp.br";
    const long  gmtOffset_sec = -3600*3;
    const int   daylightOffset_sec = 0;

    inline void configNTP() {
        // initialize esp32 sntp client, which calls settimeofday
        // periodically; this performs a DNS lookup and an NTP
        // request, so it takes some time. If the network is not
        // already up when this is called, the system retries later.
        // I suppose there is no harm in calling this every time the
        // network connects, even if it is unnecessary.
        configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    }

    class TimeManager {
        public:
            inline void init();
            inline void checkSync();
            inline unsigned long getCurrentTime();
        private:
            RTC_DS1307 rtc;
            unsigned long lastClockAdjustment; // variable that holds the last time we adjusted the clock
            void update();
            bool HWClockExists;
    };

    // This should be called from setup(), after NTP has been configured
    // (it's ok if the network is not up and/or NTP is not synchronized yet)
    inline void TimeManager::init() {
        lastClockAdjustment = 0; // when we last adjusted the HW clock

        if (!rtc.begin()) {
            HWClockExists = false;
            log_i("Couldn't find HW clock, continuing with NTP only");
        } else {
            HWClockExists = true;

            // If I understand things correctly, the hardware RTC starts life in
            // state "stopped", because it has no idea what the time is. After
            // you set the time for the first time, it enters state "started"
            // and keeps time. This start/stop operation is controlled by bit 7
            // of register 0 (if the bit is zero, the clock is started), which
            // is what "isrunning()" checks. Since register 0 is the seconds
            // register, we set it to zero when we call adjust() with a valid
            // time for the first time (if bit 7 were on, the number of seconds
            // would be >= 64).
            // https://forum.arduino.cc/t/ds1307-real-time-clock-halts-on-power-off/206537/2

            // If we ever set the hardware RTC time before, we may set the
            // system time from it and adjust with NTP later. If not, we need
            // to make sure the NTP date has been set before starting.
            if (rtc.isrunning()) {
                struct timeval tv;

                tv.tv_sec = rtc.now().unixtime();
                tv.tv_usec = 0;

                settimeofday(&tv, NULL);
            }
        }

        // getLocalTime calls localtime_r() to convert the current system
        // timestamp into "struct tm" (days, hours etc.). If the current
        // system timestamp is bogus (i.e., we did not set the clock yet),
        // this fails and it tries again, until a timeout is reached. The
        // idea is that we might have just started to run and the SNTP
        // client may have not yet received the first answer from the
        // server, so we wait a little for that.
        struct tm timeinfo;
        bool timeOK = false;

        int attempts = 0;
        while (!timeOK) {
            if (getLocalTime(&timeinfo, 30000)) { // 30s timeout
                timeOK = true;
            } else if (++attempts > 3) {
                log_e("Failed to obtain time from both HW clock "
                      "and network too many times, restarting");
                ESP.restart(); // Desperate times call for desperate measures
            } else {
                log_i("Failed to obtain time from both HW clock "
                      "and network, resetting network");

                netReset();
            }
        }

        // System time is set, now set the
        // HW clock for the first time
        update();

        log_v("Date/time are set!");
    }

    // This should be called from loop()
    inline void TimeManager::checkSync() {
        if (currentMillis - lastClockAdjustment > READJUST_CLOCK_INTERVAL) {
            lastClockAdjustment = currentMillis;
            update();
        }
    }

    void printDate(DateTime moment) {
#       if CORE_DEBUG_LEVEL >= ARDUHAL_LOG_LEVEL_VERBOSE

        char daysOfTheWeek[15][15] = {"domingo", "segunda", "terça",
                                      "quarta", "quinta", "sexta", "sabado"};

        char buf[192];
        snprintf(buf, 192,
                 "%u/%u/%u (%s) %u:%u:%u UTC\nsince midnight 1/1/1970 = "
                 "%us (%u days)", moment.year(), moment.month(),
                 moment.day(), daysOfTheWeek[moment.dayOfTheWeek()],
                 moment.hour(), moment.minute(), moment.second(),
                 moment.unixtime(), moment.unixtime() / 86400L);

        log_v("%s", buf);

#       endif
    }

    void TimeManager::update() {
        if (!HWClockExists) return;

        time_t now;
        time(&now);

        unsigned long systemtime = now;
        unsigned long hwclocktime = rtc.now().unixtime();

        log_v("System date: %lu", systemtime);
        printDate(DateTime(systemtime));
        log_v("HW clock date: %lu", hwclocktime);
        printDate(DateTime(hwclocktime));
        log_v("Difference: %lu", systemtime - hwclocktime);

        if (systemtime - hwclocktime >= 10) {
            log_v("Updating hardware clock time");
            rtc.adjust(DateTime(systemtime));
        }
    }

    inline unsigned long TimeManager::getCurrentTime() {
        time_t now;
        time(&now);
        return now;
    }

    TimeManager hwclock;
}

void initTime() { TimeNS::hwclock.init(); }

void configNTP() { TimeNS::configNTP(); }

void checkTimeSync() { TimeNS::hwclock.checkSync(); }

unsigned long getTime() { return TimeNS::hwclock.getCurrentTime(); }
