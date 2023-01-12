# Short-term TODOs

 * Check whether DB downloading is working as expected

   - Download happens periodically and things continue to work during and
     after downloads

   - If no DB is available, the first DB is downloaded during init

 * Check whether time management is working as expected

   - Time is set from the HW clock on initialization

   - NTP is configured

   - Time is periodically synchronized

 * Handle connection errors, interrupted downloads etc. and verify
   checksums at the end of each download. This means we actually
   have to download two files: the checksum (and check that is
   has the correct size) and the real file.
   https://techtutorialsx.com/2018/05/10/esp32-arduino-mbed-tls-using-the-sha-256-algorithm/

 * Implement `blinkOk()` and `blinkFail()`

# Medium-term TODOs

 * Remove card reader code that does not use interrupts

 * Add mechanism to download/upload and rotate logs:

   - Periodically use the backup API (https://www.sqlite.org/backup.html)
     to save the logs to a separate file (for example, one file per day).
     Use the date as the file name.

   - After the backup is done, generate the checksum for the file

   - After the checksum is generated, clear the main log DB

   - Should someone connect periodically to download these files or
     should we upload them (with "PUT")? How do we know when this
     was successful so we can delete the file? Maybe just keep them
     for a long time and delete them when they are some months old?

 * Actually open the door (probably use a transistor to activate the
   relay)

 * Modify the net client in dbmanager to use HTTPS and certificates, and
   actually check the certificates. This involves:

   1. Creating a local root certificate (fake Certificate Authority) -
      look up on how to create a self-signed certificate or a snake oil
      certificate

   2. Creating a certificate for the nodeMCU and another for the test web
      server

   3. Signing both with the fake CA certificate

   4. Adding the fake CA certificate, the nodeMCU certificate and the
      private key for the nodeMCU certificate to the nodeMCU

   5. Adding the fake CA certificate, the test webserver certificate and
      the private key for the webserver certificate to the web server

   6. Replacing WiFiClient with WiFiClientSecure. NOPE! We should use
      this: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/esp_http_client.html
      To download the checksum, we should use `esp_http_client_perform()`
      with `is_async`. For the DB, we should follow the "HTTP Stream"
      section. This allows us to check the status code (200) and the
      expected size of the file (content-length) while also ditching
      our code to skip the HTTP headers.

   I believe this is enough to make the nodeMCU check whether the server
   it connects to is correct. I do not really know how to make the server
   only accept connections from a valid client; should not be too hard to
   figure out.

 * Network initialization and callbacks:

   - How to better integrate net initialization and HW clock initialization?

# Non-critical TODOs

 * When downloading from the network, we should check the response
   code (200) and the record the expected file size to verify later

 * Substitute the debugging messages with a call to "log()", which
   does print to serial but also save events to the sqlite log DB.

 * `*TimestampFile` is not a good name for the files with the metadata
   about the DB files (but check the next item)

 * The two `*TimestampFile` files are not really necessary; a single file
   with the name (or number) of the correct DB is enough (if the file does
   not exist or is corrupted, the code already has a default). Another
   option is to use a second sqlite DB file instead of a plain text file
   for this, which gives us consistency guarantees. If there is a problem
   with multiple "things" writing to the SD card at the same time, we
   can use the preferences library instead.

 * We should record things such as the doorID and net credentials in the
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

 * Names "1" and "2" for the wiegand readers is not very inspired

 * Should we use `sntp_set_time_sync_notification_cb()` to synchronize
   the HW clock?

 * wiegand `stateChanged()` and `receivedDataError()` callbacks should
   be more useful

 * Should we check whether we are connected before trying to download
   updates to the DB?

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

 * Rewrite bitsToNumber without `String` (see `verifyChecksum()`)

 * Before downloading the DB, download the checksum to verify whether
   it has changed; if not, skip downloading
