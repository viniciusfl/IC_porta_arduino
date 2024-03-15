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

namespace OTA {
    class FirmwareUpdater {
        public:
            FirmwareUpdater() {
                configured = esp_ota_get_boot_partition();
                running = esp_ota_get_running_partition();
            };
            int writeBinary(const char* data, int length);
            void processOTAUpdate();
            void cancelDownload();

        private:
            bool startOTAUpdate();

            const esp_partition_t *configured;
            const esp_partition_t *running;
            const esp_partition_t *update_partition;
            bool downloading = false;

            esp_ota_handle_t update_handle;
            int binary_file_length = 0;
    };


    void FirmwareUpdater::cancelDownload() {
        downloading = false;
        esp_ota_abort(update_handle);
    }


    bool FirmwareUpdater::startOTAUpdate() {
        if (configured == NULL or running == NULL) { return false; }

        // This is *probably* not a fatal error, so just log it
        if (configured != running) {
            log_w("Configured OTA boot partition at offset %#x, "
                  "but running from offset %#x",
                    configured->address, running->address);

            log_w("This can happen if either the OTA boot data or "
                  "preferred boot image become corrupted somehow.");
        }

        update_partition = esp_ota_get_next_update_partition(NULL);
        if (update_partition == NULL) { return false; }

        esp_err_t status = esp_ota_begin(update_partition,
                                         OTA_WITH_SEQUENTIAL_WRITES,
                                         &update_handle);

        if (status != ESP_OK) { return false; }

        binary_file_length = 0;
        downloading = true;
        return true;
    }


    int FirmwareUpdater::writeBinary(const char* ota_write_data, int length) {
        if (!downloading) {
            log_i("Starting OTA update");
            if (!startOTAUpdate()) {
                log_w("OTA: Cannot start download!");
                return -1;
            } else {
                log_d("OTA update started");
            }
        }

        if (length > 0) {
            int err = esp_ota_write(update_handle,
                                    (const void *) ota_write_data, length);
            if (err != ESP_OK) {
                log_w("OTA: Error writing data, aborting");
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
            return;
        }

        err = esp_ota_set_boot_partition(update_partition);
        if (err != ESP_OK) {
           log_e("esp_ota_set_boot_partition failed (%s)!",
                 esp_err_to_name(err));
           return;
        }

        log_w("Prepare to restart system!");
        delay(2000); // time to flush pending logs
        esp_restart();
    }

    FirmwareUpdater firmware;
}


void writeToFirmwareFile(const char* data, int data_len) {
    OTA::firmware.writeBinary(data, data_len);
}

void performFirmwareUpdate() { OTA::firmware.processOTAUpdate(); }

void cancelFirmwareDownload() { OTA::firmware.cancelDownload(); }
