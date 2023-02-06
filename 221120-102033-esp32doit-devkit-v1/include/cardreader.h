#ifndef CARD_READER_H
#define CARD_READER_H

void initCardReaders();

bool checkCardReaders(const char*& readerID, unsigned long int& cardID);

void openDoor(const char* reader);

void denyToOpenDoor(const char* reader);

#endif
