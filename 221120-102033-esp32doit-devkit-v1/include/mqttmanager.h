#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

void initMqtt(); 

bool startDownload(); 

void finishDownload(); 

bool didDownloadFinish();

bool isClientConnected();

bool sendLog(const char* filename); 

#endif