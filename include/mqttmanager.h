#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

void initMqtt(); 

bool isClientConnected();

bool sendLog(const char* filename); 

void forceDBDownload();

#endif