/*
* By: Mahmoud Shokry
* Finger Print  access control/ attendance system
* TODO
* See examples/Setparamiter.ino 
*/

#include <EEPROM.h>
#include <SPI.h>
#include <SoftwareSerial.h>
#include <FPM.h>

#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
//#include <WiFiClient.h>
#include <ESP8266WebServer.h>

#include <ArduinoJson.h>
#include "FS.h"

#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>


#define DEBUG 1
#if (DEBUG==1)
  #define PRINTDEBUGLN(STR) Serial.println(STR)
  #define PRINTDEBUG(STR) Serial.print(STR)
#else
  #define PRINTDEBUGLN(STR) /*NOTHING*/
  #define PRINTDEBUG(STR) /*NOTHING*/
#endif


/*************************************************************************************
 *  //Relay and outputs HW DEB
 *************************************************************************************/
#define RELAY_PIN 4 // Signal pin for relay
#define BUZZER  5
#define INDICATOR 10
#define LED 10 
#define Door_sensor 9
#define Door_button 16
uint32_t previousMillis = 0;        // will store last time LED was updated
const unsigned long interval = 5000;     // interval at which open relay (milliseconds)
const unsigned long fing_interval = 60000;     // interval at which waiting the finger print (milliseconds)
bool unlocked=false;

// Search the DB for a print with this example
#define TEMPLATES_PER_PAGE  256
/*RED  -> VCC
BLACK -> GND
YELLOW  -> RXD 
GREEN -> TXD
*/
SoftwareSerial mySerial(14, 12); // RX, TX 12-D6 14-D5
FPM finger;


/************************************************************************************
 * ESP Hellpers
 *************************************************************************************/
// Inifinite loop - Reset self
void resetSelf() {
  PRINTDEBUGLN("Reseting");

  ESP.reset();
  while(1);
}
//Buzzing
void buzzing(int times=2,int delayh = 200,int delayl = 100){
  for(int i=0;i<times;i++){
    digitalWrite(BUZZER, HIGH);         // Turns Buzzer ON
    delay(delayh);
    digitalWrite(BUZZER, LOW);         // Turns Relay Off
    delay(delayl);
  }
}
//Updating ESP From Internet
void update(){
t_httpUpdate_return ret = ESPhttpUpdate.update("http://7elol.com/update/finger.php", "2.2");
switch(ret) {
    case HTTP_UPDATE_FAILED:
        #if (DEBUG==1)
          Serial.printf("HTTP_UPDATE_FAILD Error (%d): %s", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());;
        #endif 
        PRINTDEBUGLN("[update] Update failed.");
        break;
    case HTTP_UPDATE_NO_UPDATES:
        PRINTDEBUGLN("[update] Update no Update.");
        break;
    case HTTP_UPDATE_OK:
        PRINTDEBUGLN("[update] Update ok."); // may not called we reboot the ESP
       // resetSelf();
        break;
  }
}

/*************************************************************************************
 *  Wifi Control and  Connection
 *************************************************************************************/
//WiFi
char ap1[22] = "HIMC-EBDA3";
char ap1_pwd[22] = "EBDA3123";
char ap2[22] = "7elolcom";
char ap2_pwd[22] = "#$7elol(R)";
char ap3[22] = "ebda3-eg.com";
char ap3_pwd[22] = "#$ebda3(R)";
char ap4[22] = "ebda3 egypt";
char ap4_pwd[22] = "qweasdzxc12345";
const char* AP_ssid = "7elol.Lock"; // The name of the Wi-Fi network that will be created
const char* AP_password = "#$Lock()";   // The password required to connect to it, leave 
#define AP 1
#define STA 0
bool WIFI_Status = AP;
ESP8266WiFiMulti wifiMulti;

/*************************************************************************************
 *  Function to connect WiFi 
 *************************************************************************************/
void connectWifimul(){
  int WiFiCounter = 0; 
  if(WIFI_Status == STA){ //Set as STA
      wifiMulti.addAP(ap1, ap1_pwd);
      wifiMulti.addAP(ap2, ap2_pwd);
      wifiMulti.addAP(ap3, ap3_pwd);
      wifiMulti.addAP(ap4, ap4_pwd);
      while(wifiMulti.run() != WL_CONNECTED && WiFiCounter < 30) {
        delay(1000);
        WiFiCounter++;
        #if (DEBUG==1)
          Serial.print(".");
        #endif 
      }
      //PRINTDEBUG();
      PRINTDEBUGLN("WiFi connected");
      PRINTDEBUG("IP address:");
      PRINTDEBUGLN(WiFi.localIP());
  }else{ // SEt as AP
    WiFi.softAP(AP_ssid, AP_password);             // Start the access point
    PRINTDEBUG("Access Point:");
    PRINTDEBUGLN(AP_ssid);
    PRINTDEBUGLN("started");
  
    PRINTDEBUG("IP address:\t");
    PRINTDEBUGLN(WiFi.softAPIP());         // Send the IP address of the ESP8266 to the 
  }
}

/*************************************************************************************
 *  API and Server
 *************************************************************************************/
char PLACE[20] = "FINGER1";
char host[50] = "192.168.1.50";
unsigned int httpsPort = 80; //443
#define API "/lock.php?place="
//#define API "/request_service-edit-1.html?json=true&ajax_page=true&place="
ESP8266WebServer server(80); //Web Server

/**************************************************************************************
 *  Loading Configurations from json files
 **************************************************************************************/
bool load_wifi(){
  PRINTDEBUGLN("##############################");
  PRINTDEBUGLN("WIFI File Reading");
  PRINTDEBUGLN("##############################");
  if (!SPIFFS.begin()) {
  PRINTDEBUGLN("failed to mount FS");
    WIFI_Status = AP;
    WiFi.mode(WIFI_AP);
    return false;
  }
  PRINTDEBUGLN("mounted file system");
  if (!SPIFFS.exists("/config.json")) {
    PRINTDEBUGLN("Failed to open config file - File not exist");
    WIFI_Status = AP;
    WiFi.mode(WIFI_AP);
    return false;
  }
  PRINTDEBUGLN("File exists");
  File configFile = SPIFFS.open("/config.json", "r");
  if (!configFile) {
    PRINTDEBUGLN("Failed to open config file");
    WIFI_Status = AP;
    WiFi.mode(WIFI_AP);
    return false;
  }
  size_t size = configFile.size();
  if (size > 1024) {
    PRINTDEBUGLN("Config file size is too large");
    WIFI_Status = AP;
    WiFi.mode(WIFI_AP);
    return false;
  }
  PRINTDEBUGLN("File opend ,, Reading");
  // Allocate a buffer to store contents of the file.
  std::unique_ptr<char[]> buf(new char[size]);
  configFile.readBytes(buf.get(), size);
  DynamicJsonBuffer jsonBuffer;
  JsonObject& json = jsonBuffer.parseObject(buf.get());
  configFile.close();
  PRINTDEBUGLN("Config File content");
  #if (DEBUG==1)  
      json.printTo(Serial);
  #endif

  if (!json.success()) {
    PRINTDEBUGLN("Failed to parse config file");
    WIFI_Status = AP;
    WiFi.mode(WIFI_AP);
    return false;
  }
  PRINTDEBUGLN("\nparsed json");
  if(json.containsKey("ap1") && json.containsKey("ap1_pwd")){  
    strcpy(ap1, json["ap1"]);
    strcpy(ap1_pwd, json["ap1_pwd"]);
    PRINTDEBUG("Ap1 ");
    //PRINTDEBUG(apl);
    PRINTDEBUG(ap1);
    PRINTDEBUG(" : ");
    PRINTDEBUGLN(ap1_pwd);
  }
  if(json.containsKey("ap2") && json.containsKey("ap2_pwd")){
    strcpy(ap2, json["ap2"]);
    strcpy(ap2_pwd, json["ap2_pwd"]);
    PRINTDEBUG("Ap2 ");
    PRINTDEBUG(ap2);
    PRINTDEBUG(" : ");
    PRINTDEBUGLN(ap2_pwd);
  }
  if(json.containsKey("ap3") && json.containsKey("ap3_pwd")){
    strcpy(ap2, json["ap3"]);
    strcpy(ap2_pwd, json["ap3_pwd"]);
    PRINTDEBUG("Ap3");
    PRINTDEBUG(ap3);
    PRINTDEBUG(" : ");
    PRINTDEBUGLN(ap3_pwd);
  }
    if(json.containsKey("ap4") && json.containsKey("ap4_pwd")){
    strcpy(ap2, json["ap4"]);
    strcpy(ap2_pwd, json["ap4_pwd"]);
    PRINTDEBUG("Ap4 ");
    PRINTDEBUG(ap4);
    PRINTDEBUG(" : ");
    PRINTDEBUGLN(ap4_pwd);
  }
  WIFI_Status = STA;
  WiFi.mode(WIFI_STA);
  return true;
}
//Load Server Data
bool load_server(){
  PRINTDEBUGLN("##############################");
  PRINTDEBUGLN("Server File Reading");
  PRINTDEBUGLN("##############################");
  if (!SPIFFS.begin()) {
  PRINTDEBUGLN("failed to mount FS");
    return false;
  }
  PRINTDEBUGLN("mounted file system");

  if (!SPIFFS.exists("/server.json")) {
    PRINTDEBUGLN("Failed to open config file - File not exist");
    return false;
  }
  PRINTDEBUGLN("File exists");

  File configFile = SPIFFS.open("/server.json", "r");
  if (!configFile) {
    PRINTDEBUGLN("Failed to open server file");
    return false;
  }

  size_t size = configFile.size();
  if (size > 1024) {
    PRINTDEBUGLN("Config file size is too large");
    return false;
  }
  // Allocate a buffer to store contents of the file.
  std::unique_ptr<char[]> buf(new char[size]);
  configFile.readBytes(buf.get(), size);
  DynamicJsonBuffer jsonBuffer;
  JsonObject& json = jsonBuffer.parseObject(buf.get());
  #if (DEBUG==1)  
    json.printTo(Serial);
  #endif

  configFile.close();
  
  PRINTDEBUGLN("Server File content");
  #if (DEBUG==1)  
    json.printTo(Serial);
  #endif
  if (!json.success()) {
    PRINTDEBUGLN("Failed to parse Server file");
    return false;
  }else{
    if(json.containsKey("host") ){  
      strcpy(host, json["host"]);
      PRINTDEBUG("Server ");
      PRINTDEBUGLN( host);
    }
    if(json.containsKey("place") ){  
      strcpy(PLACE, json["place"]);
      PRINTDEBUG("Place ");
      PRINTDEBUGLN(PLACE); 
    }
  }
}


/*************************************************************************************
 *  Pinging the server
 *************************************************************************************/
//Pingging main
bool ping (String msg="ping"){
   // Use WiFiClientSecure class to create TLS connection
  WiFiClient client;
  PRINTDEBUG("Connecting to:");
  PRINTDEBUGLN(host);
  if(!client.connect(host, httpsPort))
  {
    PRINTDEBUGLN("Connection failed");
    buzzing(2,300,200);
    return false;
  }
  // URL request
  String url = String(API) + String(PLACE) + String("&msg=");
  url += msg;
  url += String("&mac=") + String(WiFi.macAddress());
  PRINTDEBUG("Requesting URL:");
  PRINTDEBUGLN(url);
  client.print(String("GET ") + url + " HTTP/1.1\r\n" +
               "Host: " + host + "\r\n" +
               "User-Agent: 7elolLock\r\n" +
               "Connection: close\r\n\r\n");
  PRINTDEBUGLN("Request sent");
  return true;
}

/*************************************************
 *   Helper routine to check authorization 
 *************************************************/
// Helper routine to check authorization
bool msg (String msgs ,String data){
  // Use WiFiClientSecure class to create TLS connection
  WiFiClient client;
  PRINTDEBUG("Connecting to:");
  PRINTDEBUGLN(host);
  //delay(10);
  // Check connection
  if(!client.connect(host, httpsPort))
  {
    PRINTDEBUGLN("Connection failed");
    buzzing(4,300,200);
    return false;
  }
  // URL request
  String url = String(API) + String(PLACE) + String("&msg=");
  url+=String(msgs) ;
  url+=String("&data=");
  url+=String(data) ;
  url += String("&mac=") + String(WiFi.macAddress());
  PRINTDEBUG("Requesting URL:");
  PRINTDEBUGLN(url);
  client.print(String("GET ") + url + " HTTP/1.1\r\n" +
               "Host: " + host + "\r\n" +
               "User-Agent: 7elolLock\r\n" +
               "Connection: close\r\n\r\n");
  PRINTDEBUGLN("Request sent");
  PRINTDEBUGLN("Headers:");
  PRINTDEBUGLN("========");
  while(client.connected())
  {
    String line = client.readStringUntil('\n');
    PRINTDEBUG(line);
    if(line == "\r")
    {
      PRINTDEBUGLN(line);
      PRINTDEBUGLN("========");      
      PRINTDEBUGLN("Headers received");
      break;
    }
  }
  String line = client.readStringUntil('\n');
  PRINTDEBUGLN("Reply was:");
  PRINTDEBUGLN("==========");
  PRINTDEBUGLN(line);
  PRINTDEBUGLN("==========");
  PRINTDEBUGLN("Closing connection");
  if(line == "{\"" + String(PLACE) + "\":true}")    //Check the server response
  {
    PRINTDEBUGLN("UID is authorized");
    return true;
  }
  else
  {
    PRINTDEBUGLN("UID is not authorized");
    return false;
  }
}

/*************************************************************************************
 *  ////////////////////ROOT Server Handler ////////////////
 *************************************************************************************/
//root page can be accessed only if authentification is ok
void handleRoot(){
  PRINTDEBUGLN("Enter handleRoot");
  String header;
  String wifi_conf = server.arg("wifi");
  String server_conf = server.arg("server");
  String master_conf = server.arg("master");
  String action = server.arg("action");
  String ids = server.arg("ID");
  int id = ids.toInt();


  if(action == String(PLACE) ){
    String content = "{\"" + String(PLACE) + "\":\"Opend\"}";
    server.send(200, "text/html", content);
    PRINTDEBUGLN("Authorized Server");
    digitalWrite(INDICATOR, LOW);  // Turn the LED off by making the voltage LOW
    buzzing(1,400,20);
    unlocked = true;
    digitalWrite(RELAY_PIN, HIGH);          // Turns Relay ON
    previousMillis = millis();
    PRINTDEBUGLN("Unlock ON");
    return;
  }else if(action == String("RESET")){
    String content = "{\"" + String(PLACE) + "\":\"Resitting Config\"}";
    server.send(200, "text/html", content);
    buzzing(1,600,20);
    resetSelf();
    return;
  }else if(action == String("ADD")){
    if(id){
      buzzing(1,600,20);
      previousMillis = millis();
      getFingerprintEnroll(id);
    }else{
      buzzing(1,600,20);
      enroll();
    }
    String content = "{\"" + String(PLACE) + "\":\"Adding\"}";
    server.send(200, "text/html", content);
    return;
  }else if(action == String("DELETE")){
    if(id)
      deleteFingerprint(id);
    String content = "{\"" + String(PLACE) + "\":\"Deleting\"}";
    server.send(200, "text/html", content);
    return;
  } else{
    String content = "{\"" + String(PLACE) + "\":\"Recived\"}";
    server.send(200, "text/html", content);
  }
  if(wifi_conf){
    PRINTDEBUGLN("saving config");
    PRINTDEBUGLN(wifi_conf);
    // Allocate a buffer to store contents of the file.
    //std::unique_ptr<char[]> buf(new char[600]);
    DynamicJsonBuffer jsonBuffer;
    JsonObject& root = jsonBuffer.parseObject(wifi_conf);
    #if (DEBUG==1)  
      root.printTo(Serial);
    #endif
    if (root.success())
      {
        File configFile = SPIFFS.open("/config.json", "w+");
        if (!configFile) {
          PRINTDEBUGLN("Failed to open config file for writing");
        }else{
          PRINTDEBUGLN("Writing to file config.json");
          root.printTo(configFile);
          configFile.close();
          buzzing(2,300,200);
          delay(2000);
          ESP.reset();
        }
      }else{
        PRINTDEBUGLN("Unable to handle JSON file");
      }
   } 
if(server_conf){
  PRINTDEBUGLN("saving Server config");
  PRINTDEBUGLN(server_conf);
  // Allocate a buffer to store contents of the file.
  //std::unique_ptr<char[]> buf(new char[600]);
  DynamicJsonBuffer jsonBuffer;
  JsonObject& root = jsonBuffer.parseObject(server_conf);
  #if (DEBUG==1)  
    root.printTo(Serial);
  #endif
  if (root.success())
    {
      File configFile = SPIFFS.open("/server.json", "w+");
      if (!configFile) {
        PRINTDEBUGLN("Failed to open server file for writing");
      }else{
        PRINTDEBUGLN("Writing to file server.json");
        root.printTo(configFile);
        configFile.close();
        buzzing(2,300,200);
        delay(2000);
        ESP.reset();
      }
    }else{
      PRINTDEBUGLN("Unable to handle JSON file");
    }
  }
}

/*************************************************************************************
 *  ////////////////////SETUP ////////////////
 *************************************************************************************/
void setup()  
{
  pinMode(RELAY_PIN, OUTPUT);           // Initialize the RELAY_PIN pin as an output
  digitalWrite(RELAY_PIN, LOW);         // Turns Relay Off
  pinMode(BUZZER, OUTPUT);              // Initialize the Buzzer pin as an output
  digitalWrite(BUZZER, LOW);  
  //pinMode(LED, OUTPUT);         // Initialize the INDICATOR pin as an output
  //digitalWrite(LED, LOW);   
  pinMode(Door_button, INPUT_PULLUP);
  pinMode(Door_sensor, INPUT_PULLUP);
  #if (DEBUG==1)
    Serial.begin(115200);     // Initialize serial communications
    delay(100);
    Serial.setDebugOutput(true);
    Serial.println();
    Serial.println("Author:: Mahmoud Shokry");
  #endif

  PRINTDEBUGLN("Finger Print");

  pinMode(INDICATOR, OUTPUT);         // Initialize the INDICATOR pin as an output
   digitalWrite(INDICATOR, HIGH);      // Turn the LED off by making the voltage HIGH
  delay(10);
  /*
  if (!SPIFFS.begin()) {
    Serial.println("Failed to mount file system");
  }
  EEPROM.begin(1025);
 */
  mySerial.begin(57600);
  
  configTime(1 * 3600, 1 * 3600, "pool.ntp.org", "time.nist.gov");
  delay(500);
  buzzing(1,300,20);    
  load_server();
  //SPIFFS.format();
  load_wifi();
  //Wifi connection
  connectWifimul();


  if (finger.begin(&mySerial)) {
    PRINTDEBUGLN("Found fingerprint sensor!");
    PRINTDEBUG("Capacity: "); PRINTDEBUGLN(finger.capacity);
    PRINTDEBUG("Packet length: "); PRINTDEBUGLN(finger.packetLen);
  } else {
    PRINTDEBUGLN("Did not find fingerprint sensor :(");
    while (1);
  }
  int p = finger.getTemplateCount();
  if (p == FINGERPRINT_OK){
    PRINTDEBUG(finger.templateCount); PRINTDEBUGLN(" print(s) in module memory.");
  }
  else if (p == FINGERPRINT_PACKETRECIEVEERR)
    PRINTDEBUGLN("Communication error!");
  else
    PRINTDEBUGLN("Unknown error!");
  
  delay(100);
  digitalWrite(INDICATOR, LOW);      // Turn the LED off by making the voltage HIGH
  if(WIFI_Status == STA)
    if (wifiMulti.run() != WL_CONNECTED ){
      buzzing(3,300,200);
    }
  delay(10);
  
  server.on("/", handleRoot);
  server.begin();
  if (WIFI_Status == STA){
    ping();
    update();
  }
  previousMillis = millis();


}

void loop()                     // run over and over again
{
    int connectFails = 0;
  uint32_t currentMillis = millis();
  if (currentMillis - previousMillis >= interval && unlocked) {
    digitalWrite(RELAY_PIN, LOW);
    unlocked=false;
    PRINTDEBUG("Unlock OFF");
  }

    //WiFi check
  if(WIFI_Status == STA){
    if(currentMillis - previousMillis >= (interval*10)){
      while(wifiMulti.run() != WL_CONNECTED)
      {
        digitalWrite(INDICATOR, HIGH);      // Turn the LED off by making the voltage HIGH
        //connectWifi(ssid, password);          // Try to connect WiFi for 30 seconds
        connectWifimul();
        digitalWrite(INDICATOR, LOW);      // Turn the LED off by making the voltage HIGH
        connectFails++;
        PRINTDEBUG("Failed to connect OFF");
        buzzing(3,300,200);
        previousMillis = millis();
        if (connectFails >= 4){
          buzzing(3,300,200);
          connectFails = 0;
          WIFI_Status = AP;
          WiFi.mode(WIFI_AP);
          //resetSelf();                        // If 2.5 minutes passed with no connection - Reset Self
        }
      }
     }
  }
  server.handleClient();

  getFingerprintID();
  delay(50);            //don't ned to run this at full speed.
}


/*************************************************************************************
 *  Finger Print Helprs
 *************************************************************************************/

int getFingerprintID() {
  int p = -1;
  //PRINTDEBUGLN("Waiting for a finger...");
  //while (p != FINGERPRINT_OK){
    p = finger.getImage();
    switch (p) {
      case FINGERPRINT_OK:
        PRINTDEBUGLN("Image taken");
        break;
      case FINGERPRINT_NOFINGER:
        //PRINTDEBUGLN(".");
        break;
      case FINGERPRINT_PACKETRECIEVEERR:
        PRINTDEBUGLN("Communication error");
        break;
      case FINGERPRINT_IMAGEFAIL:
        PRINTDEBUGLN("Imaging error");
        break;
      default:
        PRINTDEBUGLN("Unknown error");
        break;
    }
  //}
  if (p != FINGERPRINT_OK){
    return p;
  }

  // OK success!

  p = finger.image2Tz();
  switch (p) {
    case FINGERPRINT_OK:
      PRINTDEBUGLN("Image converted");
      break;
    case FINGERPRINT_IMAGEMESS:
      PRINTDEBUGLN("Image too messy");
      return p;
    case FINGERPRINT_PACKETRECIEVEERR:
      PRINTDEBUGLN("Communication error");
      return p;
    case FINGERPRINT_FEATUREFAIL:
      PRINTDEBUGLN("Could not find fingerprint features");
      return p;
    case FINGERPRINT_INVALIDIMAGE:
      PRINTDEBUGLN("Could not find fingerprint features");
      return p;
    default:
      PRINTDEBUGLN("Unknown error");
      return p;
  }

  
  PRINTDEBUGLN();
  // OK converted!
  p = finger.fingerFastSearch();
  if (p == FINGERPRINT_OK) {
    PRINTDEBUGLN("Found a print match!");
      // found a match!
    PRINTDEBUGLN("Remove finger...");
    PRINTDEBUG("Found ID #"); PRINTDEBUG(finger.fingerID); 
    PRINTDEBUG(" with confidence of "); PRINTDEBUGLN(finger.confidence); 
    
    msg(String ("Log"),String (finger.fingerID));
    buzzing(1,400,20);
  } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
    PRINTDEBUGLN("Communication error");
    return p;
  } else if (p == FINGERPRINT_NOTFOUND) {
    PRINTDEBUGLN("Did not find a match");
    return p;
  } else {
    PRINTDEBUGLN("Unknown error");
    return p;
  }   

/*
PRINTDEBUGLN("Remove finger...");
  while (p != FINGERPRINT_NOFINGER){
    p = finger.getImage();
  }
  */
}


void enroll(){
  //PRINTDEBUGLN("Send any character to enroll a finger...");
  PRINTDEBUGLN("##############################");
  PRINTDEBUGLN("Enroll new finger");
  PRINTDEBUGLN("##############################");
  // while (Serial.available() == 0);
  PRINTDEBUGLN("Searching for a free slot to store the template...");
  int16_t id;

  if (get_free_id(&id)){
    PRINTDEBUG("FreeID : ");
    PRINTDEBUGLN(id);
    buzzing(1,300,20);
    previousMillis = millis();
    getFingerprintEnroll(id);
  }else{
    buzzing(5,300,20);
    PRINTDEBUGLN("No free slot in flash library!");
  }
}

bool get_free_id(int16_t * id){
  int p = -1;
  for (int page = 0; page < (finger.capacity / TEMPLATES_PER_PAGE) + 1; page++){
    p = finger.getFreeIndex(page, id);
    switch (p){
      case FINGERPRINT_OK:
        if (*id != FINGERPRINT_NOFREEINDEX){
          PRINTDEBUG("Free slot at ID ");
          PRINTDEBUGLN(*id);
          return true;
        }
      case FINGERPRINT_PACKETRECIEVEERR:
        PRINTDEBUGLN("Communication error!");
        return false;
      default:
        PRINTDEBUGLN("Unknown error!");
        return false;
    }
  }
}

int deleteFingerprint(int id) {
  int p = -1;
  p = finger.deleteModel(id);
  if (p == FINGERPRINT_OK) {
    Serial.println("Deleted!");
    msg(String ("Deleted"),String (id));
    buzzing(1,300,20);   
  } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
    Serial.println("Communication error");
      buzzing(5,100,20);   
    return p;
  } else if (p == FINGERPRINT_BADLOCATION) {
    Serial.println("Could not delete in that location");
    buzzing(2,300,20);
    return p;
  } else if (p == FINGERPRINT_FLASHERR) {
    Serial.println("Error writing to flash");
    buzzing(4,100,20);
    return p;
  } else {
    Serial.print("Unknown error: 0x"); Serial.println(p, HEX);
    buzzing(6,100,20);
    return p;
  }   
}

int emptyDatabase(int id) {
  int p=-1;
  p = finger.emptyDatabase();
    if (p == FINGERPRINT_OK){
      PRINTDEBUGLN("Database empty!");
    }
    else if (p == FINGERPRINT_PACKETRECIEVEERR) {
      PRINTDEBUG("Communication error!");
    }
    else if (p == FINGERPRINT_DBCLEARFAIL) {
      PRINTDEBUGLN("Could not clear database!");
    } else {
    PRINTDEBUG("Unknown error: 0x"); PRINTDEBUGLN((p, HEX));
    return p;
    }   
}

int getFingerprintEnroll(int id) {
  int p = -1;
  PRINTDEBUGLN("Waiting for valid finger to enroll");
  while (p != FINGERPRINT_OK) {
    uint32_t currentMillis = millis();
    if (currentMillis - previousMillis >= fing_interval) {
      PRINTDEBUGLN("Time Up");
      return p;
    }
    p = finger.getImage();
    switch (p) {
    case FINGERPRINT_OK:
      PRINTDEBUGLN("Image taken");
      break;
    case FINGERPRINT_NOFINGER:
      //PRINTDEBUGLN(".");
      break;
    case FINGERPRINT_PACKETRECIEVEERR:
      PRINTDEBUGLN("Communication error");
      break;
    case FINGERPRINT_IMAGEFAIL:
      PRINTDEBUGLN("Imaging error");
      break;
    default:
      PRINTDEBUGLN("Unknown error");
      break;
    }
  }
  // OK success!

  p = finger.image2Tz(1);
  switch (p) {
    case FINGERPRINT_OK:
      PRINTDEBUGLN("Image converted");
      buzzing(1,100,20);
      break;
    case FINGERPRINT_IMAGEMESS:
      PRINTDEBUGLN("Image too messy");
      return p;
    case FINGERPRINT_PACKETRECIEVEERR:
      PRINTDEBUGLN("Communication error");
      return p;
    case FINGERPRINT_FEATUREFAIL:
      PRINTDEBUGLN("Could not find fingerprint features");
      return p;
    case FINGERPRINT_INVALIDIMAGE:
      PRINTDEBUGLN("Could not find fingerprint features");
      return p;
    default:
      PRINTDEBUGLN("Unknown error");
      return p;
  }
  
  PRINTDEBUGLN("Remove finger");
  delay(2000);
  p = 0;
  while (p != FINGERPRINT_NOFINGER) {
    p = finger.getImage();
  }

previousMillis = millis();
  p = -1;
  PRINTDEBUGLN("Place same finger again");
  while (p != FINGERPRINT_OK) {
    uint32_t currentMillis = millis();
    if (currentMillis - previousMillis >= fing_interval) {
      PRINTDEBUGLN("Time Up");
      return p;
    }
    p = finger.getImage();
    switch (p) {
    case FINGERPRINT_OK:
      PRINTDEBUGLN("Image taken");
      break;
    case FINGERPRINT_NOFINGER:
      PRINTDEBUG(".");
      break;
    case FINGERPRINT_PACKETRECIEVEERR:
      PRINTDEBUGLN("Communication error");
      break;
    case FINGERPRINT_IMAGEFAIL:
      PRINTDEBUGLN("Imaging error");
      break;
    default:
      PRINTDEBUGLN("Unknown error");
      break;
    }
  }

  // OK success!

  p = finger.image2Tz(2);
  switch (p) {
    case FINGERPRINT_OK:
      PRINTDEBUGLN("Image converted");
      buzzing(1,100,20);
      break;
    case FINGERPRINT_IMAGEMESS:
      PRINTDEBUGLN("Image too messy");
      return p;
    case FINGERPRINT_PACKETRECIEVEERR:
      PRINTDEBUGLN("Communication error");
      return p;
    case FINGERPRINT_FEATUREFAIL:
      PRINTDEBUGLN("Could not find fingerprint features");
      return p;
    case FINGERPRINT_INVALIDIMAGE:
      PRINTDEBUGLN("Could not find fingerprint features");
      return p;
    default:
      PRINTDEBUGLN("Unknown error");
      return p;
  }
  
  
  // OK converted!
  p = finger.createModel();
  if (p == FINGERPRINT_OK) {
    PRINTDEBUGLN("Prints matched!");
  } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
    PRINTDEBUGLN("Communication error");
    return p;
  } else if (p == FINGERPRINT_ENROLLMISMATCH) {
    PRINTDEBUGLN("Fingerprints did not match");
    buzzing(2,100,20);
    return p;
  } else {
    PRINTDEBUGLN("Unknown error");
    return p;
  }   
  
  PRINTDEBUG("ID "); PRINTDEBUGLN(id);
  p = finger.storeModel(id);
  if (p == FINGERPRINT_OK) {
    PRINTDEBUGLN("Stored!");
    msg(String ("Added"),String (id));
    buzzing(1,400,20);
    return 0;
  } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
    PRINTDEBUGLN("Communication error");
    return p;
  } else if (p == FINGERPRINT_BADLOCATION) {
    PRINTDEBUGLN("Could not store in that location");
    return p;
  } else if (p == FINGERPRINT_FLASHERR) {
    PRINTDEBUGLN("Error writing to flash");
    return p;
  } else {
    PRINTDEBUGLN("Unknown error");
    return p;
  }   
}
