#ifndef DOOR_H
#define DOOR_H

void initDoor();

bool openDoor(const char* reader = NULL);

bool denyToOpenDoor(const char* reader);

#endif
