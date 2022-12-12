// On every loop, we check for the amount of time passed since we last
// did some operations. Instead of calling millis() everywhere, we call
// it only once per loop and store the value here. This *might* save
// some processing (or maybe not) and *might* prevent races if millis()
// changes within a single iteration.
extern unsigned long currentMillis;

// Since we want to avoid blocking calls, lots of things that we do
// happen piecewise, at each loop iteration. So, to pass information
// from the card reader to the DB management code, we do not use
// function calls; we set some globals instead:
extern bool isSearching; // Is there a user trying to open a door?
extern unsigned long int currentCardID; // If so, this is his ID number
extern int currentCardReader; // And it came from this reader

extern bool downloading; // Is there an ongoing DB update?

