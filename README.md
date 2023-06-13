# Tramela - nodeMCU-based Access Control System

This project has as objective the construction of a **Key Card Door Lock** using basic hardware and software.

**Materials used for the *hardware*:**

1. Doit ESP32 devkit V1
2. Real Time Clock RTC DS1307
3. Micro SD Card Module
4. Breadboard
5. Jumpers
6. Intelbras LE 130 MF
7. Logic Level Converter

## What does it do

This is an access control system that opens a door for authorized users.
On each side of the door, there is a Wiegand reader (RFID/MIFARE, QR Code
etc.); when the user presents their ID (a 32-bit number), the system logs
that information and, if the ID is authorized, opens the door.

The DB with the authorized users is stored in an SD card. The system
interacts over MQTT with a server to (1) download updated versions of
the DB and (2) upload the latest access logs over wifi. System time is
synchronized with NTP. Network outages do not prevent the system from
working, only DB updates and log uploads are affected.

## Initialization steps

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

6. Check if there is a valid DB file available on disk; if not, block
   waiting to receive it from MQTT.

7. Start operating; unless steps 4 or 6 blocked, we should be ready to go
   less than a second after the microcontroller is turned on.

After the initialization is complete, we may receive updated versions of
the DB file over MQTT; when this happens, we save the new file to disk
and, after downloading is complete, swap the new file for the old one.

## Main loop

1. Periodically check whether the current logfile is getting too large
   and, if so, close it and open a new one.

2. Periodically check whether there exists a closed log file that needs
   to be uploaded to the controlling server; if so, send it over MQTT.
   When the upload is complete, the file is deleted. Only one file is
   sent at a time in order to conserve memory and to guarantee we never
   delete a file prematurely.

3. Periodically check whether we are connected; if this is false for too
   long, reset the network.

4. Periodically, check whether the system time and HW clock time have
   drifted apart too much; if so, update the HW clock time. This assumes
   the system time is more accurate, which is usually true as we should
   be synchronized with NTP.

5. On every iteration, check whether an authorized card was read and,
   if so, open the door.

