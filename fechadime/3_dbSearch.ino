unsigned long int input;

bool isSearching = false;



// Function that is called when card is read 
inline void dbStartSearch(uint8_t* data, uint8_t bits, const char* message){
    String card = "";
    isSearching = true;
    Serial.print("\nWe received -> ");
    Serial.print(bits);
    Serial.print("bits / ");
    uint8_t bytes = (bits+7)/8;

    // concatenate each byte from hex
    for (int i=0; i<bytes; i++){
      card += String(data[i] >> 4, HEX);
      card += String(data[i] & 0xF, HEX);
    }

    // convert hex into dec
    input = strtoul(card.c_str(), NULL, 16);
    
    // Open database if its not opened
    if(!downloading)
      if (openDb("/sd/banco.db")) return;
    
    // Make query and execute it
    char searchDB[100];
    sprintf(searchDB, "SELECT EXISTS(SELECT * FROM %s WHERE cartao='%lu')", dbNames[currentDB], input); //FIXME
    db_exec(searchDB);

    // Close db
    isSearching = false;
    if(!downloading) sqlite3_close(db);
}

const char* data = "Callback function called";
static int callback(void *data, int argc, char **argv, char **azColName){ 
   /*
    * This function is called when we make a query and receives db output. 
    * We only care about output when we are searching an card.
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
