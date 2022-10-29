String input; // FIXME: I think this should be unsigned,
            // but what about atol() ?

bool readInput = false;
File file2;

inline void dbStartSearch(uint8_t* data, uint8_t bits, const char* message){
    String b;
    String a;
    readInput = true;
    Serial.print("We received -> ");
    Serial.print(bits);
    Serial.print("bits / ");
    uint8_t bytes = (bits+7)/8;
    for (int i=0; i<bytes; i++){
      //Serial.print(data[i] >> 4, 16);
      //Serial.print(data[i] & 0xF, 16);
      b = String(data[i] >> 4, HEX);
      b += String(data[i] & 0xF, HEX);
      a += b;
    }
    Serial.println(a);
    input = a;
    dbSearch();
}

inline void dbSearch(){
      File arquivo;
      String current;
      arquivo = SD.open(dbfiles[currentDB], FILE_READ);
      if(!arquivo){
        Serial.println("Failed to open file for reading");
        return;
      }
      Serial.println("db search");
      while(arquivo.available()) {
          current = arquivo.readStringUntil('\n');
          current.trim();
          if(current == input){
              Serial.println(F(" ------> exists in db!!!!!"));
              readInput = false;
              arquivo.close();
              return;
          }
      }
      
}


/*
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
        return 0;

    }else{ //Full message received...

        //Add null character to string
        message[message_pos] = '\0';

        //Print the message (or do other things)
        Serial.print("We received: ");
        Serial.println(atol(message));
        message_pos = 0;
        readInput = true;
        file2 = SD.open(dbfiles[currentDB], FILE_READ);
        return atol(message);
        //Reset for the next message
        message[0] = 0;
   }
}
*/
