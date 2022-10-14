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

File arquivo;

bool lendoDoCliente = false;

bool headerHTTP = false;

bool banco = 0; // 0 se o banco A for o mais antigo, 1 se o banco B for o mais antigo.

void setup() {
  delay(150);
  Serial.begin(9600);
   while (!Serial) {
    ; 
  }
  
  // inicializa internet
  Serial.println(Ethernet.begin(mac));
  
  // inicializa relógio
  Serial.println(rtc.begin());

  if (! rtc.isrunning()) { // se o relógio não estiver funcionando, atualiza o horário uma vez
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  // inicia leitor de cartão SD
  Serial.println(SD.begin(4));

  // verifico quem é o bd mais novo
  arquivo = SD.open("A.txt");
  long tempoA = arquivo.parseInt();
  arquivo.close();

  arquivo = SD.open("B.txt");
  long tempoB = arquivo.parseInt();
  arquivo.close();

  if(tempoA < tempoB){
    banco = 1;
  }else{
    banco = 0;
  }
}

void loop() {
  DateTime now = rtc.now();
  unsigned long milisAtual = millis(); // milisegundos desde que o arduino ligou (obs: ele pode guardar "acho" que uns 40 dias!!)

  /*
  if(Serial.available() > 0){ // se alguém mandou algo pelo serial
    int num = toInt(Serial.readString());
    verificaIdNoBanco(num);
  }*/
  
  if(lendoDoCliente){ // se ainda não terminei de ler o conteúdo que o cliente mandou
    Serial.println(F("estou  lendo mais uma linha do cliente..."));
    lendoDoCliente = leCliente();
    if(!lendoDoCliente){ // serve para debuggar
      headerHTTP = false; // reseta a variável que diz se passamos do header ou não
      if(banco){ 
        banco = 0;
      }else{
        banco = 1;
      }
      Serial.println(F("parei de me comunicar com o cliente. devo ter lido tudo já"));
    }
  }else if (milisAtual - milisPrevia > 15000) { // se já se passaram 15 segundos eu tento atualizar o banco de dados
    chamaCliente(now);
    Serial.println(F("vou começar a ler do cliente..."));
    lendoDoCliente = true;
    milisPrevia = milisAtual;
  }  
}


void chamaCliente(DateTime now){
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

  /*
   * Já que no setup eu vejo quem é o banco mais novo, então eu não preciso procurar de novo. Basta atualizar o tempo do mais antigo
   */
  long unixTime = now.unixtime();
  if(banco){ // se o banco B é o mais novo eu vou excluir o banco A e o datetime de A, e atualizar o datetime de A
    Serial.println("banco A é o mais antigo... apagando");
    SD.remove("A.txt");
    SD.remove("bancoA.txt");  
    arquivo = SD.open("A.txt", FILE_WRITE);
    arquivo.println(unixTime);
    arquivo.close();
  }else{ // se o banco A é o mais novo, ...
    Serial.println("banco B é o mais antigo... apagando");
    SD.remove("B.txt");
    SD.remove("bancoB.txt");  
    arquivo = SD.open("B.txt", FILE_WRITE);
    arquivo.println(unixTime);
    arquivo.close();
  }
}

bool leCliente(){
  /* função que recebe a resposta do http request, ignora o response header, lê uma linha que o cliente mandou e escreve ela no arquivo
   *  OBS: fazer com char depois pois é mais rápido e flexível.
   */
   /*
  if(banco){
    arquivo = SD.open("bancoA.txt", FILE_WRITE);
  }else{
    arquivo = SD.open("bancoB.txt", FILE_WRITE);
  }*/
  
  bool liPalavra = false;
  String palavra = "";
  char c;
  // começa a ler o conteúdo do cliente
  while(!liPalavra && client.connected() && client.available()){
    c = client.read();
    palavra = palavra + String(c);
    if (String(c) == "\n") {
      if(headerHTTP){
        arquivo.println(palavra);
        Serial.println(palavra);
      }
      if(!headerHTTP && palavra == "\r\n"){
        headerHTTP = true;
        arquivo.println("");
        Serial.println("header...");
      }
      palavra = "";
      liPalavra = true;
    }
    
  }
  Serial.println();
  arquivo.close();
  if(client.connected() && client.available()){ // se o cliente está conectado e mandando coisas ainda mesmo depois de lermos uma linha
    return true; 
  }else{ // lemos tudo, podemos desconectar do cliente. 
    Serial.println("lemos tudo do cliente...");
    client.stop();
    return false;
  }
}
