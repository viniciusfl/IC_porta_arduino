#include <SPI.h>
#include <Ethernet.h>

byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED};

// Nao estamos usando, vai com DHCP
byte ip[] = {192, 168, 48, 129};
byte mask[] = {255, 255, 192, 0};
byte dns[] = {192, 168, 45, 11};
byte gw[] = {192, 168, 45, 1};


void printStatus() {
  if (Ethernet.hardwareStatus() == EthernetNoHardware) {
    Serial.println("Ethernet shield was not found.");
    return;
  }
  else if (Ethernet.hardwareStatus() == EthernetW5100) {
    Serial.println("W5100 Ethernet controller detected.");
  }
  else if (Ethernet.hardwareStatus() == EthernetW5200) {
    Serial.println("W5200 Ethernet controller detected.");
  }
  else if (Ethernet.hardwareStatus() == EthernetW5500) {
    Serial.println("W5500 Ethernet controller detected.");
  }

  byte macBuffer[6];  // create a buffer to hold the MAC address
  Ethernet.MACAddress(macBuffer); // fill the buffer
  Serial.print("The MAC address is: ");
  for (byte octet = 0; octet < 6; octet++) {
    Serial.print(macBuffer[octet], HEX);
    if (octet < 5) {
      Serial.print('-');
    }
  }
  Serial.println("");
  Serial.print("The IP address is: ");
  Serial.println(Ethernet.localIP());
  Serial.print("The DNS server is: ");
  Serial.println(Ethernet.dnsServerIP());
}

void setup() {
  Serial.begin(9600);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }

  Serial.println("Initializing network...");
  //Ethernet.setMACAddress(mac);
  //Ethernet.begin(mac, ip, dns, gw, mask);
  Serial.println(Ethernet.begin(mac));
  printStatus();
}


int i = 0;
void loop () {
  delay(2000);
  printStatus();
  
  i += 1;
  if (i >= 3) {
    Ethernet.maintain();
    i = 0;
  }
  
}
