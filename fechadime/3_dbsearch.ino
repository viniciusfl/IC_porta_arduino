String input; // FIXME: i couldn't use long because some cards did surpass max value

bool isSearching = false;
File file2;

// Function that is called when card is read 
inline void dbStartSearch(uint8_t* data, uint8_t bits, const char* message){
    
    String card = "";
    isSearching = true;
    Serial.print("\nWe received -> ");
    Serial.print(bits);
    Serial.print("bits / ");
    uint8_t bytes = (bits+7)/8;
    
    for (int i=0; i<bytes; i++){
      card += String(data[i] >> 4, HEX);
      card += String(data[i] & 0xF, HEX);
    }
    Serial.println(card);
    input = card;
    file2 = SD.open(dbfiles[currentDB], FILE_READ);
}


inline void dbSearch(){
    
    // if we are not searching an id on db, there is no need to go further
    if(!isSearching){
      return;
    }

    // if we can't open
    if(!file2){
        Serial.println("Failed to open file for reading... stopping search");
        isSearching = false;
        return;
    }

    // if file2 isn't available, then we read all file and didn't find what we were searching for
    if(!file2.available()) {
        Serial.println("Doesn't exist in db.");
        file2.close();
        isSearching = false;
        return;
    }

    // read next string 
    String current = file2.readStringUntil('\n'); // FIXME
    current.trim();
    #       ifdef DEBUG
    Serial.println("Comparing... " + input + " = " + current);
    #       endif
    if(current == input){
        Serial.print("Exists in db... ---> " +  current + " = " + input);
        isSearching = false;
        file2.close();
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
