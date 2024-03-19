#ifndef AUTHORIZER_H
#define AUTHORIZER_H

int openDB(const char*);
void closeDB();
bool userAuthorized(const char* readerID, const char* cardHash);
void refreshQuery();
void calculate_hash(unsigned long cardID, char* hashBuf);

#endif
