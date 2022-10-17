/*


  Arquivos:
    bancoA
    bancoB
    bancoA.timestamp
    bancoB.timestamp
*/
#define DEBUG 0

#define RETRY_TIME 60000
#define DOWNLOAD_INTERVAL 15000

#include <SPI.h>
#include <Ethernet.h>
#include <SD.h>
#include "RTClib.h"
#include <EthernetUdp.h>
#include <TimeLib.h>

/*
unsigned int localPort = 8888;       // local port to listen for UDP packets

const char timeServer[] = "a.ntp.br"; // time.nist.gov NTP server

const int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message

byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets

EthernetUDP Udp; // A UDP instance to let us send and receive packets over UDP

*/


File file;

RTC_DS1307 rtc;

byte mac[] = {
  0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };

IPAddress server(10, 0, 2, 113);   

EthernetClient client;

void setup() {
  delay(150);
  Serial.begin(9600);

  // Wait until serial connection is established
   while (!Serial) {
    ; 
  }
  
  // Initialize ethernet
  Serial.println("e");
  Serial.println(Ethernet.begin(mac));
  
  // Initialize rtc 
  Serial.println("r");
  Serial.println(rtc.begin());

  if (! rtc.isrunning()) { // if the clock wasn't running, adjust the time once 
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  Serial.println("s");
  // Initialize SD card module
  Serial.println(SD.begin(4));
  
  //
  //Serial.println("--");
  // Firstly, we must know the newest DB
  chooseCurrentDB();
}

void loop() {
  // 
  dbSearch();
  // Update the oldest DB
  dbMaintenance();
}



String dbfiles[] = {"bancoA.txt", "bancoB.txt"};
String timestampfiles[] = {"A.txt", "B.txt"};
char currentDB = -1; 
unsigned long lastDownloadTime = 0;
bool downloading = false; // Is there an ongoing download?
bool headerDone = false; // Did we read the header already?
String line = "";
char newDB; 

inline void dbMaintenance() { 
    DateTime now = rtc.now();
    // We start a download only if we are not already downloading
    if (!downloading) {
        // millis() wraps every ~49 days, but I *think*
        // wrapping does not cause problems
        // TODO: we might prefer to use dates,
        //       such as "at midnight"
        if (millis() - lastDownloadTime > DOWNLOAD_INTERVAL) startDownload();
        return;
    }

    // If we did not return above, we are downloading
    if (!client.connected() && !client.available()) {
        finishDownload();
        return;
    }

    // If we did not disconnect above, we are connected
    processDownload();
}


inline void startDownload() {
    client.connect(server, 80);

#   ifdef DEBUG
      if (client.connected()) {
          Serial.println(F("Connected to server."));
      } else {
          Serial.println(F("Connection to server failed."));
      };
#   endif

    // If connection failed, pretend nothing
    // ever happened and try again later
    if (!client.connected()) {
        lastDownloadTime = lastDownloadTime + RETRY_TIME;
        client.stop();
        return;
    }

    downloading = true;
    headerDone = false;
    line = "";

    client.println(F("GET /arduino.txt HTTP/1.1"));
    client.println(F("Host: 10.0.2.113"));
    client.println();

    
    if (currentDB == 0) {newDB = 1;}
    else {newDB = 0;};

    SD.remove(dbfiles[newDB]);
    SD.remove(timestampfiles[newDB]);
    
#   ifdef DEBUG
      Serial.println("Updating DB " + dbfiles[newDB]);
#   endif
  
    file = SD.open(dbfiles[newDB], FILE_WRITE);
    
}

bool beginningOfLine = true;
char previous;
inline void processDownload() {
    if (!client.available()) return;
    char c = client.read();
    if (headerDone) {
        if (c == '\r') return;
        if (c == '\n') {
            file.println(line);
            line = "";
            return;
        }else{
          line = line + String(c); // FIXME: this is very expensive!
        }
    } else {
        if (c == '\n') {
            if (beginningOfLine and previous == '\r') {
                headerDone =  true;
                line = "";
            } else {
                previous = 0;
            }
            beginningOfLine = true;
        } else {
            line = line + String(c); // FIXME: this is very expensive!
            previous = c;
            if (c != '\r') beginningOfLine = false;
        }
    }
}

inline void finishDownload() {
#   ifdef DEBUG
      Serial.println(F("Disconnecting from server."));
#   endif
    file.close();
    client.stop();
    downloading = false;
    // FIXME: we should only save the timestamp etc.
    //        if the download was successful
#   ifdef DEBUG
      Serial.println("Writing new timestamp on " + timestampfiles[newDB]);
#   endif
    file = SD.open(timestampfiles[newDB], FILE_WRITE);
    file.println(rtc.now().unixtime());
    file.close();
    //chooseCurrentDB();
    currentDB = newDB;
    lastDownloadTime = millis();
#   ifdef DEBUG
      Serial.println("Finished...");
#   endif
}



long input;
bool readInput = false;
File file2;
inline void dbSearch(){
    if(readInput && file2.available()){
        long current = file2.parseInt();
        Serial.print(F("comparing with "));
        Serial.println(current);
        if(current == input){
            Serial.println(F("eeeeeeeeeeeeeeeeeeeeeeeeeeeeexists in db"));
            readInput = false;
        }

        if(!file2.available()){readInput = false;};
    }
  
    if(Serial.available() == 0){return;};
    
    input = readline();
}

const unsigned int MAX_MESSAGE_LENGTH = 20;


inline long int readline() {
    static char message[MAX_MESSAGE_LENGTH];
    static unsigned int message_pos = 0;
    char inByte = Serial.read();
      //Message coming in (check not terminating character) and guard for over message size
    if ( inByte != '\n' && (message_pos < MAX_MESSAGE_LENGTH - 1) )
    {
     //Add the incoming byte to our message
        message[message_pos] = inByte;
        message_pos++;
        
    }else{ //Full message received...
        
        //Add null character to string
        message[message_pos] = '\0';

        //Print the message (or do other things)
        Serial.print("We received: ");
        Serial.println(message);
        message_pos = 0;
        readInput = true;
        file2 = SD.open(dbfiles[currentDB], FILE_READ); 
        return atoi(message);
        //Reset for the next message
        
   }
   
}


char chooseCurrentDB() {
    currentDB = -1; // invalid
    long previousBestTime = -1;
    for (char i = 0; i < 2; ++i) { // 2 is the number of DBs
        File f = SD.open(timestampfiles[i]);
        if (!f) {
#           ifdef DEBUG
              Serial.println("DB " + dbfiles[i] + " not available");
#           endif
        } else {
            long t = f.parseInt(); // If reading fails, this returns 0
            if (t >= 0 and t > previousBestTime) {
                previousBestTime = t;
                currentDB = i;
            }
        }
        f.close();
    }
#   ifdef DEBUG
      //Serial.println("Choosing DB " + dbfiles[currentDB] + " as actual DB.");
      if (currentDB < 0) {
          Serial.println(F("No DB available!"));
      }
#   endif
}
