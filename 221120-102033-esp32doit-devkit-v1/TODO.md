# Short-term TODOs

 * REST API

   - open door, update the DB, reboot, recent log messages, recent
     authorization successes and failures, what else?

 * When a backup of the log database is created successfully, we should
   empty the log database. No entries will be lost because, when the
   backup is complete, both databases are guaranteed to be the same if we
   are using the same connection for access and for the backup: "If the
   source database is modified by the using the same database connection
   as is used by the backup operation, then the backup database is
   automatically updated at the same time." -
   <https://www.sqlite.org/c3ref/backup_finish.html>

 * Upload and purge logs

   - We may periodically PUT the log files to a server and delete them
     after the upload is successful; OR

   - We may offer a REST API to list the log files available for
     downloading. We may either keep old files for several days before
     deleting them or offer a REST API for the client to delete the files
     it already has.

 * Save system log messages (not just door access messages) to an sqlite
   log DB.

 * Smarter logs: we should have an in-memory circular buffer for the log
   messages. During startup, before the log db is available, messages are
   kept there and and then flushed to the DB. During normal operation,
   they are flushed immediately, but the buffer is accessible over REST
   so that we can see the latest messages even if something goes wrong
   with the log db.

 * Master key: insert a hardcoded ID that is always able to open any
   door without the need to check the DB. This should work even if the
   MCU crashes for some reason (SD not accessible, unable to set the
   clock etc.).

 * Draw printed circuit board

 * Actually open the door; we may use an ordinary logic level converter
   (https://www.sparkfun.com/products/12009 ) for that (although all
   pages about this device talk about "3.3-5V", the datasheet for the
   BSS138 allows for up to 50V Drain-Source and +-20V Gate-Source DDP)

# Non-critical TODOs

 * Better error handling everywhere (crashing is not really an option,
   in extreme situations we should at least try restarting the MCU)

 * We should be able to define the door ID, network credentials, TLS
   credentials etc. at runtime, not hardcode them in the code, but how?

   - When writing the code to the MCU, add the relevant information to
     the NVS or to SPIFFS. This is not really "at runtime", but maybe
     it's enough: since the code itself is the same for all MCUs, future
     code upgrades (including OTA) are easy.

   - When starting for the first time (or after pressing a specific
     button), prompt the user over USB/Serial to provide the necessary
     data. This is a little simpler for the user than the previous
     option, but still involves having specific tools in a computer
     and may present difficulties with nodeMCUs that fail to communicate
     over USB in some circumstances. Probably not worth it.

   - When starting for the first time (or after pressing a specific
     button), work as an access point for the user to connect to; the
     user then loads and fills up a web form. This would not work so
     well for phones: they would reject the connection because it does
     not route to the Internet.

   - Something similar, but using bluetooth (maybe with a dedicated
     phone app).

   - Store this info in an encrypted file in the SD card. We may have
     a web server somewhere with a form the user fills in; the server
     then returns an encrypted file that the user writes to the SD card.
     This involves trusting this server (it has the shared key used by the
     MCU and sees the unencrypted data), but is reasonably user-friendly.
     Another problem is that this forces us to always have an SD card
     (or maybe we could connect an SD card just for this initial step).

   - We may do something similar, but with asymetric keys. In this case,
     we could provide a program, phone app or javascript-based web page
     that collects the data from the user, encrypts it, and returns an
     encrypted file for the user, who does not need to trust any third-
     party with their credentials. We may even store the private key in
     the NVS, which allows us to use different keys for different MCUs.

   - When starting for the first time (or after pressing a specific
     button), connect to a default wifi access point and download the
     encrypted data from a default server and URL (the access point,
     the pre-shared key to access it, the server, and the URL may be
     set in the NVS). This alleviates the need for an SD card, but
     involves providing a temporary access point and having a server
     to host the files (it may be a temporary server, maybe something
     in the local network).

   - A combination of the above: if the data is not in the NVS or SPIFFS,
     check the SD card; if it is not there, try to connect to the default
     access point; if it is not available, start as an access point.

 * `*TimestampFile` is not a good name for the files with the metadata
   about the DB files (but check the next item)

 * The two `*TimestampFile` files are not really necessary; a single file
   with the name (or number) of the correct DB is enough (if the file does
   not exist or is corrupted, the code already has a default). Another
   option is to use a second sqlite DB file instead of a plain text file
   for this, which gives us consistency guarantees. If there is a problem
   with multiple "things" writing to the SD card at the same time, we
   can use the preferences library instead.

 * We might record things such as the doorID and net credentials in the
   NVS (non-volatile storage) with the preferences library or in the SPIFFS:
   https://randomnerdtutorials.com/esp32-vs-code-platformio-spiffs/
   https://blog.espressif.com/building-products-creating-unique-factory-data-images-3f642832a7a3

 * The minimum size of an SQLite database is one page for each table and
   each index, and the minimum page size is 512 bytes.
   https://www.sqlite.org/pgszchng2016.html . If we change the page size
   to 512 bytes:
   https://www.oreilly.com/library/view/using-sqlite/9781449394592/re194.html
   a DB with a small number of entries will probably be 2-4KB and fit
   the NVS. We could use that to store everything we need.

 * It is probably safe to use up to 5KB of NVS space:
   https://stackoverflow.com/a/58562855/15695987
   But it is also very easy to resize the NVS, check `board_build.partitions`
   in `platformio.ini`

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

 * Update the DB using alarms instead of time intervals?

 * Add some sort of timeout for the download

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
