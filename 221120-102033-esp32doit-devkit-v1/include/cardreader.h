#ifndef CARD_READER_H
#define CARD_READER_H

void initCardReaders();

bool checkCardReaders(const char*& readerID, unsigned long int& cardID);

void blinkOk(const char* reader);

void blinkFail(const char* reader);

#endif
