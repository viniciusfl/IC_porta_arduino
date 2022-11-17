#define RETRY_DOWNLOAD_TIME 60000
#define DOWNLOAD_INTERVAL 15000

#include "FS.h"
#include "SD.h"
#include "SPI.h"

WiFiClient client;

char SERVER[] = {"10.0.2.106"};

// This should be called from setup()
inline void initDisk() {
    
    if(!SD.begin()){
        Serial.println("Card Mount Failed");
        return;
    }else{
      Serial.println("SD connected.");
    }
    delay(300);
    
    // reset both timestamps and then update one BD so we don't have trouble when we turn off and on esp32
    resetTimestampFiles();
    startDownload();
}

// Globals related to the dbMaintenance() function. We probably
// should use an object and the state pattern instead.
File arquivo;
String dbfiles[] = {"/BANCOA.TXT", "/BANCOB.TXT"};
String timestampfiles[] = {"/TSA.TXT", "/TSB.TXT"};
char currentDB = -1; // invalid
char newDB = -1;
unsigned long lastDownloadTime = 0;
bool downloading = false; // Is there an ongoing download?
bool headerDone = false;
bool beginningOfLine = true;
char netLineBuffer[11];
char position = 0;
char previous;

// This should be called from loop()
// At each call, we determine the current state we are in, perform
// a small chunk of work, and return. This means we do not hog the
// processor and can pursue other tasks while updating the DB.
inline void dbMaintenance() {
    // We start a download only if we are not already downloading
    if (!downloading) {
        // millis() wraps every ~49 days, but
        // wrapping does not cause problems here
        // TODO: we might prefer to use dates,
        //       such as "at midnight"
        if (currentMillis - lastDownloadTime > DOWNLOAD_INTERVAL) {
            startDownload();
        }

        return; 
    }

    /
    if(!client.connected())return;
    
    // If we did not return above, we are downloading
    if (!client.available()) {
        finishDownload();
        return;
    }

    // If we did not disconnect above, we are connected
    processDownload();
}

inline void startDownload() {
    client.connect(SERVER, 80);

    if (client.connected()) {
        Serial.println(F("Connected to server."));
    } else {
        Serial.println(F("Connection to server failed."));
    };

    // If connection failed, pretend nothing
    // ever happened and try again later
    if (!client.connected()) {
        lastDownloadTime = lastDownloadTime + RETRY_DOWNLOAD_TIME;
        client.stop();
        return;
    }

    downloading = true;
    headerDone = false;
    beginningOfLine = true;
    netLineBuffer[0] = 0;
    position = 0;
    previous = 0;

    client.println("GET /arduino.txt HTTP/1.1");
    client.println("Host: 10.0.2.106");
    client.println();
    // Hack alert! This is a dirty way of saying "not the current DB"
    newDB = 0;
    if (currentDB == 0) {newDB = 1;};

    SD.remove(dbfiles[newDB]);
    SD.remove(timestampfiles[newDB]);
#   ifdef DEBUG
      Serial.print("Writing to ");
      Serial.println(dbfiles[newDB]);
#   endif

    arquivo = SD.open(dbfiles[newDB], FILE_WRITE);
    //arquivo.println("Download started at " + rtc.now().unixtime());
}

inline void finishDownload() {
    Serial.println("Disconnecting from server.");
    arquivo.close();
    client.stop();
    downloading = false;

    // FIXME: we should only save the timestamp etc.
    //        if the download was successful
    arquivo = SD.open(timestampfiles[newDB], FILE_WRITE);
    // FIXME: this wraps, we should use something more robust
    lastDownloadTime = currentMillis;
    
    arquivo.println(lastDownloadTime); 
    
    arquivo.close();

    //if (currentDB >= 0) SD.remove(timestampfiles[currentDB]);
    
    chooseCurrentDB();
}


inline void processDownload() {
    if (!client.available()){ 
      return;   
    }

    char c = client.read();
    Serial.println(client.available());
    Serial.println(client.connected());
    Serial.println("--");
    if (headerDone) {
        if (c == '\r') return;
        if (c == '\n') {

            Serial.println("Writing " + String(netLineBuffer) + " to DB file");

            arquivo.println(netLineBuffer);
            position = 0;
            netLineBuffer[position] = 0;
            return;
        }
        netLineBuffer[position] = c;
        netLineBuffer[position +1] = 0;
        ++position;
    } else {
        if (c == '\n') {
            if (beginningOfLine and previous == '\r') {
                headerDone =  true;
#               ifdef DEBUG
                Serial.println(F("Header done!"));
#               endif
            } else {
                previous = 0;
            }
            beginningOfLine = true;
        } else {
            previous = c;
            if (c != '\r') beginningOfLine = false;
        }
    }
}

char chooseCurrentDB() {
    currentDB = -1; // invalid
    unsigned long previousBestTime = 0;
    for (char i = 0; i < 2; ++i) { // 2 is the number of DBs
        File f = SD.open(timestampfiles[i]);
        if (!f) {
              Serial.println("DB " + dbfiles[i] + " not available");
        } else {
            long t = f.parseInt(); // If reading fails, this returns 0

            Serial.print("DB " + dbfiles[i] + " timestamp: ");
            Serial.println(t);

            // FIXME
            // t should also be > 0, but we do not need to check here
            // because previousBestTime is always >= 0. However, this
            // code *needs* to be changed, because millis() wraps every
            // 49 days and that *will* cause problems here.
            // One possibility is using "unsigned long long"; another
            // is, in finishDownload(), instead of using timestamps
            // and removing the old DB file, just use "0" and "1" as
            // the "timestamps". Since long long is expensive on the
            // arduino, option two seems the way to go.
            if (t > previousBestTime) {
                previousBestTime = t;
                currentDB = i;
            }
        }
        f.close();
    }
      #       ifdef DEBUG
            Serial.println("Choosing " + dbfiles[currentDB] + " as actual DB.");
      #       endif
      if (currentDB < 0) {
          Serial.println(F("No DB available!"));
      }
}



inline void resetTimestampFiles(){
  File f;
  for(int i = 0; i < 2; i++){
    SD.remove(timestampfiles[i]);
    f = SD.open(timestampfiles[i], FILE_WRITE);
    int a = 0;
    f.println(a);
    f.close();
  }
}
