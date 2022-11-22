#include <../include/networkConnection.h>

char ssid[] = "Familia Ferraz 2.4G";

char password[] = "dogtor1966";

unsigned long lastNetCheck;

unsigned long lastReconnectAttempt = 0;
// This should be called from setup()
void WiFiInit(){
    Serial.println(F("Initializing network..."));

    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);

    delay(1000);
    
    WiFi.onEvent(WiFiStationConnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_CONNECTED);
    WiFi.onEvent(WiFiGotIP, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
    WiFi.onEvent(WiFiStationDisconnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

    WiFi.begin(ssid, password);

    Serial.println("\n\nWait for WiFi... ");


    lastNetCheck = currentMillis;
    lastReconnectAttempt = currentMillis;

    printNetStatus();
}

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


// This should be called from loop()
void checkWiFiConnection() {
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


// events

void WiFiStationConnected(WiFiEvent_t event, WiFiEventInfo_t info){
  Serial.println("Connected to AP successfully!");
}

void WiFiGotIP(WiFiEvent_t event, WiFiEventInfo_t info){
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void WiFiStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info){
  Serial.println("Disconnected from WiFi access point");
  Serial.print("WiFi lost connection. Reason: ");
  Serial.println(info.wifi_sta_disconnected.reason);
  Serial.println("Trying to Reconnect");
  WiFi.begin(ssid, password);
}

/*
#include <Ethernet.h>

byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED};

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
*/