#ifndef DB_MANAGER_H
#define DB_MANAGER_H

void initDB();
void updateDB();
bool userAuthorized(const char* readerID, unsigned long cardID);

#endif
