#ifndef DB_MANAGER_H
#define DB_MANAGER_H

void initDB();
void updateDB();
bool userAuthorized(int readerID, unsigned long cardID);

#endif
