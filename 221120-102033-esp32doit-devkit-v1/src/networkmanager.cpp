#include <common.h>
#include <WiFi.h>
#include <networkmanager.h>

# define CHECK_NET_INTERVAL 5000 // 5s

namespace NetNS {

    unsigned long lastNetCheck;

    //char ssid[] = "Rede IME";
    char ssid[] = "Familia Ferraz 2.4G";
    char password[] = "dogtor1966";

    void WiFiStationConnected(WiFiEvent_t event, WiFiEventInfo_t info);

    void WiFiGotIP(WiFiEvent_t event, WiFiEventInfo_t info);

    void WiFiStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info);

    void printNetStatus();

    // This should be called from setup()
    void initWiFi() {

        // select WiFi mode
        WiFi.mode(WIFI_STA);
        WiFi.disconnect(true);

        // register WiFi event handlers
        WiFi.onEvent(WiFiStationConnected,
                     WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_CONNECTED);

        WiFi.onEvent(WiFiGotIP, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);

        WiFi.onEvent(WiFiStationDisconnected,
                     WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

        if(!WiFi.begin(ssid, password)){
            Serial.println("Initial WiFi Connection Failed!");
            if(WiFi.status() == WL_NO_SSID_AVAIL){
                Serial.println("WiFi AP is too far away or SSID is incorrect");
            }

        }
        //WiFi.begin(ssid);

    }

    /* events handling */
    // TODO: these could be more useful
    // https://randomnerdtutorials.com/esp32-useful-wi-fi-functions-arduino/#10 
    // how?

    void WiFiStationConnected(WiFiEvent_t event, WiFiEventInfo_t info) {
        Serial.println("Connected to WiFi successfully!");
    }

    void WiFiGotIP(WiFiEvent_t event, WiFiEventInfo_t info) {
        Serial.println("IP address: ");
        Serial.println(WiFi.localIP());
    }

    // events handler if wifi disconnects
    void WiFiStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info) {
        Serial.println("Disconnected from WiFi access point");
        Serial.print("WiFi lost connection. Reason: ");
        Serial.println(info.wifi_sta_disconnected.reason);
        Serial.println("Trying to Reconnect");

        WiFi.begin(ssid, password);
        Serial.println("Trying to Reconnect... ");
    }


    // This should be called from loop()
    inline void checkNetConnection() {
        if (currentMillis - lastNetCheck > CHECK_NET_INTERVAL) {
    #       ifdef DEBUG
            printNetStatus();
    #       endif
            if (WiFi.status() != WL_CONNECTED){ 
                WiFi.begin(ssid, password);
                Serial.println("Trying to Reconnect... ");
            }
            lastNetCheck = currentMillis;
        }
    }

}

    void printNetStatus() {
        byte macBuffer[6];  // create a buffer to hold the MAC address
        WiFi.macAddress(macBuffer); // fill the buffer
        Serial.print("The MAC address is: ");
        for (byte octet = 0; octet < 6; ++octet) {
            Serial.print(macBuffer[octet], HEX);
            if (octet < 5) {
                Serial.print(':');
            }
        }
        Serial.println("");

        if ((WiFi.status() != WL_CONNECTED)) {
            Serial.println("Not connected to WiFi.");
            return;
        }

        Serial.println("");
        Serial.print("The IP address is: ");
        Serial.println(WiFi.localIP());
        Serial.print("The wifi network is: ");
        Serial.println(WiFi.SSID());
    }

    bool connected() {
        return WiFi.status() == WL_CONNECTED;
    }
}

void initWiFi() {
    NetNS::initWiFi();
}

void checkNetConnection(){
    NetNS::checkNetConnection();
}

bool connected() {
    return NetNS::connected();
}
