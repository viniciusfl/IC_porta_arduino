# Why we do things this or that way

Some important requirements:

1. In most cases, we have a handful of authorized users for a door.
   However, we may sometimes need up to 200,000 registered users (really),
   so lookup time is a concern.

2. We may want to add restrictions, such as limiting access for some users
   to weekdays or daytime only.

3. We need to be able to update the list of authorized users remotely and,
   if there are many doors, for all of them without too much hassle.

4. We want to save access logs locally (to guarantee persistence) and send
   them over the network to be stored remotely.

5. We want to keep working even if the network is temporarily unavailable.

6. All network communication should be encrypted and authenticated.

Therefore:

1. Requirements 1 and 2 can be easily solved using SQLite, so we use that.

2. Requirement 6 means we should use TLS and certificates (no need for
   additional user/password authentication: anyone with a valid certificate
   can communicate).

3. Requirement 4, together with the potentially large number of users,
   means that, while we may use the ESP32 internal storage, we should also
   support an additional SD card

4. We considered using HTTP for network communication, which would allow
   us to work without a dedicated server program. Instead, we could create
   a REST service allowing us to send commands (such as "open the door now"
   and even "give me the latest log files") directly from a web browser to
   the door controller. To update the DB, we could offer it for download
   on a standard web server and have the door controller periodically check
   whether it has been updated. Unfortunately, the memory overhead for two
   open encrypted connections is too large. Using a single connection would
   force us to have a dedicated server to connect to, forfeiting the benefits
   of using HTTP, so we chose to use MQTT instead.

5. We would like to save the log of events to a second SQLite DB. That was
   not possible because managing two SQLite DBs simultaneously consumes
   too much memory and because there might be problems with reentrant calls,
   so we log to ordinary disk files instead.

6. We could update the SQLite DB incrementally, with MQTT messages like
   "add/remove this authorized user". If we did that, we could also have
   a secondary table in the same DB for logging. In spite of that, we
   chose to always upload a complete DB when there are changes because
   (1) incremental updates might fail more often and (2) we already need
   to upload a complete DB to initialize/reset a door controller. Still,
   we may review this in the future.

# Initialization steps

1. If available, read the current time from the HW clock.

2. Initialize WiFi connection; this is asynchronous, so the network may
   not be up right away.

3. Define a callback that sets the time using NTP after the network is up.

4. If time was not previously set from the HW clock, block waiting for
   NTP synchronization. However, even if we are blocked, we still check
   the card readers to open the door for someone with the master key.

5. Initialize the MQTT client; this does not block, processing is done
   in the background. Once connection is established, subscribe to the
   topic with the DB updates.

6. Check if there is a valid DB file available on the SD card; if not,
   block waiting to receive it from MQTT, but also continue checking
   for accesses with the master key.

7. Start operating; unless steps 4 or 6 blocked, we should be ready to
   go a few seconds after the microcontroller is turned on.

After the initialization is complete, we may receive updated versions of
the DB file over MQTT; when this happens, we save the new file to disk
and, after downloading is complete, swap the new file for the old one.

We log everything to a file; when this file gets "big", we close it and
open a new one. Closed files are eventually sent to the controlling
server and deleted.

# Main loop

1. Periodically check whether there exists a closed log file that needs
   to be uploaded to the controlling server; if so, send it over MQTT.
   When the upload is complete, the file is deleted. Only one file is
   sent at a time in order to conserve memory and to guarantee we never
   delete a file prematurely.

2. Periodically check whether we are online; if this is false for too
   long, reset the network.

3. Periodically check whether the system time and HW clock time have
   drifted apart too much; if so, update the HW clock time. This assumes
   the system time is more accurate, which is usually true as we should
   be synchronized with NTP.

4. On every iteration, check whether an authorized card was read and,
   if so, open the door.

# SQLite DB schema

 * Three tables:

   - "users", with the hashed ID and the user name

   - "doors", with the door number and a description/location (we may
     add a boolean column "anybody" meaning "any valid user can enter",
     so that we do not need to add one line for each user to the "auth"
     table in this case)

   - "auth", joining the other two (we may add more columns later, such
     as allowed hours etc)

 * To create:
   ```
   create table users (ID text primary key, name text);
   create table doors (ID integer primary key, location text);
   create table auth (userID text not null, doorID integer not null, foreign key (userID) references users(ID), foreign key (doorID) references doors(ID));
   create index useridx on auth(userID, doorID);
   ```

 * To add:
   - A door:
     ```
     insert into doors values (doornum, 'the door location');
     ```
   - A user:
     ```
     insert into users values ('thehash', 'the name');
     ```
   - User authorization for a given door:
     ```
     insert into auth values ('thehash', doornum);

 * To query:
   ```
   select exists(select * from auth where userID=? and doorID=?);
   ```
