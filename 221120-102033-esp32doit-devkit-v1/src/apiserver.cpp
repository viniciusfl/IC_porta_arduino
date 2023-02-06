static const char* TAG = "server";

#include <common.h>
#include <Arduino.h>
#include <apiserver.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <authorizer.h>
#include <SD.h>

namespace webServer {
    WebServer server(80);

    StaticJsonDocument<250> jsonDocument;
    char buffer[250];
    File f;

    inline void open();
    inline void getLogs();
    inline void getUpdate();

    inline void init() {
        log_e("Started api server..");
        server.on("/open", open);
        server.on("/getLogs", getLogs);
        //server.on("/getUpdate"); 
        server.begin();
    }

    inline void updateServer() {
        server.handleClient();
    }

    inline void create_json(char *tag, float value, char *unit) {  
        jsonDocument.clear();
        jsonDocument["door"] = value;
        jsonDocument["open"] = unit;
        serializeJson(jsonDocument, buffer);  
    }

    inline void add_json_object(char *tag, float value, char *unit) {
        JsonObject obj = jsonDocument.createNestedObject();
        obj["door"] = value;
        obj["open"] = unit; 
    }

    inline void open() {
        bool opened = openDoor();
        jsonDocument.clear();
        jsonDocument["door"] = doorID;
        jsonDocument["open"] = opened;
        serializeJson(jsonDocument, buffer);
        server.send(200, "application/json", buffer);
    }

    inline void getLogs() {
        log_v("Sended log.db ");
        f = SD.open("/log.db");
        if (f) {
            server.sendHeader("Content-Type", "text/text");
            server.sendHeader("Content-Disposition", "attachment; filename=log.db");
            server.sendHeader("Connection", "close");
            server.streamFile(f, "application/octet-stream");
            f.close();
        }
    }

    inline void getUpdate() {}
}

void initServer() {webServer::init();}

void updateServer() {webServer::updateServer();}