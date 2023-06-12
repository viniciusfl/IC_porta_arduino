from client import *
import multiprocessing as mp

def main():
    client2 = Main()
    client2.mqtt.subscribe("sendLogs")
    client2.mqtt.subscribe("database")
    client2.mqtt.subscribe("commands")
    time.sleep(0.1)
    client1 = Main()
    client1.mqtt.publish("sendLogs", "upload/testLogs.txt")
    create_command()
    update_database()
    time.sleep(10)
    client1.stop()
    client2.stop()

def create_command():
    print("\nCreated new Command")
    f = open(f"test.txt", mode="w")
    f.write("NOVO COMANDO")
    f.close()

def update_database():
    print("\nUpdated database")
    f = open(f"upload/databaseex.db", mode="w")
    f.write("01|NEW|UPLOAD|TO|DATABASE|")
    f.close()
    time.sleep(1)
    os.remove("upload/databaseex.db")

if __name__ == "__main__":
    main()