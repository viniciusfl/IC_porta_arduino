#define CHECK_NET_INTERVAL 30000 // 30s
unsigned long lastNetCheck;

#ifdef USE_WIFI
#define RECONNECT_INTERVAL 30000 // 30s
unsigned long lastReconnectAttempt = 0;
#endif

#ifdef USE_WIFI

#include <WiFi.h>

char ssid[] = "Familia Ferraz 2.4G";
char password[] = "dogtor1966";

// This should be called from setup()
inline void initNetwork() {
    Serial.println(F("Initializing network..."));

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    WiFi.begin(ssid, password);

    // Wait up to 10s for the connection.
    // FIXME it would be better to use events, check
    // https://randomnerdtutorials.com/esp32-useful-wi-fi-functions-arduino/
    while (WiFi.status() != WL_CONNECTED && millis() - currentMillis < 10000) {
        delay(500);
    }

    lastNetCheck = currentMillis;
    lastReconnectAttempt = currentMillis;

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println(F("Failed to connect to wifi network"));
    } else {
        Serial.println(F("Connected to wifi network"));
    }

#   ifdef DEBUG
    printNetStatus();
#   endif
}

#ifdef DEBUG
void printNetStatus() {
    byte macBuffer[6];  // create a buffer to hold the MAC address
    WiFi.macAddress(macBuffer); // fill the buffer
    Serial.print(F("The MAC address is: "));
    for (byte octet = 0; octet < 6; ++octet) {
        Serial.print(macBuffer[octet], HEX);
        if (octet < 5) {
            Serial.print(':');
        }
    }
    Serial.println("");

    if ((WiFi.status() != WL_CONNECTED)) {
        Serial.println(F("Not connected to wifi."));
        return;
    }

    Serial.println("");
    Serial.print(F("The IP address is: "));
    Serial.println(WiFi.localIP());
    Serial.print(F("The wifi network is: "));
    Serial.println(WiFi.SSID());
}
#endif

// This should be called from loop()
inline void checkNetConnection() {
    if ((WiFi.status() != WL_CONNECTED)
        && (currentMillis - lastReconnectAttempt > RECONNECT_INTERVAL)) {

        WiFi.disconnect();
        WiFi.reconnect();
        lastReconnectAttempt = currentMillis;
    }

    if (currentMillis - lastNetCheck > CHECK_NET_INTERVAL) {
        printNetStatus();
        lastNetCheck = currentMillis;
    }
}

#else

#include <SPI.h>
#include <Ethernet.h>

byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED};

// Nao estamos usando, vai com DHCP
//byte ip[] = {192, 168, 48, 129};
//byte mask[] = {255, 255, 192, 0};
//byte dns[] = {192, 168, 45, 11};
//byte gw[] = {192, 168, 45, 1};

// This should be called from setup()
inline void initNetwork() {
    Serial.println(F("Initializing network..."));
    Ethernet.init(10); // This is the default

#   ifdef DEBUG
    if (Ethernet.hardwareStatus() == EthernetNoHardware) {
        Serial.println(F("Ethernet shield was not found."));
        return;
    }

    if (Ethernet.hardwareStatus() == EthernetW5100) {
        Serial.println(F("W5100 Ethernet controller detected."));
    }
    else if (Ethernet.hardwareStatus() == EthernetW5200) {
        Serial.println(F("W5200 Ethernet controller detected."));
    }
    else if (Ethernet.hardwareStatus() == EthernetW5500) {
        Serial.println(F("W5500 Ethernet controller detected."));
    }
#   endif

    //Ethernet.setMACAddress(mac);
    //Ethernet.begin(mac, ip, dns, gw, mask);
    int status = Ethernet.begin(mac);

    if (status == 0) {
        Serial.println(F("Failed to configure Ethernet using DHCP"));
    } else {
        Serial.println(F("Connected to network."));
    }

    lastNetCheck = currentMillis;

#   ifdef DEBUG
    printNetStatus();
#   endif
}

#ifdef DEBUG
void printNetStatus() {
    byte macBuffer[6];  // create a buffer to hold the MAC address
    Ethernet.MACAddress(macBuffer); // fill the buffer
    Serial.print(F("The MAC address is: "));
    for (byte octet = 0; octet < 6; ++octet) {
        Serial.print(macBuffer[octet], HEX);
        if (octet < 5) {
            Serial.print(':');
        }
    }

    if (Ethernet.linkStatus() == LinkOFF) {
        Serial.println(F("Ethernet cable is not connected."));
        return;
    }

    Serial.println("");
    Serial.print(F("The IP address is: "));
    Serial.println(Ethernet.localIP());
    Serial.print(F("The DNS server is: "));
    Serial.println(Ethernet.dnsServerIP());
}
#endif

// This should be called from loop()
inline void checkNetConnection() {
    if (currentMillis - lastNetCheck > CHECK_NET_INTERVAL) {
#       ifdef DEBUG
        printNetStatus();
#       endif        
        Ethernet.maintain();
        lastNetCheck = currentMillis;
    }
}

#endif
