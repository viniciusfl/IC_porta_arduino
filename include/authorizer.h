#ifndef AUTHORIZER_H
#define AUTHORIZER_H

int openDB(const char*);
void closeDB();
bool userAuthorized(const char* readerID, unsigned long cardID);

#endif
