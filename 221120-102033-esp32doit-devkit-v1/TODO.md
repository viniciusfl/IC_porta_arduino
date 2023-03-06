# Short-term TODOs

 * Choose and set license

 * REST API

   - open door, update the DB, reboot, recent log messages, recent
     authorization successes and failures, what else?

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

 * Draw printed circuit board

 * Actually open the door; we may use an ordinary logic level converter
   (https://www.sparkfun.com/products/12009 ) for that (although all
   pages about this device talk about "3.3-5V", the datasheet for the
   BSS138 allows for up to 50V Drain-Source and +-20V Gate-Source DDP)

# Non-critical TODOs

 * The server is turned on or off based on WiFi events. When the system
   gets an IP, we turn on the server, and when it disconnects from WiFi,
   we turn of the server. This is necessary because if we disconnect from
   WiFi and don't do this, the server stops working.
   (https://github.com/espressif/esp-idf/tree/778aeae99ebe7d0fe437ae4aa73a9f74c5a83668/examples/protocols/https_server/simple)

 * Better error handling everywhere (crashing is not really an option,
   in extreme situations we should at least try restarting the MCU)

 * We should be able to define the door ID, network credentials, TLS
   credentials etc. at runtime, not hardcode them in the code, but how?

   - When writing the code to the MCU, add the relevant information to
     the NVS or to SPIFFS. This is not really "at runtime", but maybe
     it's enough: since the code itself is the same for all MCUs, future
     code upgrades (including OTA) are easy.

   - Store this info in an encrypted file in the SD card. We save a pair
     of public and private keys to each nodeMCU NVS and provide the user
     with a program, phone app or javascript-based web page that collects
     the data from the user (including the MCU's public key), encrypts it,
     and returns an encrypted file for the user to save to the SD card.
     This forces us to always have an SD card (or maybe we could connect
     an SD card just for this initial step), to know the public key of
     each nodeMCU (or use the same for all, which is not a great idea),
     and to create a (admittedly, very simple) dedicated program just for
     the initial configuration.

   - When starting for the first time (or after pressing a specific button),
     initiate some form of network access; either start as an access point
     (which may pose difficulties if we want to interact with a phone app,
     as the phone would probably reject the connection because it does not
     route to the Internet) or connect to a predefined wifi network (the
     network name and pre-shared key may be stored in the NVS), which the
     user may easily create using their phone. Either way, after the network
     connection is operational, either:

     * Provide a web form for the user to fill in (the form should also
       inform the user about the MCU's public key);

     * Use mDNS to download the data from a hardcoded local URL and also
       to upload the MCU's public key;

     * Use mDNS to connect to a local MQTT broker and exchange the data.

     * This alleviates the need for an SD card. While it involves creating
       a dedicated program or phone app, that program is needed anyway if
       we are to interact with the MCU over the network.

   - Something similar, but using bluetooth.

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

 * It does not make much sense to save the downloaded hash to a file;
   we should just save it to a buffer

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

 * `Authorizer::userAuthorized()` calls `sqlite3_reset()`; if authorization
   latency becomes a problem, we might call that after the door is opened,
   so the query is ready to run on the next iteration. This, however, is
   not trivial, because the DB may be updated at some point.
