# TODO

 * Check whether using a smaller page size with the SQLite DB saves
   memory.

 * Better error handling everywhere (crashing is not really an option,
   in extreme situations we should at least try restarting the MCU).
   Check return status of more function calls for memory allocation
   failures etc.

 * Choose and set license

 * Include the mechanical "open" button and the corresponding resistors
   and diode to the schematic and the PCB.

 * Check whether automatic firmware rollback is working.

 * Improve logging:

   - We should send log messages with QoS 1, not 2.

   - Divide `logmanager.cpp` in two files, one for logging and the other
     for the log manager, i.e., the class that manages and uploads the
     actual log files.

   - Minor bug: if we are currently sending a logfile and get disconnected,
     the message is still enqueued. When we reconnect, we may enqueue another
     logfile before the previous one is completely sent, consuming more
     memory. We may use `esp_mqtt_client_get_outbox_size()` to work around
     this ("0" means there is nothing there).

   - Instead of using `esp_mqtt_client_enqueue()`, we should use
     `esp_mqtt_client_publish()` in a separate thread. This would save
     some memory because there would not be a copy from the send buffer
     to the mqtt outbox. If I understand things correctly, trying to
     send something while offline should timeout and fail (unless we
     connect in the mean time). This means we would not need to check
     whether we are connected before attempting to send something.
     Still, it would be better to do just that.

   - The way we handle log messages in the callbacks in `cardreader.cpp`,
     with the `connectedMsg` etc buffers, is lame and wastes memory.

 * We should be able to define the door ID, network credentials, TLS
   credentials etc. at runtime, not hardcode them in the code

   - When writing the code to the MCU, add the relevant information to
     the NVS. This is not really "at runtime", but maybe it's enough:
     since the code itself is the same for all MCUs, future code upgrades
     (including OTA) are easy.
     <https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32/api-reference/storage/mass_mfg.html>
     <https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32/api-reference/storage/nvs_flash.html#nvs-encryption>
     <https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32/security/flash-encryption.html>
     <https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32/security/secure-boot-v2.html>

   - ESP32 has some facilities for autoconfiguration:
     <https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32/api-reference/provisioning/index.html>
     For WiFi, we may use Wi-Fi provisioning, SmartConfig or unified
     provisioning. Wi-Fi provisioning and unified provisioning may use
     Bluetooth and both have android/iOS apps. Unified provisioning is
     extensible, so we may add any data we want to it.

   - When this is working, we should keep the master keys in NVS
     instead of hardcoded

   - We should also implement MQTT commands to eliminate the DB file to
     "reset" the controller, change the device ID and credentials etc.

 * Implement MQTT command to make sql updates to the DB (add/remove
   users). While we prefer to update the whole DB each time, this is
   obviously much faster. But how to harmonize these small updates
   with the full DB uploads?

   - Alter the DB on the ESP (so that the changes are applied quickly)
     then update the DB on the server and let the ESP download it.
     This is simple and should work fine.

   - Same, but also create a "DB version"; sql updates always include
     an update to this version too, so the ESP realizes that its DB
     version matches that on the server and skips the download. This
     complicates the DB download process.

   - Only perform a full DB download during the initial setup, when
     an error is detected or when we receive a command to forcibly
     update the DB. This works and is reasonably simple (just subscribe
     and unsubscribe to the DB topic as needed). If we choose to go
     with this, we may also use the DB for logging.

   - If we need to further reduce memory usage, we could give up logging
     to disk files and use the DB for logging, but this complicates
     updating the DB (we would have to defer updates to when there are
     no logs to be sent).

# Misc small improvements

 * Should we use `sntp_set_time_sync_notification_cb()` to synchronize
   the HW clock?

 * wiegand `stateChanged()` and `receivedDataError()` callbacks should
   be more useful

 * Namespaces vs classes

 * What to do if `openDB()` fails?

 * What to do if `userAuthorized()` fails?

 * `configNTP()` should check when was the last time it was called
   and only call `configTime` if it is the first time or the previous
   call was at least 1 hour before. This would prevent us from hammering
   the NTP serve if the network connection is erratic.

 * Currently, we do not define the timezone; considering that the platform
   does not understand daylight saving time, should we?

 * We may want to control more than one door with a single MCU or have
   more than one reader (for example, an RFID reader and a keypad). Current
   code uses `openDoor()` etc. Therefore, to do that, we probably need to
   create a class to wrap around a "door", i.e., the internal and external
   card readers + the pin that controls the relay that actually opens the
   door, and then call `somedoor.open()` etc. instead.

 * Many things use "poor-man's parallel processing"; we should use
   actual tasks instead, but that uses additional memory...

   - It would be better (faster) if `captureIncomingData()` (which runs
     with interrupts disabled) could trigger a separate, high priority
     task to open the door (alternatively, sending/receiving data from
     mqtt, updating the DB etc. could run on a lower-priority task).

 * Instead of using a `#define`, we should try mounting the SD card; if
   that fails, use FFat.

 * At some point, we should ditch the arduino framework and use ESP-IDF
   directly, but this involves adapting YetAnotherArduinoWiegandLibrary

   - SD card:
     <https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32/api-reference/storage/fatfs.html>
     and maybe
     <https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32/api-reference/storage/sdmmc.html>
