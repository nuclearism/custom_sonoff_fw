#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>

#include <ESP8266TelegramBOT.h>
#include <fauxmoESP.h>
extern "C" {
#include "user_interface.h"
}

const char * const RST_REASONS[] =
{
    "REASON_DEFAULT_RST",
    "REASON_WDT_RST",
    "REASON_EXCEPTION_RST",
    "REASON_SOFT_WDT_RST",
    "REASON_SOFT_RESTART",
    "REASON_DEEP_SLEEP_AWAKE",
    "REASON_EXT_SYS_RST"
};

//네트워크 설정
#define WIFI_SSID "RT-AC68W"
#define WIFI_PASS "g5iago001017"
//#define WIFI_SSID "Myplace_2.4G"
//#define WIFI_PASS "31g02645"
#define MDNS_NAME "sonoff-001"

//telegram bot token
#define BOTtoken "121390740:AAFXl6jJe8vyLcWDs7hRwotKjSihyTPUh0g"
#define BOTname "DSM_Noti"
#define BOTusername "DSM_nuker_BOT"
#define CHAT_ID "54032331"

//장치 이름
#define SWITCH_NAME "bedroom"

#define LED_DELAY_INTERVAL 1000

#define SERIAL_BAUDRATE                 115200
#define MAX_DEVICE  1

//장치 타입
#define D_NONE 0
//#define D_NONE 16
#define D_RELAY 9


//GPIO 핀 설정
#define O_RELAY_ONE 12
//#define O_RELAY_ONE 16 //for testing
#define O_LEDALIVE 13

ESP8266WebServer server ( 80 );

bool debugMode = false;
bool ledPinStatus = false;

fauxmoESP fauxmo; //for alexa control

TelegramBOT bot(BOTtoken, BOTname, BOTusername);
long Bot_lasttime;   //last time messages' scan has been done
bool Start = false;

typedef struct {
  bool status;
  char ok[3];
} SAVE_LIST;
SAVE_LIST save_data;
bool temp_state;

typedef struct {
  String dev_name;
  uint8_t dev_type;
  uint8_t sel_no;
  bool state;
  bool change;
} DEVICE_t;

DEVICE_t devices[MAX_DEVICE] = {" ", D_NONE, 0, false, false};

// ----------------------------------------------------------------------------
// Wifi 
// ----------------------------------------------------------------------------

void wifiSetup() {
    // Set WIFI module to STA mode    
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID,WIFI_PASS);

    // Connect    
    Serial.printf("[WIFI] Connecting to %s ", WIFI_SSID);    
    
    // Wait    
    while (WiFi.status() != WL_CONNECTED) {        
      Serial.printf(".");        
      delay(100);    
    }    
    Serial.println();
    
    // Connected!
    Serial.printf("[WIFI] STATION Mode, SSID: %s, IP address: %s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str()); 
    WiFi.setAutoReconnect(true); //set auto reconnect when ESP8266 station is disconnected from AP.
}

// 장치 상태 변경 요청 처리 (에코)
// 상태 변경 및 상태 변경 플래그 변경
void callback(unsigned int device_id, String device_name, bool state) {
  uint8_t num = 0;
  for (num = 0; num < MAX_DEVICE; num++) {
    if(device_name == devices[num].dev_name) {
      devices[num].change = true;  
      if (state) {
        if (debugMode) Serial.printf("Device %s state: ON\n", device_name.c_str());
        devices[num].state = true;
      } else {
        if (debugMode) Serial.printf("Device %s state: OFF\n", device_name.c_str());
        devices[num].state = false;
      }
    }
  } 
}

void addDevice(uint8_t num, String dev_name, uint8_t dev_type, uint8_t sel_no) {
  devices[num].dev_name = dev_name;
  devices[num].dev_type = dev_type;
  devices[num].sel_no = sel_no;
  devices[num].state = save_data.status;
  devices[num].change = false;
  fauxmo.addDevice(devices[num].dev_name.c_str());

  //릴레이 타입인 경우 핀모드 설정
  //기본값으로 일단 설정한다...(릴레이 모듈이 low가 오프라서..)
  if (dev_type == D_RELAY) {
    pinMode(sel_no, OUTPUT);
    digitalWrite(sel_no, (save_data.status)?HIGH:LOW);
  }
}

//웹서버 응답 관련 
void handleRoot() {
  if(debugMode) Serial.println("normal response");
  char temp[400];
  int sec = millis() / 1000;
  int min = sec / 60;
  int hr = min / 60;

  snprintf ( temp, 400,

"<html>\
  <head>\
    <meta http-equiv='refresh' content='5'/>\
    <title>ESP8266 Demo</title>\
    <style>\
      body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }\
    </style>\
  </head>\
  <body>\
    <h1>Hello from ESP8266!</h1>\
    <p>Uptime: %02d:%02d:%02d</p>\
    <p>%s stat: %s</p>\
  </body>\
</html>",

    hr, min % 60, sec % 60, devices[0].dev_name.c_str(), devices[0].state?"ON":"OFF"
  );
  server.send ( 200, "text/html", temp );
}

//웹서버 응답 관련 
void handleNotFound() {
  if(debugMode) Serial.println("404 error response");
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += ( server.method() == HTTP_GET ) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";

  for ( uint8_t i = 0; i < server.args(); i++ ) {
    message += " " + server.argName ( i ) + ": " + server.arg ( i ) + "\n";
  }

  server.send ( 404, "text/plain", message );
}

//디바이스 상태 응답
void handlePoll() {
  String json = "{";
  for (int num = 0;num < MAX_DEVICE;num++) {
    if(num) json +=", ";
    json += "\""+devices[num].dev_name+"\":"+String(devices[num].state?"\"ON\"":"\"OFF\"");
  }
  json += "}";
  server.send(200, "text/json", json);
}

void handleChange() {
  String sw_name = server.arg("name");
  String sw_state = server.arg("state");
  
  uint8_t num = 0;
  for (num = 0; num < MAX_DEVICE; num++) {
    Serial.printf(sw_name.c_str());
    if (debugMode) Serial.printf("device_name: %s, sel_no: %d, status: %d change: %d\n", devices[num].dev_name.c_str(), devices[num].sel_no, devices[num].state, devices[num].change);
    if (sw_name == devices[num].dev_name) {
      devices[num].change = true;  
      if (sw_state == "ON") {
        if (debugMode) Serial.printf("Device %s state: ON\n", sw_name.c_str());
        devices[num].state = true;
        bot.sendMessage(CHAT_ID, "Device state : ON", "");
      } else if (sw_state == "OFF") {
        if (debugMode) Serial.printf("Device %s state: OFF\n", sw_name.c_str());
        devices[num].state = false;
        bot.sendMessage(CHAT_ID, "Device state : OFF", "");
      }
    server.send(200, "text/plain", "{\""+devices[num].dev_name+"\":"+String(devices[num].state?"\"ON\"":"\"OFF\"")+"}");
    }
  } 
}

void webserverSetup() {
  if ( MDNS.begin ( MDNS_NAME ) ) {
    Serial.println ( "MDNS responder started" );
  }

  server.on ( "/", handleRoot );
  server.on ( "/check", []() {
    server.send ( 200, "text/plain", "this works as well" );
  } );
  server.on ("/poll", handlePoll);
  server.on("/change", handleChange);
  server.onNotFound ( handleNotFound );
  server.begin();
  Serial.println ( "HTTP server started" );
}

/********************************************

 * EchoMessages - function to Echo messages *

 ********************************************/
void Bot_ExecMessages() {
  
}

bool restore_status() {
  EEPROM.begin(512);
  EEPROM.get(0, save_data.status);
  EEPROM.get(0 + sizeof(bool), save_data.ok);
  if (String(save_data.ok) != String("OK"))
     save_data.status = true; //if data is invalid, keep light on
  temp_state = save_data.status;
  EEPROM.end();
  return temp_state;
}

void save_status() {
  if (temp_state != save_data.status) {
    EEPROM.begin(512);
    EEPROM.put(0, temp_state);
    if (String(save_data.ok) != String("OK")) {
      save_data.ok[0] = 'O', save_data.ok[1] = 'K', save_data.ok[2] = '\O';
      EEPROM.put(0 + sizeof(bool), "OK");
    }
    save_data.status = temp_state;
    EEPROM.end();
  }
}

void setup() {
  const rst_info * resetInfo = system_get_rst_info();
  bool restored_status = restore_status();
  
  pinMode(O_RELAY_ONE, OUTPUT);
  digitalWrite(O_RELAY_ONE, ((restored_status)?HIGH:LOW));    //restore Light status
  // Init serial port and clean garbage
  //Serial.begin(SERIAL_BAUDRATE);
  //Serial.println("After connection, ask Alexa/Echo to 'turn <devicename> on' or 'off'");

  //debug mode check
  pinMode(O_LEDALIVE, OUTPUT);
  debugMode = true;
  
  // wifi 설정
  wifiSetup();

  // webserver 설정
  webserverSetup();
  
  // 장치 추가
  addDevice(0, SWITCH_NAME, D_RELAY, O_RELAY_ONE);
  fauxmo.onMessage(callback);

  //telegram bot register
   bot.begin();      // launch Bot functionalities

   bot.sendMessage(CHAT_ID, "Hello, "+String(SWITCH_NAME)+"_FW is started%0AConnection information:%0A IP: "+WiFi.localIP().toString()+"%0A RSSI: "+String(WiFi.RSSI())+ "%0A Reset Reason: "+String(RST_REASONS[resetInfo->reason]), "");
   
   ESP.wdtDisable();
   //ESP.wdtEnable(5000);
}

//사라있네~
void aliveLedSignal() {
  if (ledPinStatus == LOW) {
    digitalWrite(O_LEDALIVE, HIGH);
    ledPinStatus = HIGH;
  } else {
    digitalWrite(O_LEDALIVE, LOW);
    ledPinStatus = LOW;
  }
  //delay(interval);
}

void loop() {
  fauxmo.handle();
  server.handleClient();
  
  for (uint8_t num = 0; num < MAX_DEVICE; num++) {
    if (devices[num].change) {
      if (debugMode) Serial.printf("device_name: %s, num: %d, status: %d change: %d\n", devices[num].dev_name.c_str(), devices[num].sel_no, devices[num].state, devices[num].change);
      if (devices[num].state) {
        Serial.println("onstate");
        digitalWrite(devices[num].sel_no, HIGH);
      } else {
        Serial.println("offstate");
        digitalWrite(devices[num].sel_no, LOW);
      }
      temp_state = devices[num].state;
      devices[num].change = false;
    }
  }
  //aliveLedSignal(LED_DELAY_INTERVAL);
  ESP.wdtFeed();
  #if 1
  if (millis() > Bot_lasttime + LED_DELAY_INTERVAL)  {
    //bot.getUpdates(bot.message[0][1]);   // launch API GetUpdates up to xxx message
    //Bot_ExecMessages();   // reply to message with Echo
    save_status();      //check if status is changed, save status.
    aliveLedSignal();   // alive led blink.
    Bot_lasttime = millis();
  }
 #endif
  
}
