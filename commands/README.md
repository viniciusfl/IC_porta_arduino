Both `watchdog.observers.Observer.start()` and `paho.mqtt.client.loop_start()`
use a single additional python thread each by means of the python `threading`
API (such threads are subject to the GIL, but that does not matter much to
us).

The use of a single thread each implies that, while an event is being
handled, other incoming events have to wait and, therefore, it is a good
idea to make the handlers short-lived if performance may be an issue. We
have four options to deal with this, from "best" to "worst" (but all
should work fine, even the last one):

 1. Use the `selectors` module to prevent blocking; this would involve
    sending data from the watchdog and mqtt threads to the main thread
    (freeing those threads) and using selectors in the main thread.

 2. Use the `threading` module to do the actual work on separate threads.

 3. Use the `multiprocessing` module to do the actual work on separate
    processes.
 
 4. Assume this will not cause performance problems and ignore it.
