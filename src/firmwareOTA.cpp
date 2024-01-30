static const char *TAG = "ota";

#include <tramela.h>

#include <firmwareOTA.h>

#include <Update.h>

#ifdef USE_SD
#include "SPI.h"
#include "SD.h"
#else
#include "FFat.h"
#endif

namespace OTA{
    class FirmwareUpdater {
        public:
            int writeBinary(const char* data, int length);
            void processOTAUpdate();
        private:
            bool startOTAUpdate();
            const char* filename = "/firmware.bin";
            bool performUpdate(Stream &updateSource, int updateSize);

            File file;
            bool downloading = false;
    };

    int FirmwareUpdater::writeBinary(const char* data, int length) {
        if (!downloading) {
            if (!startOTAUpdate()) {
                log_e("Cannot start download!");
                return -1;
            }
        }
        downloading = true;
        int written = file.write((byte*) data, length);

        return written;
    }


    bool FirmwareUpdater::startOTAUpdate() {
        if (DISK.exists(filename)) { DISK.remove(filename); }

        file = DISK.open(filename, FILE_WRITE);
        if (!file) {
            log_e("Cannot open file!");
            return false;
        }
        return true;
    }

    bool FirmwareUpdater::performUpdate(Stream &updateSource, int updateSize) {
        if (Update.begin(updateSize)) {
            int written = Update.writeStream(updateSource);
            if (written != updateSize) {
                log_e("Couldnt write binary into memory!");
                return false;
            }
            if (!Update.end()) {
                log_e("Couldnt write binary into memory!");
                return false;
            }

            if (!Update.isFinished()) {
                log_e("Error finishing update");
                return false;
            }
        } else {
            log_e("There is not enough space in memory for binary");
            return false;
        }
        return true;
    }

    void FirmwareUpdater::processOTAUpdate() {
        file.flush();
        file.close();
        file = DISK.open(filename, FILE_READ);

        if (file) {
            int updateSize = file.size();
            if (!performUpdate(file, updateSize)) {
                file.close();
                log_e("Aborting update");
                return;
            };
            file.close();

        }
        log_w("Finished update, restarting ESP...");
        ESP.restart();
    }

    FirmwareUpdater firmware;
}

void writeToFirmwareFile(const char* data, int data_len) {
    OTA::firmware.writeBinary(data, data_len);
}

void performFirmwareUpdate() {
    OTA::firmware.processOTAUpdate();
}
