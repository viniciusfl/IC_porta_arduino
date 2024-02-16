#ifndef TEMPBUFS_H
#define TEMPBUFS_H

#define MAX_LOGMSG_SIZE 384
/*
This allows us to choose an unused buffer from a set of preallocated
buffers without using locks. As long as we never actually use all
available buffers, this should never block.

ESP-IDF version 4, currently used by arduino-esp32, offers
the function compare_and_set_native(addr1, val, addr2)
from <compare_set.h> . In version 5, this was replaced by
esp_cpu_compare_and_set(addr1, val1, val2) from <esp_cpu.h>.

compare_and_set_native(addr1, val, addr2) does not return
anything. If the value in addr1 is val, it swaps the
contents of addr1 and addr2.

esp_cpu_compare_and_set(addr, val1, val2) returns a boolean
indicating if the operation succeeded or not. It checks
whether the value in addr in val1; if so, it changes it to
val2 and returns true.
*/


// TODO: change this when arduino-esp32 migrates to ESP-IDF version 5.
#define ESP_IDF_4

#ifdef ESP_IDF_4
#include <compare_set.h> // compare_and_set_native()
#else
#include <esp_cpu.h> // esp_cpu_compare_and_set()
#endif

// Realistically, we will very rarely use more than one buffer:
// this only happens if a task that is processing a log message
// is preempted by another task, also processing a log message.
#define NUM_BUFS 3

class TempBufsManager {
    public:
        inline char* operator[](int i) { return bufs[i].buf; };

        inline int get() {
            int id = -1;
#       ifdef ESP_IDF_4
            uint32_t result = 1;
            do {
                ++id;
                id %= NUM_BUFS;
                compare_and_set_native(status + id, 0, &result);
            } while (result == 1);
#       else // ESP-IDF version >= 5
            do {
                ++id;
                id %= NUM_BUFS;
            } while (not esp_cpu_compare_and_set(status + id, 0, 1));
#       endif
            return id;
        }

        inline void release(int id) { status[id] = 0; };

    private:

        typedef struct { char buf[MAX_LOGMSG_SIZE]; } tempbuf;

        tempbuf bufs[NUM_BUFS];
        uint32_t status[NUM_BUFS];
};

#endif
