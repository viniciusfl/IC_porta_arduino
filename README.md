
# Project of Scientific Research (CNPq)

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
periodically downloads new versions of the DB and uploads the latest
access logs over wifi. System time is synchronized with NTP. Network
outages do not prevent the system from working, only DB updates and
log uploads are affected.

## Initialization steps

1. Initialize WiFi connection; this is asynchronous, so the network may
   not be up right away.

2. Define a callback that sets the time using NTP after the network is up.

3. If there is a HW clock and it is active, use it to set the system time;
   if not, wait for NTP synchronization. Therefore, this step only blocks
   waiting for the network if we depend on NTP to figure out the date.

4. Check if there is a valid DB file available on disk; if not, download
   one. Therefore, this too only blocks waiting for the network if there
   is no DB available.

5. Start operating; unless steps 3 or 4 blocked, we should be ready to go
   less than a second after the microcontroller is turned on.

## Main loop

1. Periodically check whether we are connected; if this is false for too
   long, reset the network

2. Periodically, download the DB checksum from the server; if it is
   different from the local one, download the new DB and make it active.
   These downloads do not block; with HTTP, they are handled by a callback,
   with TCP sockets we process a chunk of data on each main loop iteration.

3. Periodically, check whether the system time and HW clock time have
   drifted apart too much; if so, update the HW clock time. This assumes
   the system time is more accurate, which is usually true as we should
   be synchronized with NTP.

4. On every iteration, check whether a card was read and, if so, open
   the door.

## Details

There is some old code that uses TCP sockets to download updated versions
of the DB; it is just a draft that may be useful in the future should we
not be able to use HTTP.

With HTTPS, we define our own Certificate Authority to create the
certificates for the server and each of the clients. Since we *only* use
this CA (and none of the "normal" ones), there is no need for other forms
of authentication: we always know we are talking to a trusted party.
