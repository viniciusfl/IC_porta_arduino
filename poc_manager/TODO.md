 * Make sure that the output of `msg.payload.decode()` is really a string
   with the comma-separated list of data fields; if not, handle specially
   (for example, save as-is to a separate DB table)

 * Add the name of the DB file to monitor to `FILE_PATTERNS` and modify
   the expected extension for files in `commands` directory

 * Remove `\n` from commands

 * Whenever a new message is received (or, maybe, a new message of a
   specific type), check for messages stored in the DB with the "wrong"
   timestamp and fix them

 * The door controller should only "see" the authorized hashes, not
   the user names and other info
