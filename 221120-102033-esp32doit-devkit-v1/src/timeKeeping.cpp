#include <../include/timeKeeping.h>
#define DEBUGRTC

#define LOCALPORT 8888       // local port to listen for UDP packets

#define READJUST_CLOCK_INTERVAL 60000 // 10s, just for testing; a good
                                      // value is 10800 (3 hours)

#define NTP_WAIT_TIMEOUT 10000 // 10s, waaay more than enough

#define TIME_SERVER "a.ntp.br"

#define NTP_PACKET_SIZE 48 // NTP time stamp is in the first 48 bytes of the message


const char* ntpServer = "a.ntp.br";
const long  gmtOffset_sec = 0;
const int   daylightOffset_sec = -3600*3;

// This should be called from setup()
RTC::RTC(){
    bool waitingForNTPreply = false;
    unsigned long lastClockAdjustment = 0; // variable that holds the last time we adjusted the clock
}

void RTC::initRTC(){
    if (!rtc.begin()) {
        Serial.println("Couldn't find RTC");
        Serial.flush();
        while (true) delay(10);
    }

    // esp32 internal rtc
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

    // The RTC may be started and stopped by setting bit 7 of register 0
    // to 0 or 1 respectively. Register 0 is the seconds register, which
    // means we set it to zero (start) when we call adjust() with a valid
    // time for the first time (if bit 7 were on, the number of seconds
    // would be >= 64).
    // https://forum.arduino.cc/t/ds1307-real-time-clock-halts-on-power-off/206537/2
    if (! rtc.isrunning()) {
        Serial.println("RTC NOT running, starting it with a bogus date");
        // When time needs to be set on a new device, this line
        // sets the RTC to the date & time this sketch was compiled
        rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
        // It is possible to use an explicit date & time. For example,
        // to set January 21, 2014 at 3am you would call:
        // rtc.adjust(DateTime(2014, 1, 21, 3, 0, 0));
    }

    Serial.println("RTC is ready!");
}


// This should be called from loop()
void RTC::checkRTCsync() {
    if (currentMillis - lastClockAdjustment > READJUST_CLOCK_INTERVAL) {
        // Actually, this is the time of the last *attempt* to adjust
        // the clock, but that's ok: If it fails, we do nothing special,
        // just wait for READJUST_CLOCK_INTERVAL again.
        lastClockAdjustment = currentMillis;
        updateRTC(); 
    }
}

void RTC::printDate(DateTime moment){
    char daysOfTheWeek[15][15] = {"domingo", "segunda", "terça", "quarta", "quinta", "sexta", "sabado"};
    Serial.print(moment.year(), DEC);
    Serial.print('/');
    Serial.print(moment.month(), DEC);
    Serial.print('/');
    Serial.print(moment.day(), DEC);
    Serial.print(" (");
    Serial.print(daysOfTheWeek[moment.dayOfTheWeek()]);
    Serial.print(") ");
    Serial.print(moment.hour(), DEC);
    Serial.print(':');
    Serial.print(moment.minute(), DEC);
    Serial.print(':');
    Serial.print(moment.second(), DEC);
    Serial.println();
    Serial.print(" since midnight 1/1/1970 = ");
    Serial.print(moment.unixtime());
    Serial.print("s = ");
    Serial.print(moment.unixtime() / 86400L);
    Serial.print("d");
    Serial.println(" UTC"); // Let's always use UTC
}

void RTC::updateRTC(){
    time_t now;
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        Serial.println("Failed to obtain time");
        return;
    }
    time(&now);

    unsigned long nettime = now + daylightOffset_sec;
    unsigned long rtctime = rtc.now().unixtime();
#   ifdef DEBUGRTC
    Serial.print("NTP date: ");
    Serial.println(nettime);
    printDate(DateTime(nettime));
    Serial.print("RTC date: ");
    Serial.println(rtctime);
    printDate(DateTime(rtctime));
    Serial.print("Difference: ");
    Serial.println(nettime - rtctime);

    if(nettime - rtctime >= 2){
        Serial.println("Using NTP to update RTC time");
        rtc.adjust(DateTime(nettime));
    }
#   endif
}

unsigned long int RTC::unixTime(){
    return rtc.now().unixtime();
}
