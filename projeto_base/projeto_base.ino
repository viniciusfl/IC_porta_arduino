/*


  Arquivos:
    bancoA
    bancoB
    bancoA.timestamp
    bancoB.timestamp
*/
#include <SPI.h>
#include <Ethernet.h>
#include <SD.h>
#include "RTClib.h"
#include <EthernetUdp.h>
#include <TimeLib.h>

// ---------------------- UDP ---------------------
unsigned int localPort = 8888;       // local port to listen for UDP packets

const char timeServer[] = "a.ntp.br"; // time.nist.gov NTP server

const int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message

byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets

EthernetUDP Udp; // A UDP instance to let us send and receive packets over UDP

// --------------------- relógio ---------------------
RTC_DS1307 rtc;

char daysOfTheWeek[7][12] = {"Domingo", "Segunda", "Terça", "Quarta", "Quinta", "Sexta", "Sábado"};

unsigned long milisPrevia = 0;

unsigned long intervalo = 30000;

// ---------------------- ethernet client ---------------------
byte mac[] = {
  0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };

IPAddress server(10, 0, 2, 113);   

bool jaConectado = false; // whether or not the client was connected previously

EthernetClient client;

#define BUFFERSIZE 40

void setup() {
  delay(200);
  Serial.begin(9600);
   while (!Serial) {
    ; 
  }
  
  if (Ethernet.begin(mac) == 0) {  // inicia conexão ethernet
    Serial.println("Failed to configure Ethernet using DHCP");
    // Check for Ethernet hardware present
    if (Ethernet.hardwareStatus() == EthernetNoHardware) {
      Serial.println("Ethernet shield was not found.  Sorry, can't run without hardware. :(");
    } else if (Ethernet.linkStatus() == LinkOFF) {
      Serial.println("Ethernet cable is not connected.");
    }
    // no point in carrying on, so do nothing forevermore:
    while (true) {
      delay(1);
    }
  }


  // inicia o udp
  Serial.println("udp tentando..");
  Udp.begin(localPort);
  Serial.println("udp feito");
  
  // inicializa relógio
  if (! rtc.begin()) { 
    Serial.println(F("Couldn't find RTC"));
    Serial.flush();
    while (1) delay(10);
  }else{
    Serial.println(F("RTC conectado")); 
  }
  

  if (! rtc.isrunning()) { // se o relógio não estiver funcionando, atualiza o horário uma vez
    Serial.println(F("RTC is NOT running, let's set the time!"));
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  // inicia leitor de cartão SD
  if (!SD.begin(4)) {
    Serial.println(F("sd falhou"));
    while (1);
  }else{
    Serial.println(F("sd conectado"));
  }
  
}

void loop() {
  DateTime now = rtc.now();
      
  unsigned long milisAtual = millis(); // milisegundos desde que o arduino ligou (obs: ele pode guardar "acho" que uns 40 dias!!)
  
  if (milisAtual - milisPrevia > 10000) { // se já se passaram 30 segundos eu tento atualizar o banco de dados
    pegaCliente();
    milisPrevia = milisAtual;
  }  
}



void pegaCliente(){
  // remover strings dps, fazer com char!!
if (client.connect(server, 80)) {
    Serial.println("conectado servidor.");
  }else{
    Serial.println("conexão servidor failed");  
  }
  // Make a HTTP request:
  //client.println("GET /localhost/index/arduino.xml HTTP/1.0");
  client.println("GET /arduino.txt HTTP/1.1");
  client.println("Host: 10.0.2.113");
  //client.println("Connection: close");
  client.println();


  //leEscreveCliente(now);
  /*while(client.connected()){
    while(client.available()){
      char c = client.read();
      //theFile.print(c);
      Serial.print(c);
    }
  }*/


  // verifica qual bd é o mais antigo
  File arq;


  // começa a ler o conteúdo do cliente
  //char buffer[BUFFERSIZE+1]; 
  bool lastreadeol = false;
  bool headerHTTP = false;
  int counter = 0;
  String palavra = "";
  //arq = SD.open(String("bancoA.txt."), FILE_WRITE);
  int i = 0;
  char c;
  
  while(client.connected()){
    while (client.available()){
      c = client.read();
      palavra = palavra + String(c);
      if (String(c) == "\n") {
        if(headerHTTP){
          Serial.print(palavra);
        }
        if(!headerHTTP && palavra == "\r\n"){
          headerHTTP = true;
        }
        palavra = "";
      }
    }
  }
  //buffer[i] = '\0';
  //String resultado = String(buffer);
  //Serial.println(resultado);

  //Serial.println();
  
  Serial.println("disconecting.");
  client.stop();
}

  /*if (milisAtual - milisPrevia > 15000) { 
    milisPrevia = milisAtual;
    atualizaRTC(now);
    }
    */
  

void leEscreveCliente(DateTime now){
  /* Funçaõ que lê o conteúdo que o cliente mandou e escreve no banco de dados mais velho e atualiza o respectivo timestamp*/

  
  /*if (arq) {
    Serial.println(F("Estou escrevendo coisas no cartão..."));
    arq.print(resultado);
    arq.close();
  }*/


}
