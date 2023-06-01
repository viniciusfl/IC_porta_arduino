#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#----------------------------------------------------------------------------
# Created Date: 28/05/2023
# ---------------------------------------------------------------------------
"""This code handles the creation of a MQTT client that deals up to three things:

1) Observe the users.db and send it to the nodeMCU whenever the file is changed;

2) Observe the commands directory, when a new file is created he reads the data,
   send it to nodeMCU and delete the file;

3) Subscribe for receiving log messages from the other clients, when it gets a
   new message it pushes it to the sqlite messages.db according to its topic,
   (acesses or systems)"""
# ---------------------------------------------------------------------------

from watchdog.observers import Observer
from watchdog.events import LoggingEventHandler, PatternMatchingEventHandler
from paho.mqtt import client as mqtt_client
from multiprocessing import Process
import ssl, sys, time, logging, sqlite3, inspect, os, random, re


SLEEP_TIME = 1
DB_NAME = "messages.db"
FILE_PATTERNS = ["./commands/*.txt", "*/acess.db"]
OBSERVED_DIR = ".."
TABLES = {
    "accesses": "bootcount INT, \
                time VARCHAR(40),\
                door INT,\
                reader INT,\
                authorization INT,\
                card INT,\
                UNIQUE (time, door, bootcount, reader, card, authorization)",

    "systems" : "bootcount INT, \
                time VARCHAR(40),\
                door INT,\
                message VARCHAR(1024),\
                UNIQUE (time, bootcount, door, message)"
}


class Client():
    def __init__(self):
            self.data_base = self.create_data_base()
            self.event_handler = Handler(self.publish)
            self.observer = Observer()

    def start_observer(self):
        self.observer.schedule(self.event_handler, OBSERVED_DIR, recursive=True)
        self.observer.start()

        try:
            while True:
                time.sleep(SLEEP_TIME)
        finally:
            self.observer.stop()
            self.observer.join()

    def create_data_base(self):
        data_base = DataBase()
        for table in TABLES:
            data_base.create_db_tables(table, TABLES[table])
        return data_base

    def publish(self, topic, *file_name):
        file_name = format("".join(file_name))
        if topic == "db":
            file = open(file_name, mode="rb")
            result = self.client.publish("/topic/database", file.read(), retain=True, qos=2)
        elif topic == "commands":
            file = open(file_name, mode="rb")
            result = self.client.publish("/topic/commands", file.read())

        if result[0] == 0:
            self.message = "Message sent to ({})".format(topic)
        else:
            self.message = "Failed to send message to ({})".format(topic)

    def subscribe(self):
        def get_bootcount(msg):
            start = msg.find('#')
            if start == -1:
                return "NULL"
            end = msg.find(')', start)
            return msg[start+1:end]

        def on_message(client, userdata, msg):
            self.message = f"Received msg from `{msg.topic}` topic"
            rcv_msg = str(msg.payload.decode("utf-8"))
            splitted_msg = rcv_msg.split()
            
            is_access = rcv_msg.find("ACCESS") != -1 
            msg_data = f'"{get_bootcount(splitted_msg[1])}"'

            if is_access:
                table = "access"
                for i in range(0, len(splitted_msg)):
                    if i == 1: continue
                    msg_data = f'{msg_data}, "{splitted_msg[i]}"'
            else:
                table = "systems"
                for i in range(0, 3):
                    if i == 1: continue
                    msg_data = f'{msg_data}, "{splitted_msg[i]}"'
                system_msg = "".join(splitted_msg[3:])
                msg_data = f'{msg_data}, "{system_msg}"'

            self.data_base.push_data(msg.topic, msg_data)
            self.contador = self.contador + 1
                
            
        self.client.subscribe("/topic/sendLogs", 1)
        self.client.on_message = on_message

class DataBase():
    def __init__(self):
        self.connection = sqlite3.connect(DB_NAME)
        self.cursor = self.connection.cursor()

    def create_db_tables(self, table, fields):
        self.cursor.execute("""CREATE TABLE IF NOT EXISTS {}({}); """.format(table, fields))

    def push_data(self, table, msg_data):
        try:
            self.cursor.execute("""INSERT INTO {} VALUES({});""".format(table, msg_data))
        except:
            return
        self.connection.commit()

class Handler(PatternMatchingEventHandler):
    def __init__(self, client_publish):
        self.client_publish = client_publish
        # Set the patterns for PatternMatchingEventHandler
        PatternMatchingEventHandler.__init__(self, patterns=FILE_PATTERNS,
                                                             ignore_directories=True, case_sensitive=False)
    def on_created(self, event):
        #handle files created on command directory
        q = Process(target=self.client_publish, args=("commands", event.src_path))
        q.start()
        os.remove("".join(event.src_path))


    def on_modified(self, event):
        #handle acess.db changes
        p = Process(target=self.client_publish, args=("db", event.src_path))
        p.start()


def main():
    client = Client()
    client.start_observer()

if __name__ == "__main__":
    main()
