# Tramela - nodeMCU-based Access Control System

A keycard-based door lock controller using an ESP32 and Wiegand readers.

## What does it do

This is an access control system that opens a door for authorized users.
On each side of the door, there is a Wiegand reader (RFID/MIFARE, QR Code
etc.); when the user presents their ID (a 32-bit number), the system logs
that information and, if the ID is authorized, opens the door (by changing
the logical level of a microcontroller port, which activates a relay). The
"outgoing" reader is optional and may be replaced by a simple "open door"
button. It should be easy to adapt this system to open different types of
doors, gates etc.

The DB with the authorized users is stored either in the ESP32 flash or
in an SD card, as are the log files that register user access and system
events. The authorized user IDs are stored hashed, so even if someone
steals a controller they are unable to obtain useful data.

The system connects over WiFi to an MQTT broker which, in turn, communicates
with a server. This allows the door controller to upload the latest logs
to that server. Conversely, the server may instruct the door controller to
update the authorization DB, update the firmware code, and execute one-off
commands such as "reboot" or "open the door now". This communication is
secured by TLS certificates, so it is encrypted and only someone with access
to one of the known certificates can connect to the broker. If someone
steals a controller, they will have access to the certificates and one
of the certificate keys; this would allow them to obtain a copy of the
DB file (but, as mentioned, auth data is hashed) and upload fake log data,
but not to interfere with other controllers on the same network. Network
outages do not prevent the system from working, only cause delays to DB
updates and log uploads.

System time is synchronized with NTP; a battery-backed hardware module
provides correct time information right after boot, even before the
network connection is established. The system will function correctly
without it but, in that case, on every boot it will only start operating
after the initial NTP synchronization.

# Current status and limitations

The code that runs on the controller is functional and has been working
on a test site for over a month without problems. The server-side manager
code is just a proof of concept; adding and removing users involves using
the `sqlite3` command-line tool. Network credentials and addresses are
currently hardcoded in the source code itself.

