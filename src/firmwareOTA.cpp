static const char *TAG = "ota";

#include <tramela.h>

#include <firmwareOTA.h>

#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_flash_partitions.h"
#include "esp_partition.h"
#include "nvs.h"
#include "nvs_flash.h"


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
            void cancelFirmwareDownload();
        private:
            bool startOTAUpdate();

            const esp_partition_t *configured;
            const esp_partition_t *running;
            const esp_partition_t *update_partition;
            bool downloading = false;

            esp_ota_handle_t update_handle;
            int binary_file_length = 0;

    };

    void FirmwareUpdater::cancelFirmwareDownload() {
        downloading = false;
        esp_ota_abort(update_handle);
    }


    bool FirmwareUpdater::startOTAUpdate() {
        update_handle = 0 ;
        update_partition = NULL;
        log_i("Starting OTA example task");

        configured = esp_ota_get_boot_partition();
        running = esp_ota_get_running_partition();

        if (configured != running) {
            log_e("Configured OTA boot partition at offset 0x%08"PRIx32", but running from offset 0x%08"PRIx32,
                    configured->address, running->address);

            log_e("This can happen if either the OTA boot data or preferred boot image become corrupted somehow.");
                    Serial.println();

        }

        update_partition = esp_ota_get_next_update_partition(NULL);

        binary_file_length = 0;
        esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &update_handle);

        log_i("Started OTA configuration!");
        downloading = true;
        return true;
    }




    int FirmwareUpdater::writeBinary(const char* ota_write_data, int length) {
        if (!downloading) {
            if (!startOTAUpdate()) {
                log_e("Cannot start download!");
                return -1;
            }
        }

        if (length > 0) {
            int err = esp_ota_write( update_handle, (const void *)ota_write_data, length);
            if (err != ESP_OK) {
                esp_ota_abort(update_handle);
            }
            binary_file_length += length;
        }
        return length;
    }


    void FirmwareUpdater::processOTAUpdate() {

        int err = esp_ota_end(update_handle);
        if (err != ESP_OK) {
            if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
                log_e("Image validation failed, image is corrupted");
            } else {
                log_e("esp_ota_end failed (%s)!", esp_err_to_name(err));
            }
        }

        err = esp_ota_set_boot_partition(update_partition);
        if (err != ESP_OK) {
           log_e("esp_ota_set_boot_partition failed (%s)!", esp_err_to_name(err));

        }
        log_w("Prepare to restart system!");
        esp_restart();
    }

    FirmwareUpdater firmware;
}

void writeToFirmwareFile(const char* data, int data_len) {
    OTA::firmware.writeBinary(data, data_len);
}

void performFirmwareUpdate() {
    OTA::firmware.processOTAUpdate();
}

void cancelFirmwareDownload() {
    OTA::firmware.cancelFirmwareDownload();
}