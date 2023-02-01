# Short-term TODOs

 * Implement `blinkOk()` and `blinkFail()`

# Medium-term TODOs

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

 * Actually open the door; we may use an ordinary logic level converter
   (https://www.sparkfun.com/products/12009 ) for that (although all
   pages about this device talk about "3.3-5V", the datasheet for the
   BSS138 allows for up to 50V Drain-Source and +-20V Gate-Source DDP)

# Non-critical TODOs

 * Save log messages to the sqlite log DB.

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

 * Before downloading the DB, download the checksum to verify whether
   it has changed; if not, skip downloading

 * We are using almost 100% of the storage space; consider using some
   of the tips from https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/performance/size.html ,
   especially those related to mbedTLS, but also maybe disabling
   IPv6, using the newlib nano formatting, and modifying the compiler
   optimization. `CONFIG_ESP32_REV_MIN` also seems interesting. Compilation
   options are defined with `build_flags` in platformio.ini:
   https://docs.platformio.org/en/latest/platforms/espressif32.html
   Apparently, the arduino framework for ESP32 presets most relevant
   variables, so we need to figure out how to override that.
