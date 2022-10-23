long input; // FIXME: I think this should be unsigned,
            // but what about atol() ?

bool readInput = false;
File file2;
inline void dbSearch(){
    if(readInput && file2.available()){
        while(file2.available()) {
            long current = file2.parseInt();
            if (current == 0) continue; // We reached EOF, so parseInt returned 0
            //Serial.print(F("comparing with "));
            //Serial.println(current);
            if(current == input){
                Serial.println(F("eeeeeeeeeeeeeeeeeeeeeeeeeeeeexists in db"));
                break;
            }
        }

        readInput = false;
        file2.close();
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
