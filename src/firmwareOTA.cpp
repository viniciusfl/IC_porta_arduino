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

/*
   Whenever we update the firmware, there is a chance we introduce a
   serious bug. We would like to detect whether this has happened and
   rollback the firmware version if necessary. So, what we do is:

   1. Define "fwOK", initially false.

   2. Call firmwareOKWatchdog() periodically; if fwOK is still false
      10 minutes after boot, it calls seemsFaulty().

   3. seemsFaulty() checks whether the firmware has been recently
      updated and, if so, rolls back the firmware version.

   4. If we connect successfully to the mqtt broker (which means we may
      remotely control the system), we call currentFirmwareSeemsOK(),
      which changes fwOK to true.
*/

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

            void forceRollback();
            void seemsOK();
            void seemsFaulty();
            void watchdog();
        private:
            bool startOTAUpdate();

            const esp_partition_t *configured;
            const esp_partition_t *running;
            const esp_partition_t *update_partition;

            bool downloading = false;

            bool fwOK = false;

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


    void FirmwareUpdater::forceRollback() {
        log_w("Firmware rollback requested");
        delay(2000); // time to flush pending logs
        esp_ota_mark_app_invalid_rollback_and_reboot();
    }


    void FirmwareUpdater::seemsOK() {
        if (fwOK) { return; }

        fwOK = true;
        esp_ota_img_states_t state;
        esp_ota_get_state_partition(running, &state);
        if (state == ESP_OTA_IMG_PENDING_VERIFY) {
            esp_ota_mark_app_valid_cancel_rollback();
        }
    }

    void FirmwareUpdater::seemsFaulty() {
        if (fwOK) { return; }

        esp_ota_img_states_t state;
        esp_ota_get_state_partition(running, &state);
        if (state == ESP_OTA_IMG_PENDING_VERIFY) {
            log_w("Current firmware seems faulty, rolling back");
            delay(2000); // time to flush pending logs
            esp_ota_mark_app_invalid_rollback_and_reboot();
        } else {
            fwOK = true;
        }
    }

    void FirmwareUpdater::watchdog() {
        if (fwOK) { return; }

        if (millis() > 600000) { // 10 minutes of uptime
            seemsFaulty();
            return;
        }
    }


    FirmwareUpdater firmware;
}


void writeToFirmwareFile(const char* data, int data_len) {
    OTA::firmware.writeBinary(data, data_len);
}

void performFirmwareUpdate() { OTA::firmware.processOTAUpdate(); }

void cancelFirmwareDownload() { OTA::firmware.cancelDownload(); }

void forceFirmwareRollback() { OTA::firmware.forceRollback(); }

void currentFirmwareSeemsOK() { OTA::firmware.seemsOK(); }

void currentFirmwareSeemsFaulty() { OTA::firmware.seemsFaulty(); }

void firmwareOKWatchdog() { OTA::firmware.watchdog(); }

