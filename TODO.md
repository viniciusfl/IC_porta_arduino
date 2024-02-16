# Short-term TODOs

 * The DB is downloaded every time the MCU boots; during this time, the
   lock is unresponsive

 * Try to reduce memory usage (maybe reduce the maximum log file size
   to 2-3K, or store them as larger files and split them before sending)

 * Error handling: check return status of more function calls for
   memory allocation failures etc.

 * Choose and set license

 * The circuit board was created with <https://www.kicad.org/>

 * Make the circuit board:
   * <https://www.pcbway.com/>
   * <https://www.allpcb.com/activity/prototype2023.html>
   * In Brazil: <https://pcbbrasil.com.br>


# Non-critical TODOs

 * Include the mechanical "open" button and the corresponding resistors
   and diode to the schematic.

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

 * Better error handling everywhere (crashing is not really an option,
   in extreme situations we should at least try restarting the MCU)

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

 * When we initialize for the first time, there is no DB file on disk and
   we have never been subscribed to the DB topic before. Two things happen
   in parallel:

   1. After we subscribe, we start downloading the DB file, as it is a
      "retain" message
   2. We call `chooseInitialFile()`, which calls `forceDBDownload()` when
      there is no local file.

   Since there is no local DB on disk, `chooseInitialFile()` enqueues an
   unsubscribe/subscribe sequence in the mqtt client. This means that,
   when the download triggered by the first subscription is complete,
   we unsubscribe/subscribe and start downloading again. Not great, but
   not terrible either (fixing this is not entirely trivial).

 * Many things use "poor-man's parallel processing"; we should use
   actual tasks instead.
