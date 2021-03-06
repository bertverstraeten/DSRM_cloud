/*
***************************************************************************  
**  Program  : DSMRlogger2HTTP
*/
/*
**  Copyright (c) 2019 Willem Aandewiel
**
**  TERMS OF USE: MIT License. See bottom of file.                                                            
***************************************************************************      
  Arduino-IDE settings for ESP01 (black):

    - Board: "Generic ESP8266 Module"
    - Flash mode: "DOUT"
    - Flash size: "1M (128K SPIFFS)"
    - Debug port: "Disabled"
    - Debug Level: "None"
    - IwIP Variant: "v2 Lower Memory"
    - Reset Method: "nodemcu"   // but will depend on the programmer! 
    - Crystal Frequency: "26 MHz" 
    - VTables: "Flash"
    - Flash Frequency: "40MHz"
    - CPU Frequency: "80 MHz"
    - Buildin Led: "1"  // GPIO01 !! for ESP-01S USE "2"! Also the "S" has no red led
    - Upload Speed: "115200"
    - Erase Flash: "Only Sketch"
    - Port: "ESP01-DSMR at <-- IP address -->"
*/

//  part of ESP8266 Core https://github.com/esp8266/Arduino
#include <ESP8266WiFi.h>        // version 1.0.0

//  part of ESP8266 Core https://github.com/esp8266/Arduino
#include <ESP8266WebServer.h>   // Version 1.0.0

//  https://github.com/tzapu/WiFiManager
#include <WiFiManager.h>        // version 0.14.0

//  part of ESP8266 Core https://github.com/esp8266/Arduino
#include <ArduinoOTA.h>         // Version 1.0.0

//  https://github.com/jandrassy/TelnetStream
#include <TelnetStream.h>       // Version 0.0.1

//  part of ESP8266 Core https://github.com/esp8266/Arduino
#include <FS.h>

//  https://github.com/PaulStoffregen/Time
#include <TimeLib.h>

#include <SoftwareSerial.h>

#include "CRC16.h"

#define HOSTNAME     "SmartReader"  // 
    
#define LED_ON            LOW
#define LED_OFF           HIGH
void configModeCallback(WiFiManager *myWiFiManager);

static FSInfo SPIFFSinfo;

WiFiClient        wifiClient;
ESP8266WebServer  server ( 80 );

String    MAC = ""; //init macaddress
uint32_t  waitLoop, noMeterWait, telegramCount, telegramErrors, waitForATOupdate;
char      cMsg[100], fChar[10];
char      APname[50], MAChex[13]; //n1n2n3n4n5n6\0

String    lastReset = ""; 
bool      debug = false, OTAinProgress = false, Verbose = false, showRaw = false, SPIFFSmounted = false;
int8_t    tries, showRawCount;
uint32_t  nextSecond, unixTimestamp;
uint64_t  upTimeSeconds;
IPAddress ipDNS, ipGateWay, ipSubnet;
uint16_t  WIFIreStartCount;
String    jsonString;
int       iterationsGet = 0; //returns number of server calls
const bool outputOnSerial = true;

#define MAXLINELENGTH 1024 // longest normal line is 47 char (+3 for \r\n\0)
char telegram[MAXLINELENGTH];

#define SERIAL_RX  14 // pin for SoftwareSerial RX
SoftwareSerial mySerial(SERIAL_RX, -1, false, MAXLINELENGTH); // (RX, TX. inverted, buffer)

unsigned int currentCRC=0;

// Vars to store meter readings
double mEVLT = 0; //Meter reading Electrics - consumption low tariff
double mEVHT = 0; //Meter reading Electrics - consumption high tariff
double mEOLT = 0; //Meter reading Electrics - return low tariff
double mEOHT = 0; //Meter reading Electrics - return high tariff
double mEAV = 0;  //Meter reading Electrics - Actual consumption
double mEAT = 0;  //Meter reading Electrics - Actual return
double mEVLT_temp = 0; //Meter reading Electrics - consumption low tariff
double mEVHT_temp = 0; //Meter reading Electrics - consumption high tariff
double mEOLT_temp = 0; //Meter reading Electrics - return low tariff
double mEOHT_temp = 0; //Meter reading Electrics - return high tariff
double mEAV_temp = 0;  //Meter reading Electrics - Actual consumption
double mEAT_temp = 0;  //Meter reading Electrics - Actual return

// var for timing
int send_wait_time = 10000; //wait time in between logs in millis
int previous_millis = 0;


//===========================================================================================
void setup() {
//===========================================================================================
  Serial.begin(115200);
  mySerial.begin(115200);
  pinMode(BUILTIN_LED, OUTPUT);
  for(int I=0; I<5; I++) {
    digitalWrite(BUILTIN_LED, !digitalRead(BUILTIN_LED));
    delay(2000);
  }
  digitalWrite(BUILTIN_LED, LED_OFF);  // HIGH is OFF
  lastReset     = ESP.getResetReason();
  
  if (debug) Serial.println("\nBooting....\n");

//================ SPIFFS =========================================
  if (!SPIFFS.begin()) {
    if (debug) Serial.println("SPIFFS Mount failed");   // Serious problem with SPIFFS 
    SPIFFSmounted = false;
    
  } else { 
    if (debug) Serial.println("SPIFFS Mount succesfull");
    SPIFFSmounted = true;
    sprintf(cMsg, "Last reset reason: [%s]", ESP.getResetReason().c_str());
    if (debug) {
      Serial.println(cMsg);
      Serial.flush();
    }
  }
//=============end SPIFFS =========================================

  digitalWrite(BUILTIN_LED, LED_ON);
  setupWiFi(false);
  digitalWrite(BUILTIN_LED, LED_OFF);

  if (debug) {
    Serial.println ( "" );
    Serial.print ( "Connected to " ); Serial.println (WiFi.SSID());
    Serial.print ( "IP address: " );  Serial.println (WiFi.localIP());
  }
  for (int L=0; L < 10; L++) {
    digitalWrite(BUILTIN_LED, !digitalRead(BUILTIN_LED));
    delay(200);
  }
  digitalWrite(BUILTIN_LED, LED_OFF);

  // retrieve macaddress
  MAC = WiFi.macAddress();
  Serial.print("MAC = ");
  Serial.println(MAC);

  server.on("/getActual.json", sendDataActual);
  server.on("/ReBoot", HTTP_POST, handleReBoot);
  
  server.serveStatic("/", SPIFFS, "/index.html");
  server.serveStatic("/index.js", SPIFFS, "/index.js");
  
  server.onNotFound([]() {
    if (!handleFileRead(server.uri()))
      server.send(404, "text/plain", "FileNotFound");
  });

  server.begin();
  if (debug) Serial.println( "HTTP server started" );
  Serial.flush();

  if (debug) Serial.println("\nSetup done.."); 
} // setup()

void sendDataActual() {
//=======================================================================
  jsonString  = "{";
  jsonString += " \"Iteration\":\"" + String(iterationsGet) + "\"";
  jsonString += ",\"Verbruik 1\":\"" + String(mEVLT,3) + "\"";
  jsonString += ",\"Verbruik 2\":\"" + String(mEVHT, 3) + "\"";
  jsonString += ",\"Opbrengst 1\":\"" + String(mEOLT, 3) + "\"";
  jsonString += ",\"Opbrengst 2\":\"" + String(mEOHT, 3) + "\"";
  jsonString += ",\"Actueel verbruik\":\"" + String(mEAV, 3) + "\"";
  jsonString += ",\"Actuele opbrengst\":\"" + String(mEAT, 3) + "\"";
  jsonString += "}";
  server.send(200, "application/json", jsonString);
  LEDblink();
  iterationsGet++;

} // sendDataActual()

void LEDblink() {
  for (int L=0; L < 3; L++) {
    digitalWrite(BUILTIN_LED, !digitalRead(BUILTIN_LED));
    delay(200);
  }
  digitalWrite(BUILTIN_LED, LED_OFF);  
}

//===========================================================================================
void loop () {
//===========================================================================================
  server.handleClient();

  // run p1 reader here
  readTelegram();

  // send data after waiting time has been passed
  if (millis() - previous_millis > send_wait_time){
    Serial.println("Sending data...");
    const int httpGetPort = 80;
    const char* hostGet = "kinekadees.be"; //host server 
    String urlGet = "/monitor/receiver.php"; //the path and file to send the data to
    
    // We now create and add parameters
    urlGet += "?mac_address=" + MAC  + "&current_data=%5B%7BVerbruikH%3D" + String(mEVLT) + "%2CVerbruikL%3D" + String(mEVHT) + "%2COpbrengst1%3D" + String(mEOLT) + "%2COpbrengst2%3D" + String(mEOHT) + "%2CActueelVerbruik%3D" + String(mEAV) + "%2CActueleOpbrengst%3D" + String(mEAT) + "%7D%5D";
 
   Serial.print(">>> Connecting to host: ");
   Serial.println(hostGet);
      
    if (!wifiClient.connect(hostGet, httpGetPort)) {
    Serial.print("Connection failed: ");
    Serial.print(hostGet);
    } else {
        wifiClient.println("GET " + urlGet + " HTTP/1.1");
        wifiClient.print("Host: ");
        wifiClient.println(hostGet);
        wifiClient.println("User-Agent: ESP8266/1.0");
        wifiClient.println("Connection: close\r\n\r\n");
          
        unsigned long timeoutP = millis();
        while (wifiClient.available() == 0) {
            
          if (millis() - timeoutP > 10000) {
            Serial.print(">>> Client Timeout: ");
            Serial.println(hostGet);
            wifiClient.stop();
            return;
          } else{
            // blink to indicate data has been sent
            LEDblink();
            Serial.println("URL sent: " + urlGet);
          }
        }
     previous_millis = millis();
     Serial.println("Wait for next log");
  }
  }  
} // loop()


/*
***************************************************************************  
**  Program  : WiFiStuff, part of DSMRlogger2HTTP
**  Version  : v0.7.8
**
**  Copyright (c) 2018 Willem Aandewiel
**
**  TERMS OF USE: MIT License. See bottom of file.                                                            
***************************************************************************      
*/

//===========================================================================================
//gets called when WiFiManager enters configuration mode
void configModeCallback(WiFiManager *myWiFiManager) {
//===========================================================================================
    if (debug) Serial.print("configModeCallback(): Entered config mode @");
    if (debug) Serial.print(WiFi.softAPIP());
    if (debug) Serial.println(" with SSID: ");
    //if you used auto generated SSID, print it
    if (debug) Serial.println("myWiFiManager->getConfigPortalSSID()");
    myWiFiManager->getConfigPortalSSID();

}   // configModeCallback()



//===========================================================================================
void setupWiFi(bool forceAP) {
//===========================================================================================
String  tmpTopic;
int     noConnectWiFi;
char    MAC5hex[5];
byte    mac[6];

    digitalWrite(BUILTIN_LED, LED_ON);  
    WiFi.macAddress(mac);
    sprintf(APname, "%s-%02x-192.168.3.1", HOSTNAME, mac[5]);
    //WiFiManager
    //Local intialization. Once its business is done, there is no need to keep it around
    WiFiManager wifiManager;
    wifiManager.setDebugOutput(true);
    noConnectWiFi = 30;

    if (forceAP) {
        if (debug) Serial.printf("Restart DSMR to start AP [%s] to re-configure\r\n", APname);
        TelnetStream.printf("Restart DSMR to start AP [%s] to re-configure\r\n", APname);
        wifiManager.resetSettings();
        noConnectWiFi   = 0;
    }

    WiFi.hostname(HOSTNAME);

    wifiManager.setTimeout(180);  // timeOut in 180 seconds!

#ifdef ARDUINO_ESP8266_GENERIC
      wifiManager.setAPStaticIPConfig(IPAddress(192,168,3,1), IPAddress(192,168,3,1), IPAddress(255,255,255,0));
#else
      wifiManager.setAPStaticIPConfig(IPAddress(192,168,6,1), IPAddress(192,168,6,1), IPAddress(255,255,255,0));
#endif

    WiFi.mode(WIFI_AP_STA);
    if (forceAP) {
      if (!wifiManager.startConfigPortal(APname)) {
        Serial.println("startConfigPortal(): Failed to connect and hit timeout");
        TelnetStream.println("startConfigPortal(): Failed to connect and hit timeout");
        TelnetStream.flush();
        delay(1000);
        //reset and try again, or maybe put it to deep sleep
        ESP.reset();
        delay(1000);
      } 
    } else {
      if (!wifiManager.autoConnect(APname)) {
         delay(1000);
         //reset and try again, or maybe put it to deep sleep
         ESP.reset();
         delay(1000);
      }
    }
    //if you get here you have connected to the WiFi AFTER reconfiguring!
    if (debug) Serial.println("connected to WiFi ... Yes! ;-)");
    Serial.flush();
    TelnetStream.println("connected to WiFi ... Yes! ;-)");
    TelnetStream.flush();

    // WiFi connexion is OK
    if (debug) {
      Serial.println ( "" );
      Serial.print ( "Connected to " ); Serial.println ( WiFi.SSID() );
      Serial.print ( "IP address: " );  Serial.println ( WiFi.localIP() );
    }
    TelnetStream.println ( "" );
    TelnetStream.print ( "Connected to " ); TelnetStream.println ( WiFi.SSID() );
    TelnetStream.print ( "IP address: " );  TelnetStream.println ( WiFi.localIP() );

    if (debug) {
      Serial.print(  "setupWiFi(): Free Heap[B]: ");
      Serial.println( ESP.getFreeHeap() );
      Serial.print(  "setupWiFi(): Connected to SSID: ");
      Serial.println( WiFi.SSID() );
      Serial.print(  "setupWiFi(): IP address: ");
      Serial.println( WiFi.localIP() );
      Serial.flush();
    }
    TelnetStream.print(  "setupWiFi(): Free Heap[B]: ");
    TelnetStream.println( ESP.getFreeHeap() );
    TelnetStream.print(  "setupWiFi(): Connected to SSID: ");
    TelnetStream.println( WiFi.SSID() );
    TelnetStream.print(  "setupWiFi(): IP address: ");
    TelnetStream.println( WiFi.localIP() );
    TelnetStream.flush();

    digitalWrite(BUILTIN_LED, LED_OFF);

}   // setupWiFi()

/*
***************************************************************************  
**  Program  : SPIFFSstuff, part of DSMRlogger2HTTP
**  Version  : v0.7.7
**
**  Copyright (c) 2019 Willem Aandewiel
**
**  TERMS OF USE: MIT License. See bottom of file.                                                            
***************************************************************************      
*/

//===========================================================================================
int32_t freeSpace() {
//===========================================================================================
  int32_t space;
  
  SPIFFS.info(SPIFFSinfo);

  space = (int32_t)(SPIFFSinfo.totalBytes - SPIFFSinfo.usedBytes);

  return space;
  
} // freeSpace()

//===========================================================================================
void listSPIFFS() {
//===========================================================================================
  Dir dir = SPIFFS.openDir("/");

  TelnetStream.println();
  while (dir.next()) {
    File f = dir.openFile("r");
    TelnetStream.printf("%-15s %ld \r\n", dir.fileName().c_str(), f.size());
    yield();
  }
  TelnetStream.flush();

  SPIFFS.info(SPIFFSinfo);

  int32_t space = (int32_t)(SPIFFSinfo.totalBytes - SPIFFSinfo.usedBytes);
  TelnetStream.println("\r");
  TelnetStream.printf("Available SPIFFS space [%ld]bytes\r\n", freeSpace());
  TelnetStream.printf("           SPIFFS Size [%ld]kB\r\n", (SPIFFSinfo.totalBytes / 1024));
  TelnetStream.printf("     SPIFFS block Size [%ld]bytes\r\n", SPIFFSinfo.blockSize);
  TelnetStream.printf("      SPIFFS page Size [%ld]bytes\r\n", SPIFFSinfo.pageSize);
  TelnetStream.printf(" SPIFFS max.Open Files [%ld]\r\n\n", SPIFFSinfo.maxOpenFiles);


} // listSPIFFS()

void UpdateElectricity()
{
  char sValue[255];
  sprintf(sValue, "%d;%d;%d;%d;%d;%d", mEVLT, mEVHT, mEOLT, mEOHT, mEAV, mEAT);
  Serial.println("");
  Serial.print("Verbruik tarfief 1 = ");Serial.print(mEVLT);Serial.print(" ");
  Serial.print("Verbruik tarfief 2 = ");Serial.print(mEVHT);Serial.print(" ");
  Serial.print("Opbrengst tarfief 1 = ");Serial.print(mEOLT);Serial.print(" ");
  Serial.print("Opbrengst tarfief 2 = ");Serial.print(mEOHT);Serial.print(" ");
  Serial.print("Actueel verbruik = ");Serial.print(mEAV);Serial.print(" ");
  Serial.print("Actuele opbrengst = ");Serial.print(mEAT);Serial.print(" ");
}


bool isNumber(char* res, int len) {
  for (int i = 0; i < len; i++) {
    if (((res[i] < '0') || (res[i] > '9'))  && (res[i] != '.' && res[i] != 0)) {
      return false;
    }
  }
  return true;
}

int FindCharInArrayRev(char array[], char c, int len) {
  for (int i = len - 1; i >= 0; i--) {
    if (array[i] == c) {
      return i;
    }
  }
  return -1;
}

long getValidVal(long valNew, long valOld, long maxDiffer)
{
  //check if the incoming value is valid
      if(valOld > 0 && ((valNew - valOld > maxDiffer) && (valOld - valNew > maxDiffer)))
        return valOld;
      return valNew;
}

long getValue(char* buffer, int maxlen) {
  int s = FindCharInArrayRev(buffer, '(', maxlen - 2);
  if (s < 8) return 0;
  if (s > 32) s = 32;
  int l = FindCharInArrayRev(buffer, '*', maxlen - 2) - s - 1;
  if (l < 4) return 0;
  if (l > 12) return 0;
  char res[16];
  memset(res, 0, sizeof(res));

  if (strncpy(res, buffer + s + 1, l)) {
    if (isNumber(res, l)) {
      return (1000 * atof(res));
    }
  }
  return 0;
}

bool decodeTelegram(int len) {
  //need to check for start
  int startChar = FindCharInArrayRev(telegram, '/', len);
  int endChar = FindCharInArrayRev(telegram, '!', len);
  bool validCRCFound = false;
  if(startChar>=0)
  {
    //start found. Reset CRC calculation
    currentCRC = CRC16(0x0000,(unsigned char *) telegram+startChar, len-startChar);
    if(outputOnSerial)
    {
      for(int cnt=startChar; cnt<len-startChar;cnt++)
        ;//Serial.println(telegram[cnt]);
    }    
    Serial.println("Start found!");
    
  }
  else if(endChar>=0)
  {
    //add to crc calc 
    currentCRC=CRC16(currentCRC,(unsigned char*)telegram+endChar, 1);
    char messageCRC[5];
    strncpy(messageCRC, telegram + endChar + 1, 4);
    messageCRC[4]=0; //thanks to HarmOtten (issue 5)
    if(outputOnSerial)
    {
      for(int cnt=0; cnt<len;cnt++)
        ;//Serial.print(telegram[cnt]);
    }    
   validCRCFound = (strtol(messageCRC, NULL, 16) == currentCRC);
    if(validCRCFound)
      ;//Serial.println("\nVALID CRC FOUND!"); 
    else
     ;// Serial.println("\n===INVALID CRC FOUND!===");
    currentCRC = 0;
  }
  else
  {
    currentCRC=CRC16(currentCRC, (unsigned char*)telegram, len);
    if(outputOnSerial)
    {
      for(int cnt=0; cnt<len;cnt++)
        ;//Serial.print(telegram[cnt]);
    }
  }

  Serial.println("");
  Serial.print("Telegram =");
  Serial.println(telegram); 
  
  long val = 0;
  long val2 = 0;
  
  // 1-0:1.8.1(000992.992*kWh)
  // 1-0:1.8.1 = Elektra verbruik laag tarief (DSMR v4.0)
  if (strncmp(telegram, "1-0:1.8.1", strlen("1-0:1.8.1")) == 0){ 
    mEVLT_temp =  getValue(telegram, len);
    if (mEVLT_temp > 0.1) {
      mEVLT = mEVLT_temp; // remove fault readings
    }
  }

  // 1-0:1.8.2(000560.157*kWh)
  // 1-0:1.8.2 = Elektra verbruik hoog tarief (DSMR v4.0)
  if (strncmp(telegram, "1-0:1.8.2", strlen("1-0:1.8.2")) == 0){ 
    mEVHT_temp = getValue(telegram, len);
    if (mEVHT_temp > 0.1) {
      mEVHT = mEVHT_temp; // remove fault readings
    }
    }
    
  // 1-0:2.8.1(003563.888*kWh)
  // 1-0:2.8.1(000348.890*kWh)
  // 1-0:2.8.1 = Elektra opbrengst laag tarief (DSMR v4.0)
  if (strncmp(telegram, "1-0:2.8.1", strlen("1-0:2.8.1")) == 0) {
    mEOLT_temp = getValue(telegram, len);
    if (mEOLT_temp > 0.1) {
      mEOLT = mEOLT_temp; // remove fault readings
    }
    }
   

  // 1-0:2.8.2(000859.885*kWh)
  // 1-0:2.8.2 = Elektra opbrengst hoog tarief (DSMR v4.0)
  if (strncmp(telegram, "1-0:2.8.2", strlen("1-0:2.8.2")) == 0) {
    mEOHT_temp = getValue(telegram, len);
    if (mEOHT_temp > 0.1) {
      mEOHT = mEOHT_temp; // remove fault readings
    }
  }

  // 1-0:1.7.0(00.424*kW) Actueel verbruik
  // 1-0:2.7.0(00.000*kW) Actuele teruglevering
  // 1-0:1.7.x = Electricity consumption actual usage (DSMR v4.0)
  if (strncmp(telegram, "1-0:1.7.0", strlen("1-0:1.7.0")) == 0) {
    mEAV_temp = getValue(telegram, len);
  }
    
  if (strncmp(telegram, "1-0:2.7.0", strlen("1-0:2.7.0")) == 0){
    mEAT_temp = getValue(telegram, len);
  }
  //filter out bad readings
  if (mEAV_temp+mEAT_temp < 9000 && mEAV_temp + mEAT_temp > 0.1){
    mEAV = mEAV_temp;
    mEAT = mEAT_temp;
  }
  return validCRCFound;
}

void readTelegram() {
  if (mySerial.available()) {
    Serial.println("telegram available");
    memset(telegram, 0, sizeof(telegram));
    while (mySerial.available()) {
      
      //Serial.println("myserial available");
      int len = mySerial.readBytesUntil('\n', telegram, MAXLINELENGTH);
      telegram[len] = '\n';
      telegram[len+1] = 0;
      yield();
      
      if(decodeTelegram(len+1)){
        Serial.println("decoded");
        
         UpdateElectricity();
      }
    } // end while 
 };//else{Serial.println("No telegram available");}
}

/*
***************************************************************************  
**  Program  : OnderhoudStuff, part of DSMRlogger2HTTP
**  Version  : v0.7.6
**
**  Mostly stolen from https://www.arduinoforum.de/User-Fips
**  See also https://www.arduinoforum.de/arduino-Thread-SPIFFS-DOWNLOAD-UPLOAD-DELETE-Esp8266-NodeMCU
**
***************************************************************************      
*/

File fsUploadFile;                      // Stores de actuele upload

void handleRoot() {                     // HTML onderhoud
  FSInfo fs_info;
  SPIFFS.info(fs_info);
  String onderhoudHTML;
  onderhoudHTML += "<!DOCTYPE HTML><html lang='en'>";
  onderhoudHTML += "<head>";
  onderhoudHTML += "<meta charset='UTF-8'>";
  onderhoudHTML += "<meta name= viewport content='width=device-width, initial-scale=1.0,' user-scalable=yes>";
  onderhoudHTML += "<style type='text/css'>";
  onderhoudHTML += "body {background-color: lightgray;}";
  onderhoudHTML += "</style>";
  onderhoudHTML += "</head>";
  onderhoudHTML += "<body><h1>ESP01-DSMR Onderhoud</h1><h2>Upload, Download of Verwijder</h2>";

  onderhoudHTML += "<hr><h3>Selecteer bestand om te downloaden:</h3>";
  if (!SPIFFS.begin())  TelnetStream.println("SPIFFS failed to mount !\r\n");

  Dir dir = SPIFFS.openDir("/");         // List files on SPIFFS
  while (dir.next())  {
    onderhoudHTML += "<a href ='";
    onderhoudHTML += dir.fileName();
    onderhoudHTML += "?download='>";
    onderhoudHTML += "SPIFFS";
    onderhoudHTML += dir.fileName();
    onderhoudHTML += "</a> ";
    onderhoudHTML += formatBytes(dir.fileSize()).c_str();
    onderhoudHTML += "<br>";
  }

  onderhoudHTML += "<p><hr><h3>Sleep bestand om te verwijderen:</h3>";
  onderhoudHTML += "<form action='/onderhoud' method='POST'>Om te verwijderen bestand hierheen slepen ";
  onderhoudHTML += "<input type='text' style='height:45px; font-size:15px;' name='Delete' placeholder='Bestand hier in-slepen' required>";
  onderhoudHTML += "<input type='submit' class='button' name='SUBMIT' value='Verwijderen'>";
  onderhoudHTML += "</form><p><br>";
  
  onderhoudHTML += "<hr><h3>Bestand uploaden:</h3>";
  onderhoudHTML += "<form method='POST' action='/onderhoud/upload' enctype='multipart/form-data' style='height:35px;'>";
  onderhoudHTML += "<input type='file' name='upload' style='height:35px; font-size:13px;' required>";
  onderhoudHTML += "<input type='submit' value='Upload' class='button'>";
  onderhoudHTML += "</form><p><br>";
  onderhoudHTML += "<hr>Omvang SPIFFS: ";
  onderhoudHTML += formatBytes(fs_info.totalBytes).c_str();      
  onderhoudHTML += "<br>Waarvan in gebruik: ";
  onderhoudHTML += formatBytes(fs_info.usedBytes).c_str();      
  onderhoudHTML += "<p>";

  onderhoudHTML += "<hr>";
  onderhoudHTML += "<div style='width: 30%'>";
  onderhoudHTML += "  <form style='float: left;' action='/ReBoot' method='POST'>ReBoot DSMRlogger ";
  onderhoudHTML += "    <input type='submit' class='button' name='SUBMIT' value='ReBoot'>";
  onderhoudHTML += "  </form>";

  onderhoudHTML += "  <form style='float: right;' action='/' method='POST'> &nbsp; Exit Onderhoud ";
  onderhoudHTML += "   <input type='submit' class='button' name='SUBMIT' value='Exit'>";
  onderhoudHTML += "  </form>";
  onderhoudHTML += "</div>";
  onderhoudHTML += "<div style='width: 80%'>&nbsp;</div>";
  
  onderhoudHTML += "</body></html>\r\n";

  server.send(200, "text/html", onderhoudHTML);
}

String formatBytes(size_t bytes) {
  if (bytes < 1024) {
    return String(bytes) + " Byte";
  } else if (bytes < (1024 * 1024)) {
    return String(bytes / 1024.0) + " KB";
  } else if (bytes < (1024 * 1024 * 1024)) {
    return String(bytes / 1024.0 / 1024.0) + " MB";
  }
}

String getContentType(String filename) {
  if (server.hasArg("download")) return "application/octet-stream";
  else if (filename.endsWith(".htm")) return "text/html";
  else if (filename.endsWith(".html")) return "text/html";
  else if (filename.endsWith(".css")) return "text/css";
  else if (filename.endsWith(".js")) return "application/javascript";
  else if (filename.endsWith(".png")) return "image/png";
  else if (filename.endsWith(".gif")) return "image/gif";
  else if (filename.endsWith(".jpg")) return "image/jpeg";
  else if (filename.endsWith(".ico")) return "image/x-icon";
  else if (filename.endsWith(".xml")) return "text/xml";
  else if (filename.endsWith(".pdf")) return "application/x-pdf";
  else if (filename.endsWith(".zip")) return "application/x-zip";
  else if (filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}

void handleReBoot() {
  String redirectHTML = "";

  redirectHTML += "<!DOCTYPE HTML><html lang='en-US'>";
  redirectHTML += "<head>";
  redirectHTML += "<meta charset='UTF-8'>";
  redirectHTML += "<style type='text/css'>";
  redirectHTML += "body {background-color: lightgray;}";
  redirectHTML += "</style>";
  redirectHTML += "<title>Redirect to DSMRlogger</title>";
  redirectHTML += "</head>";
  redirectHTML += "<body><h1>ESP01-DSMR Onderhoud</h1>";
  redirectHTML += "<h3>Rebooting ESP01-DSMR</h3>";
  redirectHTML += "<br><div style='width: 500px; position: relative; font-size: 25px;'>";
  redirectHTML += "  <div style='float: left;'>Redirect over &nbsp;</div>";
  redirectHTML += "  <div style='float: left;' id='counter'>20</div>";
  redirectHTML += "  <div style='float: left;'>&nbsp; seconden ...</div>";
  redirectHTML += "  <div style='float: right;'>&nbsp;</div>";
  redirectHTML += "</div>";
  redirectHTML += "<!-- Note: don't tell people to `click` the link, just tell them that it is a link. -->";
  redirectHTML += "<br><br><hr>If you are not redirected automatically, click this <a href='/'>ESP01-DSMR</a>.";
  redirectHTML += "  <script>";
  redirectHTML += "      setInterval(function() {";
  redirectHTML += "          var div = document.querySelector('#counter');";
  redirectHTML += "          var count = div.textContent * 1 - 1;";
  redirectHTML += "          div.textContent = count;";
  redirectHTML += "          if (count <= 0) {";
  redirectHTML += "              window.location.replace('/'); ";
  redirectHTML += "          } ";
  redirectHTML += "      }, 1000); ";
  redirectHTML += "  </script> ";
  redirectHTML += "</body></html>\r\n";
  
  server.send(200, "text/html", redirectHTML);
  
  TelnetStream.println("ReBoot DSMRlogger ..");
  TelnetStream.flush();
  if (debug) {
    Serial.println("ReBoot DSMRlogger ..");
    Serial.flush();
  }
  delay(1000);
  ESP.reset();
  
} // handleReBoot()


bool handleFileRead(String path) {
  TelnetStream.println("handleFileRead: " + path);
  if (path.endsWith("/")) path += "index.html";
  String contentType = getContentType(path);
  TelnetStream.println(contentType);
  String pathWithGz = path + ".gz";
  if (SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)) {
    if (SPIFFS.exists(pathWithGz))
      path += ".gz";
    File file = SPIFFS.open(path, "r");
    size_t sent = server.streamFile(file, contentType);
    file.close();
    return true;
  }
  return false;
  
} // handleFileRead()


void handleFileDelete() {                               
  String file2Delete, hostNameURL, IPaddressURL;                     
  if (server.args() == 0) return handleRoot();
  if (server.hasArg("Delete")) {
    file2Delete = server.arg("Delete");
    file2Delete.toLowerCase();
    Dir dir = SPIFFS.openDir("/");
    while (dir.next())    {
      String path = dir.fileName();
      path.replace(" ", "%20"); path.replace("ä", "%C3%A4"); path.replace("Ä", "%C3%84"); path.replace("ö", "%C3%B6"); path.replace("Ö", "%C3%96");
      path.replace("ü", "%C3%BC"); path.replace("Ü", "%C3%9C"); path.replace("ß", "%C3%9F"); path.replace("€", "%E2%82%AC");
      hostNameURL   = "http://" + String(HOSTNAME) + ".local" + path + "?download=";
      hostNameURL.toLowerCase();
      IPaddressURL  = "http://" + WiFi.localIP().toString() + path + "?download=";
      IPaddressURL.toLowerCase();
    //if (server.arg("Delete") != "http://" + WiFi.localIP().toString() + path + "?download=" )
      if ( (file2Delete != hostNameURL ) && (file2Delete != IPaddressURL ) ) {
        continue;
      }
      SPIFFS.remove(dir.fileName());
      String header = "HTTP/1.1 303 OK\r\nLocation:";
      header += server.uri();
      header += "\r\nCache-Control: no-cache\r\n\r\n";
      server.sendContent(header);
      return;
    }
    String onderhoudHTML;                                    
    onderhoudHTML += "<!DOCTYPE HTML><html lang='de'><head><meta charset='UTF-8'><meta name= viewport content=width=device-width, initial-scale=1.0, user-scalable=yes>";
    onderhoudHTML += "<meta http-equiv='refresh' content='3; URL=";
    onderhoudHTML += server.uri();
    onderhoudHTML += "'><style>body {background-color: powderblue;}</style></head>\r\n<body><center><h2>Bestand niet gevonden</h2>wacht 3 seconden...</center>";
    server.send(200, "text/html", onderhoudHTML );
  }
  
} // handleFileDelete()


void handleFileUpload() {                                 
  if (server.uri() != "/onderhoud/upload") return;
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    String filename = upload.filename;
    TelnetStream.print("handleFileUpload Name: "); TelnetStream.println(filename);
    if (filename.length() > 30) {
      int x = filename.length() - 30;
      filename = filename.substring(x, 30 + x);
    }
    if (!filename.startsWith("/")) filename = "/" + filename;
    TelnetStream.print("handleFileUpload Name: "); TelnetStream.println(filename);
    fsUploadFile = SPIFFS.open(filename, "w");
    filename = String();
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    TelnetStream.print("handleFileUpload Data: "); TelnetStream.println(upload.currentSize);
    if (fsUploadFile)
      fsUploadFile.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (fsUploadFile)
      fsUploadFile.close();
    yield();
    TelnetStream.print("handleFileUpload Size: "); TelnetStream.println(upload.totalSize);
    handleRoot();
  }
  
} // handleFileUpload()


//void formatSpiffs() {       // Format SPIFFS
//  SPIFFS.format();
//  handleRoot();
//}
