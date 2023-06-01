def get_bootcount(msg):
            start = msg.find('#')
            if start == -1:
                return "NULL"
            end = msg.find(')', start)
            return msg[start+1:end]
        
def push_data(table, msg_data):
        print("""INSERT INTO {} VALUES({});""".format(table, msg_data))
        

def on_message(msg):
    rcv_msg = msg
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
        system_msg = " ".join(splitted_msg[3:])
        msg_data = f'{msg_data}, "{system_msg}"'

    push_data(table, msg_data)

def main():

    string1 = "TIME (SYSTEM/BOOT#BOOTCOUNT): DOOR-ID MESSAGE MESSAGE MESSAGE"
    string2 = "TIME (SYSTEM): DOOR-ID MESSAGE MESSAGE MESSAGE"
    string3 = "TIME (ACCESS/BOOT#BOOTCOUNT): DOOR-ID READER-ID AUTHORIZED CARD-ID"
    string4 = "TIME (ACCESS): DOOR-ID READER-ID AUTHORIZED CARD-ID"
    
    on_message(string1)
    on_message(string2)
    on_message(string3)
    on_message(string4)

if __name__ == "__main__":
    main()