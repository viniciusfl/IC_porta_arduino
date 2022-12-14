#ifndef CARD_READER_H
#define CARD_READER_H

struct cardData {
    unsigned long int cardID; // If so, this is his ID number
    int readerID; // And it came from this reader
};

void initCardReaders();

bool checkCardReaders(cardData*);

#endif
