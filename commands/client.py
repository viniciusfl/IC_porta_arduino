#!/usr/bin/env python3  
# -*- coding: utf-8 -*- 
#----------------------------------------------------------------------------   
# Created Date: 28/05/2023
# ---------------------------------------------------------------------------
"""This code handles the creation of a MQTT client that deals up to three things:

1) Observe the users.db and send it to the nodeMCU whenever the file is changed;

2) Observe the commands directory, when a new file is created he reads the data,
   send it to nodeMCU and delete the file;

3) Subscribe for receiveng log messages from the other brokers, when he gets a 
   new message he push it to the sqlite messages.db according to its topic, 
   (acesses or systems)"""  
# ---------------------------------------------------------------------------

from watchdog.observers import Observer
from watchdog.events import LoggingEventHandler, PatternMatchingEventHandler
from paho.mqtt import client as mqtt_client
from multiprocessing import Process
import ssl, sys, time, logging, sqlite3, inspect, os, random


SLEEP_TIME = 1
DB_NAME = "messages.db"
FILE_PATTERNS = ["./commands/*.txt", "*/acess.db"]
OBSERVED_DIR = ".."
TABLES = {
    "accesses": "topic VARCHAR(10),\
                time VARCHAR(40),\
                door INT,\
                reader INT,\
                card INT,\
                authorization INT,\
                message VARCHAR(255),\
                PRIMARY KEY(time, door, reader, card)",

    "systems" :  "topic VARCHAR(10),\
                time VARCHAR(40),\
                door INT,\
                message VARCHAR(255),\
                PRIMARY KEY (time, door)"
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
            result = self.client.publish("/topic/database", file.read(), retain=True)
            if result[0] == 0:
                self.message = "Send db to topic /topic/database"
            else:
                self.message = "Failed to send message to topic /topic/database"
        elif topic == "commands":
            file = open(file_name, mode="rb")
            result = self.client.publish("/topic/commands", file.read())
  
    def subscribe(self):
        def on_message(client, userdata, msg):
            self.message = f"Received msg from `{msg.topic}` topic"     
            for info in (msg.payload.decode().split()):
                msg_data = f'{msg_data}, "{info}"'
            msg_data = f'"{msg.topic}" "{msg_data}", "{self.message}"'
            self.insert_data(msg.topic, msg_data)
            self.contador = self.contador + 1

        self.client.subscribe("/topic/sendLogs", 1)
        self.client.on_message = on_message
        
class DataBase():
    def __init__(self):
        self.connection = sqlite3.connect(DB_NAME)
        self.cursor = self.connection.cursor()

    def create_db_tables(self, table, fields):
        self.cursor.execute("""CREATE TABLE IF NOT EXISTS {}({}); """.format(table, fields))        

    def push_data(self, topic, msg_data):
        try:
            if(topic == "SYSTEM"):
                table = "systems"  
            else: 
                table = "accesses"
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