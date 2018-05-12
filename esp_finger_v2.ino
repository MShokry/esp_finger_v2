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
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager
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

#define VER "2.1"

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



#define API "?name="
// WIFI Configuration
char update_server[30] = "7elol.com";
char update_server_page[30] = "/update/finger.php";
char server[30] = "7elol.com";
char server_page[30] = "/update/rfid.php";
char server_port[6] = "80";
char PLACE[20] = "Nest1";
//default custom static IP
char static_ip[16] = "192.168.0.10";
char static_gw[16] = "192.168.0.1";
char static_sn[16] = "255.255.255.0";

char SSID[24] = "finger";
char SSID_pwd[24] = "";
//flag for saving data
bool shouldSaveConfig = false;

//callback notifying us of the need to save config
void saveConfigCallback () {
  PRINTDEBUGLN("Should save config");
  shouldSaveConfig = true;
}

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

int getFingerprintEnroll(int id);
void enroll();
int deleteFingerprint(int id);

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
t_httpUpdate_return ret = ESPhttpUpdate.update(String(update_server)+String(update_server_page), String(VER));
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
 *  API and Server
 *************************************************************************************/
//#define API "/request_service-edit-1.html?json=true&ajax_page=true&place="
ESP8266WebServer webServer(80); //Web Server

/**************************************************************************************
 *  Loading Configurations from json files
 **************************************************************************************/

bool load_wifi(){
  if (SPIFFS.begin()) {
    PRINTDEBUGLN("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      PRINTDEBUGLN("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        PRINTDEBUGLN("opened config file");
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
        if (json.success()) {
          PRINTDEBUGLN("\nparsed json");
          strcpy(SSID, json["ssid"]);
          strcpy(SSID_pwd, json["ssid_pwd"]);
          PRINTDEBUGLN(SSID);
          PRINTDEBUGLN(SSID_pwd);
          if(json["update_server"]) 
          strcpy(update_server, json["update_server"]);          
          PRINTDEBUGLN(update_server);
          if(json["update_server_page"]) 
            strcpy(update_server_page, json["update_server_page"]);          
          PRINTDEBUGLN(update_server_page);
          //if(json["server"]) 
          strcpy(server, json["server"]);
          PRINTDEBUGLN(server);
          if(json["server_page"])
            strcpy(server_page, json["server_page"]);
          PRINTDEBUGLN(server_page);
          //if(json["server_port"]) 
          //  strcpy(server_port, json["server_port"]);
          //if(json["place"])
          strcpy(PLACE, json["place"]);
          PRINTDEBUGLN(PLACE);
          if(json["ip"]) {
            PRINTDEBUGLN("setting custom ip from config");
            strcpy(static_ip, json["ip"]);
            strcpy(static_gw, json["gateway"]);
            strcpy(static_sn, json["subnet"]);
            PRINTDEBUGLN(static_ip);
/*            PRINTDEBUGLN("converting ip");
            IPAddress ip = ipFromCharArray(static_ip);
            PRINTDEBUGLN(ip);*/
          } else {
            PRINTDEBUGLN("no custom ip in config");
            return false;
          }
        } else {
          PRINTDEBUGLN("failed to load json config");
          return false;
        }
      }
    }else{
      PRINTDEBUGLN("Files Doesn't exist");
    }
  } else {
    PRINTDEBUGLN("failed to mount FS");
    return false;
  }
  //end read
  PRINTDEBUGLN(static_ip);
  PRINTDEBUGLN(static_gw);
  PRINTDEBUGLN(static_sn);
  return true;
}
/*************************************************************************************
 *  Pinging the server
 *************************************************************************************/
//Pingging main
bool ping (String msg="ping"){
   // Use WiFiClientSecure class to create TLS connection
  WiFiClient client;
  PRINTDEBUG("Connecting to:");
  PRINTDEBUG(server);
  if(!client.connect(server, 80))
  {
    PRINTDEBUG("Connection failed");
    buzzing(2,300,200);
    return false;
  }
  // URL request
  String url = String(API) + String(PLACE) + String("&msg=");
  url += msg;
  PRINTDEBUG("Requesting URL:");
  PRINTDEBUG(url);
  client.print(String("GET ") + url + " HTTP/1.1\r\n" +
               "Host: " + server + "\r\n" +
               "User-Agent: 7elolLock\r\n" +
               "Connection: close\r\n\r\n");
  PRINTDEBUG("Request sent");
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
  PRINTDEBUG(server);
  //delay(10);
  // Check connection
  if(!client.connect(server, 80))
  {
    PRINTDEBUG("Connection failed");
    buzzing(4,300,200);
    return false;
  }
  // URL request
  String url = String(API) + String(PLACE) + String("&msg=");
  url+=String(msgs) ;
  url+=String("&data=");
  url+=String(data) ;
  PRINTDEBUG("Requesting URL:");
  PRINTDEBUG(url);
  client.print(String("GET ") + url + " HTTP/1.1\r\n" +
               "Host: " + server + "\r\n" +
               "User-Agent: 7elolLock\r\n" +
               "Connection: close\r\n\r\n");
  PRINTDEBUG("Request sent");
  PRINTDEBUG("Headers:");
  PRINTDEBUG("========");
  while(client.connected())
  {
    String line = client.readStringUntil('\n');
    PRINTDEBUG(line);
    if(line == "\r")
    {
      PRINTDEBUG(line);
      PRINTDEBUG("========");      
      PRINTDEBUG("Headers received");
      break;
    }
  }
  String line = client.readStringUntil('\n');
  PRINTDEBUG("Reply was:");
  PRINTDEBUG("==========");
  PRINTDEBUG(line);
  PRINTDEBUG("==========");
  PRINTDEBUG("Closing connection");
  if(line == "{\"" + String(PLACE) + "\":true}")    //Check the server response
  {
    PRINTDEBUG("UID is authorized");
    return true;
  }
  else
  {
    PRINTDEBUG("UID is not authorized");
    return false;
  }
}

/*************************************************************************************
 *  ////////////////////ROOT Server Handler ////////////////
 *************************************************************************************/
//root page can be accessed only if authentification is ok
void handleRoot(){
  PRINTDEBUG("Enter handleRoot");
  String header;
  String wifi_conf = webServer.arg("wifi");
  String server_conf = webServer.arg("server");
  String master_conf = webServer.arg("master");
  String action = webServer.arg("action");
  String ids = webServer.arg("ID");
  int id = ids.toInt();


  if(action == String(PLACE) ){
    String content = "{\"" + String(PLACE) + "\":\"Opend\"}";
    webServer.send(200, "text/html", content);
    PRINTDEBUG("Authorized Server");
    digitalWrite(INDICATOR, LOW);  // Turn the LED off by making the voltage LOW
    buzzing(1,400,20);
    unlocked = true;
    digitalWrite(RELAY_PIN, HIGH);          // Turns Relay ON
    previousMillis = millis();
    PRINTDEBUG("Unlock ON");
    return;
  }else if(action == String("RESET")){
    String content = "{\"" + String(PLACE) + "\":\"Resitting Config\"}";
    webServer.send(200, "text/html", content);
    buzzing(1,600,20);
    resetSelf();
    return;
  }else if(action == String("ADD")){
    if(id){
      buzzing(1,600,20);
      getFingerprintEnroll(id);
    }else{
      buzzing(1,600,20);
      enroll();
    }
    String content = "{\"" + String(PLACE) + "\":\"Adding\"}";
    webServer.send(200, "text/html", content);
    return;
  }else if(action == String("DELETE")){
    if(id)
      deleteFingerprint(id);
    String content = "{\"" + String(PLACE) + "\":\"Deleting\"}";
    webServer.send(200, "text/html", content);
    return;
  } else{
    String content = "{\"" + String(PLACE) + "\":\"Recived\"}";
    webServer.send(200, "text/html", content);
  }
  if(wifi_conf){
    PRINTDEBUG(wifi_conf);
    DynamicJsonBuffer jsonBuffer(300);
    JsonObject& root = jsonBuffer.parseObject(wifi_conf);
    if (root.success())
      {
        File configFile = SPIFFS.open("/config.json", "w+");
        if (!configFile) {
          PRINTDEBUG("Failed to open config file for writing");
        }else{
          PRINTDEBUG("Writing to file config.json");
          configFile.println(wifi_conf);
          //root.printTo(configFile);
          configFile.close();
          buzzing(2,300,200);
          delay(2000);
          ESP.reset();
        }
      }else{
        PRINTDEBUG("Unable to handle JSON file");
      }
   } 
if(server_conf){
      PRINTDEBUG(server_conf);
    DynamicJsonBuffer jsonBuffer(300);
    JsonObject& root = jsonBuffer.parseObject(server_conf);
    if (root.success())
      {
        File configFile = SPIFFS.open("/server.json", "w+");
        if (!configFile) {
          PRINTDEBUG("Failed to open server file for writing");
        }else{
          PRINTDEBUG("Writing to file server.json");
          configFile.println(server_conf);
          //root.printTo(configFile);
          configFile.close();
          buzzing(2,300,200);
          delay(2000);
          ESP.reset();
        }
      }else{
        PRINTDEBUG("Unable to handle JSON file");
      }
  }
}

/*************************************************************************************
 *  ////////////////////SETUP ////////////////
 *************************************************************************************/
WiFiManager wifiManager;
void setup() {
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
    Serial.println("version:: 2.1");

  #endif

  PRINTDEBUGLN("Finger Print");

  pinMode(INDICATOR, OUTPUT);         // Initialize the INDICATOR pin as an output
   digitalWrite(INDICATOR, HIGH);      // Turn the LED off by making the voltage HIGH
  delay(10);
  
  if (!SPIFFS.begin()) {
    Serial.println("Failed to mount file system");
  }
  EEPROM.begin(1025);

  mySerial.begin(57600);
  
  configTime(1 * 3600, 1 * 3600, "pool.ntp.org", "time.nist.gov");
  delay(500);
  buzzing(1,300,20);    
  //load_server();
  //SPIFFS.format();
  load_wifi();
  //Wifi connection
  //connectWifimul();  
  delay(100);
  digitalWrite(INDICATOR, LOW);      // Turn the LED off by making the voltage HIGH
  WiFiManagerParameter custom_update("update_server", "Update Server", update_server, 34);
    WiFiManagerParameter custom_update_page("update_server_page", "Update Server route", update_server_page, 34);
    WiFiManagerParameter custom_server("server", "Server name", server, 34);
    WiFiManagerParameter custom_server_page("server_page", "Server route", server_page, 34);
    WiFiManagerParameter custom_place("place", "Location Name", PLACE, 20);
    
    #if (DEBUG==1)
    wifiManager.setDebugOutput(true);
    #endif
    #if (DEBUG==0)
    wifiManager.setDebugOutput(false);
    #endif
    //set config save notify callback
    wifiManager.setSaveConfigCallback(saveConfigCallback);

    //add all your parameters here
    wifiManager.addParameter(&custom_update);
    wifiManager.addParameter(&custom_update_page);
    wifiManager.addParameter(&custom_server);
    wifiManager.addParameter(&custom_server_page);
    wifiManager.addParameter(&custom_place);
    //set minimu quality of signal so it ignores AP's under that quality
    //defaults to 8% to reduce the power next
    //wifiManager.setMinimumSignalQuality();
    
    wifiManager.setTimeout(280); // 14.7 min in seconds
    wifiManager.autoConnect(SSID,SSID_pwd);
    
    strcpy(update_server, custom_update.getValue());
    strcpy(update_server_page, custom_update_page.getValue());
    strcpy(server, custom_server.getValue());
    strcpy(server_page, custom_server_page.getValue());
    strcpy(PLACE, custom_place.getValue());
    //strcpy(SSID, wifiManager.getSSID());
    //strcpy(SSID_pwd, wifiManager.getSSIDpwd());
    //PRINTDEBUGLN(WiFiManager.getConfigPortalSSID())
    if (shouldSaveConfig) {
      PRINTDEBUGLN("saving config");
      DynamicJsonBuffer jsonBuffer;
      JsonObject& json = jsonBuffer.createObject();
      json["server"] = server;
      json["server_page"] = server_page;
      json["update_server"] = update_server;
      json["update_server_page"] = update_server_page;
      json["place"] = PLACE;
      json["ssid"] =  WiFi.SSID();
      json["ssid_pwd"] =  WiFi.psk();
      json["ip"] = WiFi.localIP().toString();
      json["gateway"] = WiFi.gatewayIP().toString();
      json["subnet"] = WiFi.subnetMask().toString();
      File configFile = SPIFFS.open("/config.json", "w");
      if (!configFile) {
        PRINTDEBUGLN("failed to open config file for writing");
      }
      #if (DEBUG==1)
        json.prettyPrintTo(Serial);
      #endif
      json.printTo(configFile);
      configFile.close();
    }
  delay(10);

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
  
  webServer.on("/", handleRoot);
  webServer.begin();
  if (true) /*WIFI_Status == STA*/
  {
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
  if(/*WIFI_Status == STA*/1)
  {
    if(currentMillis - previousMillis >= (interval*10)){
      while(!wifiManager.autoConnect(SSID, SSID_pwd))
      {
        digitalWrite(INDICATOR, HIGH);      // Turn the LED off by making the voltage HIGH
        //connectWifi(ssid, password);          // Try to connect WiFi for 30 seconds
        //connectWifimul();
        digitalWrite(INDICATOR, LOW);      // Turn the LED off by making the voltage HIGH
        connectFails++;
        buzzing(3,300,200);
        previousMillis = millis();
        //if (connectFails > 4)
          //resetSelf();                        // If 2.5 minutes passed with no connection - Reset Self
      }
     }
     }
  webServer.handleClient();

  getFingerprintID();
  delay(50);            //don't ned to run this at full speed.
}

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
  
  // found a match!
  PRINTDEBUG("Found ID #"); PRINTDEBUG(finger.fingerID); 
  PRINTDEBUG(" with confidence of "); PRINTDEBUGLN(finger.confidence); 


PRINTDEBUGLN("Remove finger...");
  while (p != FINGERPRINT_NOFINGER){
    p = finger.getImage();
  }
}


void enroll(){
PRINTDEBUGLN("Send any character to enroll a finger...");
  while (Serial.available() == 0);
  PRINTDEBUGLN("Searching for a free slot to store the template...");
  int16_t id;
  if (get_free_id(&id)){
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
