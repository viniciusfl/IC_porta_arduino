#ifndef DB_MANAGER_H
#define DB_MANAGER_H

void initDBMan();

void updateDB();

void writeToDatabaseFile(const char* data, int data_len);

#endif
