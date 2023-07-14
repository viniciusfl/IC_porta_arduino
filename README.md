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
that information and, if the ID is authorized, opens the door (by changing
the logical level of a microcontroller port, which activates a relay).

The DB with the authorized users is stored in an SD card, as are the log
files that register user access and system events. The system connects
over WiFi to an MQTT broker which, in turn, communicates with a server.
This allows the door controller to (1) download updated versions of the
DB, (2) upload the latest logs files, and (3) execute one-off commands,
such as "reboot" or "open the door now". This communication is secured
by TLS certificates, so it is encrypted and only someone with access to
the correct certificate can connect to the broker. Network outages do
not prevent the system from working, only cause delays to DB updates
and log uploads. System time is synchronized with NTP; a battery-backed
hardware module provides correct time information right after boot, even
before the network connection is established (the system will function
correctly without it but, in that case, on every boot it will only start
operating after the initial NTP synchronization).

