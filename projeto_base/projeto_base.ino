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
/*
unsigned int localPort = 8888;       // local port to listen for UDP packets

const char timeServer[] = "a.ntp.br"; // time.nist.gov NTP server

const int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message

byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets

EthernetUDP Udp; // A UDP instance to let us send and receive packets over UDP
*/

// --------------------- relógio ---------------------
RTC_DS1307 rtc;

unsigned long milisPrevia = 0;

/*
char daysOfTheWeek[7][12] = {"Domingo", "Segunda", "Terça", "Quarta", "Quinta", "Sexta", "Sábado"};



unsigned long intervalo = 30000; */

// ---------------------- ethernet client ---------------------
byte mac[] = {
  0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };

IPAddress server(10, 0, 2, 113);   

EthernetClient client;

#define BUFFERSIZE 40

File arquivo;

void setup() {
  bool passou = 1;
  delay(150);
  Serial.begin(9600);
   while (!Serial) {
    ; 
  }
  
  Serial.println(Ethernet.begin(mac));


  // inicia o udp
 // Udp.begin(localPort);

  
  // inicializa relógio
  Serial.println(rtc.begin());

  if (! rtc.isrunning()) { // se o relógio não estiver funcionando, atualiza o horário uma vez
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  // inicia leitor de cartão SD
  Serial.println(SD.begin(4));

  
  
  if(!SD.exists("A.txt")){
    Serial.println("Estou criando A.txt");
    arquivo = SD.open("A.txt", FILE_WRITE);
    arquivo.write("0\n");
    arquivo.close();
  }
  Serial.println(passou);
}

void loop() {
  DateTime now = rtc.now();
      
  unsigned long milisAtual = millis(); // milisegundos desde que o arduino ligou (obs: ele pode guardar "acho" que uns 40 dias!!)
  if (milisAtual - milisPrevia > 30000) { // se já se passaram 30 segundos eu tento atualizar o banco de dados
    Serial.println(F("entrei"));
    //printaData(now);
    leCliente(now);
    milisPrevia = milisAtual;
    //atualizaRTC(now);
  }  
}


void leCliente(DateTime now){
  /* função que recebe a resposta do http request, ignora o response header e printa o conteúdo relevante
   *  OBS: fazer com char depois pois é mais rápido e flexível.
   */
   
if (client.connect(server, 80)) {
    Serial.println("conectado servidor.");
  }else{
    Serial.println("conexão servidor failed");  
  }
  // HTTP request:
  //client.println("GET /localhost/index/arduino.xml HTTP/1.0");
  client.println("GET /arduino.txt HTTP/1.1");
  client.println("Host: 10.0.2.113");
  //client.println("Connection: close");
  client.println();


  Serial.println("aaA");
  arquivo = SD.open("A.txt");
  long tempoA = arquivo.parseInt();
  arquivo.close();

  /*Serial.println("bbbA");
  arquivo = SD.open("B.txt");
  
  arquivo.close();
  */
  long tempoB = 100000000000000000;
  Serial.print("A = ");
  Serial.println(tempoA);
  //Serial.print("B = ");
  //Serial.println(tempoB);
  
  
  long unixTime = now.unixtime();
  Serial.println(unixTime);
  if(tempoA <= tempoB){
    Serial.println("printando em A");
    SD.remove("bancoA.txt");  
    SD.remove("A.txt");  
    arquivo = SD.open("A.txt", FILE_WRITE);
    arquivo.println(unixTime);
    arquivo.close();
    arquivo = SD.open("bancoA.txt", FILE_WRITE);
  }else{
    Serial.println("printando em B");
    SD.remove("bancoB.txt");  
    SD.remove("B.txt");  
    arquivo = SD.open("B.txt", FILE_WRITE);
    arquivo.println(unixTime);
    arquivo.close();
    arquivo = SD.open("bancoB.txt", FILE_WRITE);
  }


  bool headerHTTP = false;
  String palavra = "";
  //arq = SD.open(String("bancoA.txt."), FILE_WRITE);
  char c;
  
  // começa a ler o conteúdo do cliente
  while(client.connected()){
    while (client.available()){
      c = client.read();
      //Serial.print(c);
      palavra = palavra + String(c);
      if (String(c) == "\n") {
        if(headerHTTP){
          arquivo.println(palavra);
          Serial.print(palavra);
        }
        
        if(!headerHTTP && palavra == "\r\n"){
          headerHTTP = true;
          arquivo.println("");
          Serial.println();
        }
        palavra = "";
      }
    }
  }
  arquivo.close();
  Serial.println("desconectando.");
  client.stop();
}
