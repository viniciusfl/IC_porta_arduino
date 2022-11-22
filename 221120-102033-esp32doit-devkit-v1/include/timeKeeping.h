#include <common.h>
#include <SPI.h>
#include <stdlib.h>
#include <WiFiUdp.h>
#include <RTClib.h>

#define LOCALPORT 8888       // local port to listen for UDP packets

#define READJUST_CLOCK_INTERVAL 10000 // 10s, just for testing; a good
                                      // value is 10800 (3 hours)

#define NTP_WAIT_TIMEOUT 10000 // 10s, waaay more than enough

#define TIME_SERVER "a.ntp.br"

#define NTP_PACKET_SIZE 48 // NTP time stamp is in the first 48 bytes of the message


class RTC{
    public:
        RTC();
        void initRTC();
        void checkRTCsync();
        void processNTPreply();
        void sendNTPquery();
        void printDate(DateTime moment);
        RTC_DS1307 rtc;
        bool waitingForNTPreply;
        unsigned long lastClockAdjustment; // variable that holds the last time we adjusted the clock
        byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets
        WiFiUDP udp; // A UDP instance to let us send and receive packets over UDP

};