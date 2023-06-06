from client import *
import multiprocessing as mp

def main():
    client2 = Client()
    client2.subscribe("sendLogs")
    client2.subscribe("database")
    client2.subscribe("commands")
    client1 = Client()
    client1.publish("log", "files/ACCESSES.txt")
    client1.publish("log", "files/SYSTEM_BOOT.txt")
    client1.publish("log", "files/SYSTEM.txt")
    client1.publish("log", "files/ACCESSES_BOOT.txt")
    first = mp.Process(target=client1.start_observer)
    first.start()
    time.sleep(1)

    processes = []
    processes.append(mp.Process(target=create_command))
    processes.append(mp.Process(target=update_database))
    for p in processes:
        p.start()
        time.sleep(1)
        p.join()
        

def create_command():
    print("Created new Command")
    f = open(f"test.txt", mode="w")
    f.write("NOVO COMANDO")
    f.close()

def update_database():
    print("Updated database")
    f = open(f"files/databaseex.txt", mode="w")
    f.write("12|AAAAA|BBBBBBB|C|")
    f.close()

if __name__ == "__main__":
    main()