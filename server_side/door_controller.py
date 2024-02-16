#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#----------------------------------------------------------------------------
# Created Date: 28/05/2023
# ---------------------------------------------------------------------------
"""This code handles the creation of an MQTT client that does three things:

   1) Watch the users.db file and send it to the nodeMCU whenever it
      is changed;

   2) Watch the commands directory and, when a new file is created,
      read its content, send it to the nodeMCU, and delete the file;

   3) Receive log messages from other clients by subscribing to the
      appropriate topic. When a new message is received, save it in
      an sqlite DB according to its type (access or system)"""
# ---------------------------------------------------------------------------

import ssl, sys, time, logging, sqlite3, inspect, os, random, time

#BROKER_ADDRESS = '10.0.2.109'
#BROKER_PORT = 8883
BROKER_ADDRESS = "localhost"
BROKER_PORT = 1883

SLEEP_TIME = 1

CMD_DIR = "."
CMD_PATTERNS = ["*.txt"]
DB_DIR = "upload"
DB_PATTERNS = ["*.db"]
FM_PATTERNS = ["*.bin"]


DB_NAME = "messages.db"
TABLES = {
    "access": "bootcount INT, \
                time VARCHAR(40),\
                door INT,\
                reader INT,\
                authorization INT,\
                card INT,\
                UNIQUE (time, door, bootcount, reader, card, authorization)",

    "system" : "bootcount INT, \
                time VARCHAR(40),\
                door INT,\
                message VARCHAR(1024),\
                UNIQUE (time, bootcount, door, message)"
}


from paho.mqtt import client as mqtt_client

class OurMQTT():
    def __init__(self):      
        self.database = DBwrapper()
        self.init_mqtt_client()

    def init_mqtt_client(self):
        self.client_id = f'python-mqtt-server'
        self.client = mqtt_client.Client(client_id = self.client_id,
                                         clean_session = False,
                                         userdata = self)

        def on_connect(client, userdata, flags, rc):
            if rc == 0:
                print("Connected to MQTT Broker!")
                self.subscribe("logs")
            else:
                print(f"Failed to connect, return code {rc}\n")
        self.client.on_connect = on_connect

        def on_message(client, userdata, msg):
            userdata.on_message(client, msg)
        self.client.on_message = on_message

        self.client.tls_set(ca_certs="../certs/rootCA.crt",
                            certfile="../certs/python.crt",
                            keyfile="../certs/python.key",
                            cert_reqs = ssl.CERT_REQUIRED,
                            tls_version=ssl.PROTOCOL_TLSv1_2)
        # Do not verify whether the hostname matches the CN
        self.client.tls_insecure_set(True)

        self.client.connect_async(BROKER_ADDRESS, BROKER_PORT, 60)
        self.client.loop_start()

    def publish(self, topic, filename):
        print(f"publishing {filename} on topic {topic}")
        try:
            with open(filename, mode="rb") as f:
                if topic == "database":
                    result = self.client.publish(f"/topic/{topic}",
                                                f.read(),
                                                retain=True, qos=2)
                else:
                    result = self.client.publish(f"/topic/{topic}",
                                                f.read())

                if topic == "commands":
                    os.remove(filename)
                print("Published with success\n") if result[0] == 0 else print("Publish failed!\n")
        except:
            print(f"File {filename} not found!")
        
        time.sleep(1)


    def subscribe(self, topic):
        print(f"subscribing to topic {topic}")
        self.client.subscribe(f"/topic/{topic}", 1)


    def on_message(self, client, msg):
        print(f"Received msg from `{msg.topic}` topic")
        try:
            decoded_payload = str(msg.payload.decode("utf-8"))
        except:
            return
        if msg.topic == "/topic/logs":
            self.process_incoming_log_messages(decoded_payload)
        else: # This should only happen during testing
            print(f'msg received from topic {msg.topic}: \n {decoded_payload} \n')


    def process_incoming_log_messages(self, messages):
        for msg in messages.split('\0'):
            self.database.save_message(msg)


    def stop(self):
        self.client.loop_stop()


class DBwrapper():
    def __init__(self):
        self.connection = sqlite3.connect(DB_NAME, check_same_thread=False)
        self.cursor = self.connection.cursor()
        for table in TABLES:
            self.create_db_tables(table, TABLES[table])

    def create_db_tables(self, table, fields):
        self.cursor.execute(f"""CREATE TABLE IF NOT EXISTS {table}({fields}); """)

    def save_access_log(self, bootcount, timestamp, doorID,
                        readerID, authOK, cardID):

        try:
            self.cursor.execute("INSERT INTO access VALUES(?,?,?,?,?,?)",
                                (bootcount, timestamp, doorID,
                                readerID, authOK, cardID))
        except:
            print("Unable to push to database")

        self.connection.commit()

    def save_system_log(self, bootcount, timestamp, doorID, logtext):

        try:
            self.cursor.execute("INSERT INTO system VALUES(?,?,?,?)",
                                (bootcount, timestamp, doorID, logtext))
        except:
            print("Unable to push to database")

        self.connection.commit()

    def save_message(self, msg):
        if (len(msg) <= 0): return
        msg_fields = msg.split()
        timestamp = msg_fields[0]
        doorID = int(msg_fields[1][1])
        msgtype = msg_fields[2][1:-2]
        is_access = msgtype.find("ACCESS") != -1
        is_boot = msgtype.find("BOOT") != -1

        bootcount = 0
        if is_boot:
            start = msgtype.find('#')
            end = msgtype.find(')', start)
            bootcount = int(msgtype[start+1:end])

        if is_access:
            readerID = 1 if (msg_fields[4][-1] == "external") else 2
            cardID = int(msg_fields[6])
            authOK = 1 if (msg_fields[7] == "authorized") else 0

            self.save_access_log(bootcount, timestamp, doorID,
                                 readerID, authOK, cardID)
        else:
            # All segments after doorID are the text message
            # and, therefore, go in a single DB column:
            logtext = " ".join(msg_fields[3:])
            self.save_system_log(bootcount, timestamp, doorID, logtext)

        time.sleep(0.1)


from watchdog.observers import Observer
from watchdog.events import LoggingEventHandler, PatternMatchingEventHandler

class DiskMonitor():
    def __init__(self, cmdHandler, dbUploadHandler, fmUploadHandler):

        self.cmdObserver = Observer()
        self.cmdObserver.schedule(FileMonitor(CMD_PATTERNS, cmdHandler),
                                  CMD_DIR)
        self.cmdObserver.start()

        self.dbObserver = Observer()
        self.dbObserver.schedule(FileMonitor(DB_PATTERNS, dbUploadHandler),
                                 DB_DIR)
        self.dbObserver.start()

        self.firmwareObserver = Observer()
        self.firmwareObserver.schedule(FileMonitor(FM_PATTERNS, fmUploadHandler),
                                  DB_DIR)
        self.firmwareObserver.start()

    def stop(self):
        self.cmdObserver.stop()
        self.dbObserver.stop()
        self.firmwareObserver.stop()
        self.cmdObserver.join()
        self.dbObserver.join()
        self.firmwareObserver.join()


# TODO: prevent "repeated" events when a file is created and modified.
#       Maybe all we need to do is on_modified and ignore on_created?
class FileMonitor(PatternMatchingEventHandler):
    def __init__(self, patterns, handler):

        self.handler = handler
        # Set the patterns for PatternMatchingEventHandler
        PatternMatchingEventHandler.__init__(self, patterns=patterns,
                                                   ignore_directories=True,
                                                   case_sensitive=False)

    def on_created(self, event):
        self.handler(event.src_path)


    def on_modified(self, event):
        self.handler(event.src_path)


class Main():
    def __init__(self):

        # How to make diskMonitor call a specific method of this object?
        #
        # 1. Pass as parameters both the object and the function, then
        #    call function(obj, params). Since the first parameter of
        #    the function is "self", that probably works, but is weird.
        #
        # 2. Make the object global and define functions that call the
        #    adequate method of the global object. That should work, but
        #    involves a global, which may make testing harder.
        #
        # 3. Use closures, which is what we do here: cmdHandler and
        #    dbUploadHandler have a reference to the object as "theMain".

        theMain = self

        def cmdHandler(filename):
            theMain.sendCommand(filename)

        def dbUploadHandler(filename):
            theMain.sendDB(filename)

        def fmUploadHandler(filename):
            theMain.sendFM(filename)

        self.mqtt = OurMQTT()
        self.diskMonitor = DiskMonitor(cmdHandler, dbUploadHandler, fmUploadHandler)
    
    def sendDB(self, filename):
        self.mqtt.publish("database", filename)

    def sendCommand(self, filename):
        self.mqtt.publish("commands", filename)

    def sendFM(self, filename):
        self.mqtt.publish("firmware", filename)

    def stop(self):
        self.diskMonitor.stop()
        self.mqtt.stop()


def main():
    mainObject = Main()
    try:
        while True:
            time.sleep(SLEEP_TIME)
    finally:
        mainObject.stop()


if __name__ == "__main__":
    main()
