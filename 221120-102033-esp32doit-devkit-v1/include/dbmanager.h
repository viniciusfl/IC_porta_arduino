#ifndef DB_MANAGER_H
#define DB_MANAGER_H

void initDBMan();
void updateDB();
void startUpdateDB();
void sendLog(const char* filename);
bool isClientConnected();
#endif
