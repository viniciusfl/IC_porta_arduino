# Short-term TODOs - Vinicius

 * Certificates etc. in mqttmanager.cpp

 * CHECK WHETHER THIS IS WORKING OK:
   - Use `event->msg_id` to identify MQTT messages:

   - When we receive a `MQTT_EVENT_PUBLISHED` event, we know that the
     file we sent was received and, therefore, we delete it. We know
     what file to delete because we only send one file at a time.
     However, there *may* be a race condition: right after boot, we
     send a file and then we receive an acknowledgement for a file we
     sent before the boot. This would make us delete the wrong file.
     We should check somehow that the ack corresponds to the correct
     file; if it does not, we do nothing and we will probably send the
     same file again later on, which is ok.

 * Handle other possible MQTT errors.

 * Check whether openDoor, denyToOpenDoor, blinkOK, and blinkFail do
   what they are supposed to do:

   - We should choose an output pin to activate the door; this pin
     should *not* switch levels during boot! Then openDoor should
     change the level of the pin for some 300ms.

# Other short-term TODOs

 * Limit log file size:

   - The code in `processLogs()` that checks whether there are too many
     messages in the current log file should be migrated to `logEvent()`
     and `logAccess()` - CHECK IF THIS IS OK

   - The code should not only check the total number of messages but
     also the size the file would become if the current message were
     added to it; if that value is too large, rotate the log. A good
     limit is probably 5KB. - CHECK IF THIS IS OK

   - After we make sure no file will be larger than 5KB, eliminate the
     "malloc" in LogManager::sendNextLogfile() and use a fixed buffer
     instead (check TODO comments).

 * To actually open the door, we may use an ordinary logic level
   converter (https://www.sparkfun.com/products/12009 ): although all
   pages about this device talk about "3.3-5V", the datasheet for the
   BSS138 allows for up to 50V Drain-Source and +-20V Gate-Source DDP

 * Choose and set license

 * The circuit board was created with <https://www.kicad.org/>

 * Make the circuit board:
   * <https://www.pcbway.com/>
   * <https://www.allpcb.com/activity/prototype2023.html>
   * In Brazil: <https://pcbbrasil.com.br>


# Non-critical TODOs

 * Implement MQTT command to retrieve the contents of the log ringbuffer

 * Implement MQTT commands to handle files in the disk: download and
   delete log files, eliminate all DB files to "reset" the controller,
   change the device ID and credentials etc.

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

 * Modify the format of the log messages - is this done already???

 * Better error handling everywhere (crashing is not really an option,
   in extreme situations we should at least try restarting the MCU)

 * Truly reentrant logging: the current code is thread-safe, but probably
   not reentrant (it uses locks to access the queue). We might use a set
   of shared pointers and semaphores instead of a queue, akin to what
   was used up to commit d5a28e01e803021ed794a043f3aeebbc8cc39757.

 * We should be able to define the door ID, network credentials, TLS
   credentials etc. at runtime, not hardcode them in the code, but how?

   - When writing the code to the MCU, add the relevant information to
     the NVS. This is not really "at runtime", but maybe it's enough:
     since the code itself is the same for all MCUs, future code upgrades
     (including OTA) are easy.

   - When writing code to the MCU, store in NVS (1) a pair of private
     and public keys that identify this MCU; (2) a public key that
     identifies a trusted MQTT server; and (3) the name of a default
     wifi network, along with its pre-shared key (or maybe no key
     at all). When starting for the first time (or after pressing
     a specific button), the MCU connects to the specified network
     (which the user may easily create using their phone). After the
     network connection is operational, the MCU uses mDNS to connect
     to a local MQTT broker and exchange the data. If an attacker has
     physical access to the MCU *and* compromises the server key, they
     can take control of the door. So, for a generic system, a phone
     app may be a problem, because everybody has access to the key.

   - Check this out: <https://docs.espressif.com/projects/esp-idf/en/v4.4.5/esp32/api-reference/provisioning/index.html>

 * ESP32 supports FAT filesystems. If the flash memory contains a
   partition labelled "ffat", it mounts that partition at the `/ffat`
   path (check `tools/partitions/ffat.csv`). It also offers a "FFat"
   object that apparently is similar to the "SD" object. FFat allows
   up to 10 simultaneously open files (but it consumes 48KB of memory,
   so adding an ffat partition reduces the available memory). The SD
   card allows up to 5 simultaneously open files, so its memory
   overhead is lower. In any case, this can be changed in the call
   to `begin()`.
   https://randomnerdtutorials.com/esp32-vs-code-platformio-spiffs/
   https://blog.espressif.com/building-products-creating-unique-factory-data-images-3f642832a7a3

 * Should we use `sntp_set_time_sync_notification_cb()` to synchronize
   the HW clock?

 * wiegand `stateChanged()` and `receivedDataError()` callbacks should
   be more useful

 * Hardcoded net credentials and URLs - maybe move to common.h ?

 * Hardcoded DB names - maybe that's ok?

 * Namespaces vs classes

 * Some better form of error messages - maybe blink the board led,
   maybe add an extra led just for that, and use specific patterns
   to identify each error

 * What to do if `openDB()` fails?

 * What to do if `userAuthorized()` fails?

 * `configNTP()` should check when was the last time it was called
   and only call `configTime` if it is the first time or the previous
   call was at least 1 hour before. This would prevent us from hammering
   the NTP serve if the network connection is erratic.

 * Currently, we do not define the timezone; considering that the platform
   does not understand daylight saving time, should we?

 * We may want to control more than one port with a single MCU. Current
   code uses `openDoor()` etc. Therefore, to do that, we probably need to
   create a class to wrap around a "door", i.e., the internal and external
   card readers + the pin that controls the relay that actually opens the
   door, and then call `somedoor.open()` etc. instead.

 * `Authorizer::userAuthorized()` calls `sqlite3_reset()`; if authorization
   latency becomes a problem, we might call that after the door is opened,
   so the query is ready to run on the next iteration. This, however, is
   not trivial, because the DB may be updated at some point.

 * Implement OTA

