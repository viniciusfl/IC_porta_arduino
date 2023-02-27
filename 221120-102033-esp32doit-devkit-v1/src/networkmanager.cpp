static const char* TAG = "network";

#include <common.h>
#include <WiFi.h>
#include <networkmanager.h>
#include <timemanager.h>
#include <apiserver.h>

# define CHECK_NET_INTERVAL 5000 // 5s
# define NET_TIMEOUT 30000 // 30s

namespace NetNS {

    unsigned long lastNetCheck;
    unsigned long lastNetOK;

    static httpd_handle_t server = NULL;

    //char ssid[] = "Rede IME";
    char ssid[] = "Familia Ferraz 2.4G";
    char password[] = "dogtor1966";

    // ESP IP did change in my house, so i had to do this because
    // different IP causes problem with the certificates
    IPAddress local_IP(10, 0, 2, 101);
    IPAddress gateway(192, 168, 1, 1);
    IPAddress subnet(255, 255, 0, 0);

    // Events callback
    void WiFiGotIP(WiFiEvent_t event, WiFiEventInfo_t info);

    void WiFiLostIP(WiFiEvent_t event, WiFiEventInfo_t info);

    void printNetStatus();

    inline void netReset() {
        WiFi.disconnect(true);
        WiFi.config(local_IP, gateway, subnet);
        WiFi.begin(ssid, password);
        //WiFi.begin(ssid);
    }

    // This should be called from setup()
    inline void initWiFi() {
        // select WiFi mode
        WiFi.mode(WIFI_STA);

        // register WiFi event handlers
        WiFi.onEvent(WiFiGotIP, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
        WiFi.onEvent(WiFiLostIP, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED); 

        netReset();
    }

    /* events handling */
    // https://randomnerdtutorials.com/esp32-useful-wi-fi-functions-arduino/#10 
    void WiFiGotIP(WiFiEvent_t event, WiFiEventInfo_t info) {
        log_v("Connected to WiFi successfully! IP address: %s",
              WiFi.localIP().toString().c_str());

        if (server == NULL) {
            server = initServer();
        }

        configNTP();
    }

    void WiFiLostIP(WiFiEvent_t event, WiFiEventInfo_t info) {
        log_i("Disconnected from WiFi...");

        if (server != NULL){
            disconnectServer(server);
            server = NULL;
        }
    }

    // This should be called from loop()
    // Normally, the ESP32 WiFi lib will try to reconnect automatically
    // if the connection is lost for some reason. However, in the unlikely
    // event that such reconnection fails, we will probably be stuck
    // offline. This code handles this rare case.
    // TODO: detect and log if we stay offline for a really long time
    //       (several hours) - maybe force a reset?
    inline void checkNetConnection() {
        if (currentMillis - lastNetCheck <= CHECK_NET_INTERVAL) return;

        lastNetCheck = currentMillis;

        printNetStatus();

        if (WiFi.status() == WL_CONNECTED) {
            // All is good!
            lastNetOK = currentMillis;
        } else if (currentMillis - lastNetOK > NET_TIMEOUT) {
            // We are offline, but maybe we are in a transient
            // state during a reconnection attempt. So, we only
            // force a reconnection if we have been offline for
            // a "long" time.
            netReset();
            lastNetOK = currentMillis;
        }
        // If both conditions fail, lastNetOK is not updated
    }

    void printNetStatus() {
#       if CORE_DEBUG_LEVEL >= ARDUHAL_LOG_LEVEL_VERBOSE

        byte macBuffer[6];  // create a buffer to hold the MAC address
        WiFi.macAddress(macBuffer); // fill the buffer

        // Now let's convert this to a string
        char txtbuf[19]; // aa:bb:cc:dd:ee:ff:\0
        for (int i = 0; i < 6; ++i) {
            snprintf(txtbuf + 3*i, 4, "%02hhx:", macBuffer[i]);
        }
        txtbuf[17] = 0; // eliminate extra ":" at the end
        log_v("The MAC address is: %s", txtbuf);

        if ((WiFi.status() != WL_CONNECTED)) {
            log_v("Not connected to WiFi.");
            return;
        }

        log_v("The IP address is %s and the wifi net is %s",
              WiFi.localIP().toString().c_str(), WiFi.SSID().c_str());

#       endif
    }

    inline bool connected() { return WiFi.status() == WL_CONNECTED; }
}

void initWiFi() { NetNS::initWiFi(); }

void checkNetConnection() { NetNS::checkNetConnection(); }

bool connected() { return NetNS::connected(); }

void netReset() { NetNS::netReset(); }
