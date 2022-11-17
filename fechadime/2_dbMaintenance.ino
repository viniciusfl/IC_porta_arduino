#define RETRY_DOWNLOAD_TIME 60000
#define DOWNLOAD_INTERVAL 15000

#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include <sqlite3.h>

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
    sqlite3_initialize();
    
    // reset both timestamps and then update one BD so we don't have trouble when we reset program
    resetTimestampFiles();
    //startDownload();
}

// Globals related to the dbMaintenance() function. We probably
// should use an object and the state pattern instead.
sqlite3 *db;
File arquivo;
String dbNames[] = {"bancoA", "bancoB"};
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
    
    // If we did not return above, we are downloading
    if (!client.available() && !client.connected()) {
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
    client.println("Connection: close");
    client.println();

    // Hack alert! This is a dirty way of saying "not the current DB"
    newDB = 0;
    if (currentDB == 0) {newDB = 1;};
    
    // Open database 
    if (openDb("/sd/banco.db"))
    return;
    
    char removeDB[20];
    sprintf(removeDB, "DELETE FROM %s", dbNames[newDB]);
    db_exec(removeDB);
    SD.remove(timestampfiles[newDB]);
#   ifdef DEBUG
      Serial.print("Writing to ");
      Serial.println(dbNames[newDB]);
#   endif


}

inline void finishDownload() {
    Serial.println("Disconnecting from server.");
    client.flush();
    client.stop();
    downloading = false;

    // FIXME: we should only save the timestamp etc.
    //        if the download was successful
    arquivo = SD.open(timestampfiles[newDB], FILE_WRITE);
    // FIXME: this wraps, we should use something more robust
    lastDownloadTime = currentMillis;
    
    arquivo.println(lastDownloadTime); 
    
    arquivo.close();
    
    chooseCurrentDB();
    
    sqlite3_close(db);
}


inline void processDownload() {
    char c = client.read();

    if (headerDone) {
        if (c == '\r') return;
        if (c == '\n') {
#           ifdef DEBUG
              Serial.println("Writing " + String(netLineBuffer) + " to DB file");
#           endif
            dbInsert(netLineBuffer);
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
              Serial.println("DB " + dbNames[i] + " not available");
        } else {
            long t = f.parseInt(); // If reading fails, this returns 0

            Serial.print("DB " + dbNames[i] + " timestamp: ");
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
            Serial.println("Choosing " + dbNames[currentDB] + " as actual DB.");
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

int openDb(const char *filename) {
   int rc = sqlite3_open(filename, &db);
   if (rc) {
       Serial.printf("Can't open database: %s\n", sqlite3_errmsg(db));
       return rc;
   } else {
       Serial.printf("Opened database successfully\n");
   }
   return rc;
}

char *zErrMsg = 0;
int db_exec(const char *sql) {
   Serial.println(sql);
   long start = micros();
   int rc = sqlite3_exec(db, sql, callback, (void*)data, &zErrMsg);
   if (rc != SQLITE_OK) {
       Serial.printf("SQL error: %s\n", zErrMsg);
       sqlite3_free(zErrMsg);
   } else {
       Serial.printf("Operation done successfully\n");
   }
   Serial.print(F("Time taken:"));
   Serial.println(micros()-start);
   return rc;
   
}


void dbInsert(char *element){
  char *zErrMsg = 0;
  int rc;
  char insertMsg[100]; 
  sprintf(insertMsg, "INSERT INTO %s (cartao) VALUES ('%s')", dbNames[0], element);
  rc = db_exec(insertMsg);
  if (rc != SQLITE_OK) {
       sqlite3_close(db);
       return;
  }

}
