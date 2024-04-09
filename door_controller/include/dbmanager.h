#ifndef DB_MANAGER_H
#define DB_MANAGER_H

void initDBMan();
ssize_t writeToDatabaseFile(const char* data, int data_len);
void finishDBDownload();
void cancelDBDownload();
bool wipeDBFiles();

#endif
