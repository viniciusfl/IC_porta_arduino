# Short-term TODOs

 * Check whether DB downloading is working as expected

   - Download happens periodically and things continue to work during and
     after downloads

   - If no DB is available, the first DB is downloaded during init

 * Add `extern int doorID` to `common.h` and define it in `fechadime.cpp`,
   like `currentMillis`.

 * Change DB schema so that it has three tables:

   - "users" with only one column, "ID", which is the primary key (we may
     want to add more columns later, such as name etc.)

   - "doors" with only one column, "ID", which is the primary key (we may
     want to add more columns later, such as office name, building, floor
     etc.). In particular, we might add a boolean column "anybody" which
     means "any valid user can enter", so that we do not need to add one
     line for each user to the "auth" table in this case

   - "auth" with two columns: "userID" and "doorID", which are foreign keys
     (we may want to add more columns later, such as allowed hours etc)

   - to create:
     ```
     create table users(ID int primary key);
     create table doors(ID int primary key);
     create table auth(userID int not null, doorID int not null, foreign key (userID) references users(ID), foreign key (doorID) references doors(ID));
     create index useridx on auth(userID, doorID);
     ```

   - to query:
     ```
     select exists(select * from auth where userID=? and doorID=?);
     ```

 * Create a second sqlite DB for logs and log data to it

   - register the card ID, door ID, reader ID, unix timestamp, and whether
     access was authorized, unauthorized, or failed (some error ocurred)

   - In the future, `receivedDataError()` might log data here too

 * `WiFiStationDisconnected()` should not loop, just call `WiFi.begin()`
    once (this call does not block).

    - If the connection is not restablished with this, I think we will not
      be notified (we were already disconnected, so the callback will not
      be called again). So, we need to retry. On the main loop, add a call
      to `checkNetwork()` which, every few minutes, checks whether we are
      connected; if not, call `WiFi.begin()`.

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

   6. Replacing WiFiClient with WiFiClientSecure.

   I believe this is enough to make the nodeMCU check whether the server
   it connects to is correct. I do not really know how to make the server
   only accept connections from a valid client; should not be too hard to
   figure out.

 * Network initialization and callbacks:

   - Should we define net callbacks first and then initialize (this is
     what we do now), should we initialize and then define callbacks,
     or should we define callbacks and then call "disconnect" to have
     the callback perform initialization?

   - How to integrate net initialization with HW clock initialization
     and `configTime()`? What happens if DNS is not available when we
     call `configTime()`? Can we check if this is ok?

# Non-critical TODOs

 * Should we use `sntp_set_time_sync_notification_cb()` to synchronize
   the HW clock?

 * wiegand `stateChanged()` and `receivedDataError()` callbacks should
   be more useful

 * Should we modify the code so that, if there is no HW clock, we
   stand by until we connect to the NTP server for the first time?

 * Should we check whether we are connected before trying to download
   updates to the DB?

 * When starting up for the very first time, we want to call `startDownload()`
   during init; we should (1) check whether we are connected and (2)
   check whether download succeeded

 * `chooseInitialDB()` should check whether the chosen DB exists and
   is valid; if not, it should try to download

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

