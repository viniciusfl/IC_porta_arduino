static const char* TAG = "disk";

#include <tramela.h>

#include <Arduino.h>

#ifdef USE_SD
#include <SPI.h>
#include <SD.h>
#else
#include <FFat.h>
#endif

#include <dbmanager.h>  //wipeDBFiles()
#include <logmanager.h> //wipeLogs();

#ifdef USE_SD
#include <sd_diskio.h>
// Write some garbage to the beginning of the disk and
// attempt to mount it with "format_if_empty" == true
bool formatSDCard() {
    SPI.begin(); // this is idempotent

    uint8_t pdrv = sdcard_init(SS, &SPI, 4000000);
    if (pdrv = 0xFF) { return false; }

    uint8_t zero = 0;
    for (uint32_t i = 0; i < 16 * 1024; ++i) {
        sd_write_raw(pdrv, &zero, i);
    }

    bool success = sdcard_mount(pdrv, "/tmpformat", 2, true);
    success = (0 == sdcard_unmount(pdrv)) and success;
    success = (0 == sdcard_uninit(pdrv)) and success;

    return success;
};
#endif

bool initDisk() {
#   ifdef USE_SD
    if (!SD.begin(SS, SPI, 4000000, "/sd", 5, true)) {
        //formatSDCard();
#   else
    if (!FFat.begin(true, "/ffat", 5)) {
        //FFat.format();
#   endif
        log_e("Card Mount Failed...");
        return false;
    }

    //wipeDBFiles();
    //wipeLogs();
    log_i("Disk available.");
    return true;
}
