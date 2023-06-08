from client import *
import multiprocessing as mp

def main():
    client2 = Main()
    client2.mqtt.subscribe("sendLogs")
    client2.mqtt.subscribe("database")
    client2.mqtt.subscribe("commands")
    client1 = Main()
    client1.mqtt.publish("sendLogs", "files/ACCESSES.txt")
    client1.mqtt.publish("sendLogs", "files/SYSTEM_BOOT.txt")
    client1.mqtt.publish("sendLogs", "files/SYSTEM.txt")
    client1.mqtt.publish("sendLogs", "files/ACCESSES_BOOT.txt")

    create_command()
    update_database()
    time.sleep(10)
    client1.stop()
    client2.stop()

def create_command():
    print("Created new Command")
    f = open(f"test.txt", mode="w")
    f.write("NOVO COMANDO")
    f.close()

def update_database():
    print("Updated database")
    f = open(f"files/databaseex.txt", mode="w")
    f.write("13|AAAAA|BBBBBBB|C|")
    f.close()

if __name__ == "__main__":
    main()