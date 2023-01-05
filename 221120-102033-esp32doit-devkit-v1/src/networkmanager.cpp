#include <common.h>
#include <WiFi.h>
#include <networkmanager.h>
#include <timemanager.h>

# define CHECK_NET_INTERVAL 5000 // 5s
# define NET_TIMEOUT 30000 // 30s

namespace NetNS {

    unsigned long lastNetCheck;
    unsigned long lastNetOK;

    //char ssid[] = "Rede IME";
    char ssid[] = "Familia Ferraz 2.4G";
    char password[] = "dogtor1966";

    // callback
    void WiFiGotIP(WiFiEvent_t event, WiFiEventInfo_t info);

    void printNetStatus();

    // This should be called from setup()
    void initWiFi() {

        // select WiFi mode
        WiFi.mode(WIFI_STA);
        WiFi.disconnect(true);

        // register WiFi event handlers
        WiFi.onEvent(WiFiGotIP, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);

        WiFi.begin(ssid, password);
        //WiFi.begin(ssid);

    }

    /* events handling */
    // https://randomnerdtutorials.com/esp32-useful-wi-fi-functions-arduino/#10 
    void WiFiGotIP(WiFiEvent_t event, WiFiEventInfo_t info) {
        Serial.print("Connected to WiFi successfully!");
        Serial.print(" IP address: ");
        Serial.println(WiFi.localIP());

        configNTP();
    }

    // This should be called from loop()
    // Normally, the ESP32 WiFi lib will try to reconnect automatically
    // if the connection is lost for some reason. However, in the unlikely
    // event that such reconnection fails, we will probably be stuck
    // offline. This code handles this rare case.
    inline void checkNetConnection() {
        if (currentMillis - lastNetCheck <= CHECK_NET_INTERVAL) return;

        lastNetCheck = currentMillis;

#       ifdef DEBUG
        printNetStatus();
#       endif

        if (WiFi.status() == WL_CONNECTED) {
            // All is good!
            lastNetOK = currentMillis;
        } else if (currentMillis - lastNetOK > NET_TIMEOUT) {
            // We are offline, but maybe we are in a transient
            // state during a reconnection attempt. So, we only
            // force a reconnection if we have been offline for
            // a "long" time.
            WiFi.disconnect();
            WiFi.begin(ssid, password);
            //WiFi.begin(ssid);
            lastNetOK = currentMillis;
        }
        // If both conditions fail, lastNetOK is not updated
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
