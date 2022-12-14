#ifndef CARD_READER_H
#define CARD_READER_H

struct cardData {
    unsigned long int cardID;
    int readerID;
};

void initCardReaders();

bool checkCardReaders(cardData*);

#endif
