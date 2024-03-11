# Short-term TODOs

 * When logging access, we should register the hash, not the ID, so
   if someone steals a logfile they cannot fabricate a valid key card

 * We should add some more options for "remote control":
   - A "master key" that forces a reboot, so we can do it even if mqtt
     communication fails
   - An mqtt command to restore the previous firmware version *and*
     another "master key" that does the same
   - An mqtt command to rotate the logfile (i.e., close the current
     file and open a new one)

 * Error handling: check return status of more function calls for
   memory allocation failures etc.

 * Divide `logmanager.cpp` in two files, one for logging and the other
   for the log manager, i.e., the class that manages and uploads the
   actual log files.

 * Minor bug: if we are currently sending a logfile and get disconnected,
   the message is still enqueued. When we reconnect, we may enqueue another
   logfile before the previous one is completely sent, consuming more memory.

 * It would be better (faster) if `captureIncomingData()` (which runs
   with interrupts disabled) could trigger a separate, high priority
   task to open the door (alternatively, sending/receiving data from
   mqtt, updating the DB etc. could run on a lower-priority task).
   However, this would consume more memory.

 * If we need to further reduce memory usage, we could give up logging
   to disk files and use the DB for logging, but this complicates
   updating the DB (we would have to defer updates to when there are
   no logs to be sent).

 * Choose and set license

 * The circuit board was created with <https://www.kicad.org/>

 * Make the circuit board:
   * <https://www.pcbway.com/>
   * <https://www.allpcb.com/activity/prototype2023.html>
   * In Brazil: <https://pcbbrasil.com.br>


# Non-critical TODOs

 * Include the mechanical "open" button and the corresponding resistors
   and diode to the schematic.

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

 * Many things use "poor-man's parallel processing"; we should use
   actual tasks instead, but that uses additional memory...

 * Instead of using a `#define`, we should try mounting the SD card; if
   that fails, use FFat.

 * The way we handle log messages in the callbacks in `cardreader.cpp`,
   with the `connectedMsg` etc buffers, is lame and wastes memory.

 * At some point, we should ditch the arduino framework and use ESP-IDF
   directly, but this involves adapting YetAnotherArduinoWiegandLibrary
