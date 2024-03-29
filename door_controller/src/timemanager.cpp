static const char* TAG = "time";

#include <tramela.h>

#include <Arduino.h>
#include <SPI.h>
#include <stdlib.h>
#include <RTClib.h>
#include <time.h>

#include <timemanager.h>
#include <networkmanager.h>

#define READJUST_CLOCK_INTERVAL 10800 // 3 hours

// Instead of periodically checking for the time difference, as we do here,
// we might use sntp_set_time_sync_notification_cb(). However, polling is
// very simple and does not mess with the callback - who knows, maybe
// someone else needs it...

namespace TimeNS {

    const char* ntpServer = "a.st1.ntp.br";
    const long  gmtOffset_sec = -3600*3;
    const int   daylightOffset_sec = 0;

    class TimeManager {
        public:
            inline bool initOffline();
            inline bool init();
            inline void checkSync();
            inline unsigned long getCurrentTime();
            bool timeOK;
        private:
            RTC_DS1307 rtc;
            unsigned long lastClockAdjustment;
            void update();
            bool HWClockExists;
    };

    // This should be called very early from setup()
    inline bool TimeManager::initOffline() {
        timeOK = false;
        HWClockExists = false;

        if (!rtc.begin()) {
            log_i("Couldn't find HW clock, continuing with NTP only");
            return timeOK;
        }

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
            timeOK = true;
            log_i("Date/time provisionally set from hardware clock.");
        }

        return timeOK;
    }

    // This should be called from setup(), after NTP has been configured
    // (it's ok if the network is not up and/or NTP is not synchronized yet)
    inline bool TimeManager::init() {
        lastClockAdjustment = 0; // when we last adjusted the HW clock

        // getLocalTime calls localtime_r() to convert the current system
        // timestamp into "struct tm" (days, hours etc.). If the current
        // system timestamp is bogus (i.e., we did not set the clock yet),
        // this fails and it tries again, until a timeout is reached. The
        // idea is that we might have just started to run and the SNTP
        // client may have not yet received the first answer from the
        // server, so we wait a little for that. Note that if time was
        // previously set from the HW clock, this succeeds immediately,
        // even if we have not synced to NTP yet.
        struct tm timeinfo;

        if (getLocalTime(&timeinfo, 2000)) { // 2s timeout
            timeOK = true;

            // System time is set, now set the
            // HW clock for the first time
            update();

            log_i("Date/time are set!");
        }

        return timeOK;
    }

    // This should be called from loop()
    inline void TimeManager::checkSync() {
        if (currentMillis - lastClockAdjustment > READJUST_CLOCK_INTERVAL) {
            lastClockAdjustment = currentMillis;
            update();
        }
    }

    void TimeManager::update() {
        if (!HWClockExists) {
            log_d("Cannot update HW clock time (HW clock not found).");
            return;
        }

        time_t now;
        time(&now);

        unsigned long systemtime = now;
        unsigned long hwclocktime = rtc.now().unixtime();

        // These are unsigned, so the result is always
        // positive, but we need to watch out for overflows.
        if (systemtime - hwclocktime >= 10
                    and hwclocktime - systemtime >= 10) {

            log_v("Updating hardware clock time (%lu -> %lu)",
                  hwclocktime, systemtime);

            rtc.adjust(DateTime(systemtime));
        }
    }

    inline unsigned long TimeManager::getCurrentTime() {
        if (timeOK) {
            time_t now;
            time(&now);
            return now;
        }
        return 0; // Time not set
    }

    TimeManager hwclock;
}

bool initTimeOffline() { return TimeNS::hwclock.initOffline(); }

bool initTime() { return TimeNS::hwclock.init(); }

void configNTP() {
    // initialize esp32 sntp client, which calls settimeofday
    // periodically; this performs a DNS lookup and an NTP
    // request, so it takes some time. If the network is not
    // already up when this is called, the system retries later.
    // I suppose there is no harm in calling this every time the
    // network connects, even if it is unnecessary.
    configTime(TimeNS::gmtOffset_sec, TimeNS::daylightOffset_sec,
               TimeNS::ntpServer);
}

void checkTimeSync() { TimeNS::hwclock.checkSync(); }

unsigned long getTime() { return TimeNS::hwclock.getCurrentTime(); }

bool timeIsValid() { return TimeNS::hwclock.timeOK; }
