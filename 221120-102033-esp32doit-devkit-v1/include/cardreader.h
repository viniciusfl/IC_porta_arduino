#ifndef CARD_READER_H
#define CARD_READER_H

void initCardReaders();

bool checkCardReaders(const char*& readerID, unsigned long int& cardID);

bool openDoor(const char* reader);

bool denyToOpenDoor(const char* reader);

#endif
