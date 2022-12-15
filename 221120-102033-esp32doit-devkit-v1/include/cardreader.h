#ifndef CARD_READER_H
#define CARD_READER_H

void initCardReaders();

bool checkCardReaders(int& readerID, unsigned long int& cardID);

void blinkOk(int reader);

void blinkFail(int reader);

#endif
