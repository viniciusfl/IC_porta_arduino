#include <WiFi.h>
#include <common.h>

#define CHECK_NET_INTERVAL 30000 // 30s

#define RECONNECT_INTERVAL 30000 // 30s

extern unsigned long lastNetCheck;

extern unsigned long lastReconnectAttempt;

void WiFiInit();

void printNetStatus();

void checkWiFiConnection();

void WiFiStationConnected(WiFiEvent_t event, WiFiEventInfo_t info);

void WiFiGotIP(WiFiEvent_t event, WiFiEventInfo_t info);

void WiFiStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info);