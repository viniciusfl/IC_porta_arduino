#include <../include/dbMaintenance.h>

#define RETRY_DOWNLOAD_TIME 60000

#define DOWNLOAD_INTERVAL 20000

WiFiClient client;

char SERVER[] = {"10.0.2.106"};

// This should be called from setup()
dataBase::dataBase(){
    Serial.println("nada");
}

void dataBase::initDataBase() {
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
    chooseCurrentDB();
    //startDownload();
}

// This should be called from loop()
// At each call, we determine the current state we are in, perform
// a small chunk of work, and return. This means we do not hog the
// processor and can pursue other tasks while updating the DB.
void dataBase::dbMaintenance(){

    if(isSearching){
        search();
    }
    // We start a download only if we are not already downloading
    if (!downloading) {
        // millis() wraps every ~49 days, but
        // wrapping does not cause problems here
        // TODO: we might prefer to use dates,
        //       such as "at midnight"
        //
        // ANWSER: we can use an alarm with RTC 1307
        // https://robojax.com/learn/arduino/?vid=robojax_DS1307-clock-alarm
        // 
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

void dataBase::startDownload() {
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
    exec(removeDB);
    SD.remove(timestampfiles[newDB]);
#   ifdef DEBUG
      Serial.print("Writing to ");
      Serial.println(dbNames[newDB]);
#   endif


}

void dataBase::finishDownload() {
    
    client.flush();
    client.stop();
    downloading = false;

    // FIXME: we should only save the timestamp etc.
    //        if the download was successful
    // QUESTION:how can i know that?

    // FIXME: this wraps, we should use something more robust
    // ANWSER: maybe utc time?



    arquivo = SD.open("/TSA.TXT", FILE_WRITE);
    arquivo.println(1); 
    arquivo.close();

    arquivo = SD.open("/TSA.TXT", FILE_WRITE);
    arquivo.println(0); 
    arquivo.close();
    
    lastDownloadTime = currentMillis;
    Serial.println("Disconnecting from server and finishing db update.");

    sqlite3_close(db);
}


void dataBase::processDownload() {
    char c = client.read();

    if (headerDone) {
        if (c == '\r') return;
        if (c == '\n') {
#           ifdef DEBUG
              Serial.println("Writing " + String(netLineBuffer) + " to DB file");
#           endif
            insert(netLineBuffer);
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

void dataBase::chooseCurrentDB() {
    currentDB = -1; // invalid
    int max = -1;
    for (char i = 0; i < 2; ++i) { // 2 is the number of DBs
        File f = SD.open(timestampfiles[i]);
        if (!f) {
              Serial.print(dbNames[i]);
              Serial.println(" not available");
        } else {
            int t = f.parseInt(); // If reading fails, this returns 0

            Serial.print(dbNames[i]);
            Serial.print(" timestamp: ");
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
            if (t > max) {
                max = t;
                currentDB = i;
            }
        }
        f.close();
    }
      //Serial.println("Choosing " + dbNames[currentDB] + " as actual DB.");
}



void dataBase::resetTimestampFiles(){
  File f;
  for(int i = 0; i < 2; i++){
    SD.remove(timestampfiles[i]);
    f = SD.open(timestampfiles[i], FILE_WRITE);
    int a = 0;
    f.println(a);
    f.close();
  }
}

int dataBase::openDb(const char *filename) {
   int rc = sqlite3_open(filename, &db);
   if (rc) {
       Serial.printf("Can't open database: %s\n", sqlite3_errmsg(db));
       return rc;
   } else {
       Serial.printf("Opened database successfully\n");
   }
   return rc;
}

static int callback(void *data, int argc, char **argv, char **azColName){ 
   /*
    * This function is called when we make a query and receives db output. 
    * We only care about output when we are searching an card id.
    */
   if(!isSearching){
      return 0;
   }
   if(atoi(argv[0]) == 1){
      Serial.println("Exists in db.");
   }else{
      Serial.println("Doesn't exist in db.");
   }
   return 0;
}

bool dataBase::search(){
    // Open database if its not opened
    if(!downloading)
        if (openDb("/sd/banco.db"))
            return false;

    // Make query and execute it
    char searchDB[300];
    sprintf(searchDB, "SELECT EXISTS(SELECT * FROM %s WHERE cartao='%lu')", dbNames[0], input); //FIXME
    exec(searchDB);
    
    isSearching = false;

    // Close db
    if(!downloading)
        close();
    return true;
}

int dataBase::exec(const char *sql) {
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


void dataBase::insert(char *element){
  char *zErrMsg = 0;
  int rc;
  char insertMsg[100]; 
  sprintf(insertMsg, "INSERT INTO %s (cartao) VALUES ('%s')", dbNames[0], element);
  rc = exec(insertMsg);
  if (rc != SQLITE_OK) {
       sqlite3_close(db);
       return;
  }

}

void dataBase::close(){
  sqlite3_close(db);
}

