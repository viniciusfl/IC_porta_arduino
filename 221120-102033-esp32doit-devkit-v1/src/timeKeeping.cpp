#include <../include/timeKeeping.h>

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
    if (waitingForNTPreply) {
        processNTPreply();
    } else if (currentMillis - lastClockAdjustment > 5000) {
        // Actually, this is the time of the last *attempt* to adjust
        // the clock, but that's ok: If it fails, we do nothing special,
        // just wait for READJUST_CLOCK_INTERVAL again.
        lastClockAdjustment = currentMillis;
        sendNTPquery(); // send an NTP packet to a time server
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


// FIXME there is a remote chance someone will spoof a packet and alter
//       the clock. Is this relevant? Does the NTP library handle this?
void RTC::processNTPreply(){
    // Timeout
    
    if (currentMillis - lastClockAdjustment > NTP_WAIT_TIMEOUT) {
        udp.stop();
        waitingForNTPreply = false;
        return;
    }

    if (!udp.parsePacket()) return; // still waiting...

    // We've received a packet, read the data from it
    udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer
    udp.stop();
    waitingForNTPreply = false;

    // the timestamp starts at byte 40 of the received packet and is
    // four bytes, or two words, long. First, extract the two words:
    unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);

    // combine the four bytes (two words) into a long integer;
    // this is NTP time (seconds since Jan 1 1900):
    unsigned long nettime = highWord << 16 | lowWord;

    // now convert NTP time into unix time (seconds since
    // Jan 1 1970 UTC). In seconds, the difference is 2208988800:
    const unsigned long seventyYears = 2208988800UL;
    nettime = nettime - seventyYears;

    unsigned long rtctime = rtc.now().unixtime();

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
    
}

// send an NTP request to the time server
void RTC::sendNTPquery(){
    // set all bytes in the buffer to 0
    memset(packetBuffer, 0, NTP_PACKET_SIZE);

    // Initialize values needed to form NTP request
    // (see URL above for details on the packets)
    // FIXME which URL?
    packetBuffer[0] = 0b11100011;   // LI, Version, Mode
    packetBuffer[1] = 0;     // Stratum, or type of clock
    packetBuffer[2] = 6;     // Polling Interval
    packetBuffer[3] = 0xEC;  // Peer Clock Precision
    // 8 bytes of zero for Root Delay & Root Dispersion
    packetBuffer[12]  = 49;
    packetBuffer[13]  = 0x4E;
    packetBuffer[14]  = 49;
    packetBuffer[15]  = 52;

    // all NTP fields have been given values, now
    // you can send a packet requesting a timestamp:
    udp.begin(LOCALPORT); // prepare the connection to the NTP server
    udp.beginPacket(TIME_SERVER, 123); // NTP requests are to port 123
    udp.write(packetBuffer, NTP_PACKET_SIZE);
    udp.endPacket();
    waitingForNTPreply = true;
}
