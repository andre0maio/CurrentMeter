#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESP8266mDNS.h>
#include <ESPAsyncWebServer.h>
#include <PubSubClient.h>
#include <PZEM004Tv30.h>
#include <LiquidCrystal_I2C.h>
#include <ArduinoOTA.h>

#include "index_page.h"
#include "style.h"

//IO SETUP
int afterboot_pin = 14;
int status_led = 2;

//PZEM Setup
bool pzem_rdy = false;
unsigned long pzem_previousMillis = 0;             
float voltage;
float current;
float power;
float energy;
float frequency;
float pf;    
PZEM004Tv30 pzem(13, 15); //RX TX

//LCD
int lcd_menuposition = 1;
int lcd_button_pin = 2;
int lcd_total_menus = 4;
bool lcd_button_status;
bool last_lcd_button_status;
bool lcd_nextmenu = false;
unsigned long lcd_previousMillis = 0;
long lcd_interval = 200;

LiquidCrystal_I2C lcd(0x27,16,2);

//WiFi Info
AsyncWebServer server(80);
const char *ssid      = "Get Of My Lan";  
const char *password  = "aaa222@@@";
IPAddress staticIP(192,168,1,110);
IPAddress gateway(192,168,1,254);
IPAddress subnet(255,255,255,0);

//MQTT Info
const char *mqtt_id       = "ESP_CurrentMeter";
const char *mqtt_server   = "192.168.1.100";
const char *mqtt_user     = "pi";
const char *mqtt_pw       = "andre2009";
const int   mqtt_port     = 1883;
bool mqtt_connected = false;
String mqtt_status;
unsigned long mqtt_interval = 1000;
unsigned long mqtt_previousMillis = 0;
int mqtt_bufferint = 0;

WiFiClient espClient;
PubSubClient client(espClient);

//OTA Hostname
const char *OTA_hostname = "ESP_CurrentMeter";
bool allow_OTA = false;
bool OTA_begin = false;
String OTA_Status;

void mqtt_callback(char* topic, byte* payload, unsigned int length) {

  //Print Payload
  #ifdef DEBUG
  Serial.print("MQTT Received: ");
  Serial.print(topic);
  Serial.print("/ ");
  for ( int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
  #endif

  //Convert payload to String
  String mqtt_buffer = "";
  for ( int i = 0; i < length; i++) {
    mqtt_buffer += (char)payload[i];   
  }

  //Convert Buffer to int 
  mqtt_bufferint = mqtt_buffer.toInt(); 

  if (strcmp(topic,"ESP_currentMeter/OTA") == 0){
    if (mqtt_buffer == "ON") {
      Serial.println("OTA ON!");
      allow_OTA = true;
      OTA_begin = true;

    } else if (mqtt_buffer == "OFF") {
      Serial.println("OTA OFF!");
      allow_OTA = false;
      OTA_begin = false;
    }
  }
  if (strcmp(topic,"ESP_currentMeter/REBOOT") == 0){
    if (mqtt_buffer == "true"){
      Serial.println("ESP Rebooting...");
      ESP.restart();
    }
  }
}
void mqtt_setup(){
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqtt_callback);
  int count = 0;
  Serial.println("Connecting to the Broker MQTT...");
  mqtt_status = "Trying to connect...";

  while (!client.connected()) {
    if (client.connect(mqtt_id, mqtt_user, mqtt_pw )) {  
      Serial.println("MQTT Ready");  
      mqtt_status = "Connected"; 
    } else {  
      Serial.print("MQTT Failed to connect..  ");
      Serial.print(client.state());
      Serial.print(" - ");
      Serial.print(count);
      Serial.println("/3");
      count++;
      delay(2000);
    }
    if (count >= 3){
      Serial.println("MQTT Failed!! Skiping this step.  ");
      mqtt_status = "Failed to connect";
      break;
    }
  }
}
void mqtt_pub(unsigned int current_value){

  unsigned long mqtt_currentMillis = millis();
  if (mqtt_currentMillis - mqtt_previousMillis >= mqtt_interval) {
    char mqtt_msg[10];
    sprintf(mqtt_msg,"%d", current_value);
    client.publish("ESP_currentMeter/currentValue",mqtt_msg);
    client.loop();
    #ifdef DEBUG
      Serial.print("MQTT sent: ");
      Serial.println(mqtt_msg);
    #endif
    mqtt_previousMillis = mqtt_currentMillis;
  }
}
void fetch_pzemvalues(unsigned long interval){
  unsigned long pzem_currentMillis = millis();
  if (pzem_currentMillis - pzem_previousMillis >= interval) {
    pzem_previousMillis = pzem_currentMillis;
    
    voltage   = pzem.voltage();
    current   = pzem.current();
    power     = pzem.power();
    energy    = pzem.energy();
    frequency = pzem.frequency();
    pf        = pzem.pf();

    if (isnan(voltage)){
      voltage   = 0;
      current   = 0;
      power     = 0;
      energy    = 0;
      frequency = 0;
      pf        = 0;
    }
  }
}
void lcd_menu_update(){
      unsigned long currentMillis = millis();
      if (currentMillis - lcd_previousMillis >= lcd_interval) {
        lcd_previousMillis = currentMillis;
        if (lcd_nextmenu){
        lcd_menuposition++;
        lcd.clear();
      }
      if (lcd_menuposition >= lcd_total_menus){
        lcd_menuposition = 1;
      }
      lcd_nextmenu = false;
      switch (lcd_menuposition){
      case 1:
          //LCD Clear 
          lcd.setCursor(3,0);
          lcd.print("    ");
          lcd.setCursor(3,1);
          lcd.print("    ");
          lcd.setCursor(11,0);
          lcd.print("    ");
          lcd.setCursor(11,1);
          lcd.print("    ");

          //Print values
          lcd.setCursor(0,0);
          lcd.print("V: ");
          lcd.print(int(voltage));

          lcd.setCursor(0,1);
          lcd.print("A: ");
          lcd.print(current);

          lcd.setCursor(8,0);
          lcd.print("P: ");
          lcd.print(power);

          lcd.setCursor(8,1);
          lcd.print("E: ");
          lcd.print(energy);
        break;
      case 2:
           //LCD Clear 
          lcd.setCursor(4,0);
          lcd.print("     ");
          lcd.setCursor(4,1);
          lcd.print("     ");

          lcd.setCursor(0,0);
          lcd.print(" F: ");
          lcd.print(frequency);

          lcd.setCursor(0,1);
          lcd.print("PF: ");
          lcd.print(pf);
        break;
      case 3:  
          //LCD Clear 
          lcd.setCursor(5,0);
          lcd.print("   ");
          lcd.setCursor(5,1);
          lcd.print("   ");
          lcd.setCursor(13,0);
          lcd.print("   ");
          lcd.setCursor(13,1);
          lcd.print("   ");
          //Print Var
          lcd.setCursor(0,0); 
          lcd.print("Wifi:");
          if (WiFi.status() == WL_CONNECTED){
            lcd.print("ON");
          } else {
            lcd.print("OFF");
          }

          lcd.setCursor(0,1);
          lcd.print("MQTT:");
          if (client.connected()){
            lcd.print("ON");
          } else {
            lcd.print("OFF");
          }
          lcd.setCursor(8,0); 
          lcd.print(" OTA:");
          if (OTA_Status == "Ready"){
            lcd.print("ON");
          } else if ( OTA_Status == "OFF") {
            lcd.print("OFF");
          } 
          lcd.setCursor(8,1);
          lcd.print("PZEM:");
          if (voltage){
            lcd.print("ON");
          } else {
            lcd.print("OFF");
          }
        break;
      default:
          lcd.clear();
          lcd.print("Erro carregar menu...");
        break;
    }  
  }
    
}
String processor(const String& var){

  if (var == "VOLTAGE") {
      return String(voltage);
  }
  else if(var == "CURRENT"){
    return String(current);
  }
  else if(var == "POWER"){
    return String(power);
  }
  else if(var == "ENERGY"){
    return String(energy);
  }
  else if(var == "FREQUENCY"){
    return String(frequency);
  }
  else if(var == "PF"){
    return String(pf);
  } 
  return String();
}

bool wifi_setup(){

  lcd.setCursor(0,1);
  lcd.print("Wifi setup...");

  WiFi.mode(WIFI_STA);
  WiFi.config(staticIP, gateway, subnet);
  WiFi.begin(ssid, password);
  int count = 0;
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print("Connecting to WiFi...  ");
    Serial.print(count);
    Serial.println("/10");
    delay(500);
    count++;
    if (count >= 10){
      Serial.println("Wifi failed to connect! Reboot to reconnect.");
      break;
    }
  }
  if (WiFi.status() == WL_CONNECTED){
    Serial.println("Connected to the network!");
    Serial.print("STA IP Address: ");
    Serial.println(WiFi.localIP());

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send_P(200, "text/html", index_html, processor);
    });
    server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send_P(200, "text/css", index_css);
    });
    server.on("/VOLTAGE", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send_P(200, "text/plain",String(voltage).c_str());
    });
    server.on("/CURRENT", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send_P(200, "text/plain",String(current).c_str());
    });
    server.on("/POWER", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send_P(200, "text/plain",String(power).c_str());
    });
    server.on("/ENERGY", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send_P(200, "text/plain",String(energy).c_str());
    });
    server.on("/FREQUENCY", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send_P(200, "text/plain",String(frequency).c_str());
    });
    server.on("/PF", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send_P(200, "text/plain",String(pf).c_str());
    });
    server.on("/OTA_Status", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send_P(200, "text/plain",OTA_Status.c_str());
    });
    server.on("/MQTT_Status", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send_P(200, "text/plain",mqtt_status.c_str());
    });
    server.on("/ESP_REBOOT", HTTP_GET, [](AsyncWebServerRequest *request){
      ESP.restart();
      request->send_P(200, "text/plain","");
    });
    server.on("/reset_energia", HTTP_GET, [](AsyncWebServerRequest *request){
      pzem.resetEnergy();
      request->send_P(200, "text/plain","");
    });
    return true;
  }
  return false;
   
}
void OTA_setup(){

  ArduinoOTA.setHostname(OTA_hostname);
  client.publish("ESP_currentMeter/MQTT_Log", "OTA SETUP");
  ArduinoOTA.onStart([]() {
    Serial.println("Start");
    client.publish("ESP_currentMeter/MQTT_Log", "START");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
    client.publish("ESP_currentMeter/MQTT_Log", "EEND");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    String p = "Progress: " + String((progress / (total / 100)));
    client.publish("ESP_currentMeter/MQTT_Log", p.c_str());
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR){
      Serial.println("Auth Failed"); 
      client.publish("ESP_currentMeter/MQTT_Log", "Auth Failed");
      }
    else if (error == OTA_BEGIN_ERROR){
      Serial.println("Begin Failed"); 
      client.publish("ESP_currentMeter/MQTT_Log", "Begin Failed");
      }
    else if (error == OTA_CONNECT_ERROR){ 
      Serial.println("Connect Failed");
      client.publish("ESP_currentMeter/MQTT_Log", "Connect Failed");
      }
    else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
      client.publish("ESP_currentMeter/MQTT_Log", "Receive Failed");
      }
    else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
      client.publish("ESP_currentMeter/MQTT_Log", "End Failed");
      }
  });
  ArduinoOTA.begin();
  Serial.println("OTA Ready!");
  
  client.publish("ESP_currentMeter/MQTT_Log", "OTA SETUP Comleted");
}
void read_io(){

  //LCD Button
  lcd_button_status = digitalRead(lcd_button_pin);
  if (lcd_button_status != last_lcd_button_status){
    if (lcd_button_status){
      lcd_nextmenu = true;
    }
  }
  last_lcd_button_status = lcd_button_status;

}
void setup() {

  Serial.begin(115200);
  Serial.println("ESP_CurrentMeter On!");
  pinMode(afterboot_pin, OUTPUT);
  pinMode(status_led, OUTPUT);
  pinMode(lcd_button_pin, INPUT);
  digitalWrite(status_led, LOW);

  lcd.init(); 
  lcd.setCursor(0,0);
  lcd.print("ESP_CurrentMeter");

  if (wifi_setup()){
    lcd.setCursor(0,1);
    lcd.print("                ");
    lcd.setCursor(0,1);
    lcd.print("MQTT/OTA Setup...");

    mqtt_setup();
    server.begin();
    client.subscribe("ESP_currentMeter/OTA",1);
    client.subscribe("ESP_currentMeter/REBOOT",1);
  } else {
    Serial.println("MQTT & OTA Disabled.");
  }

  delay(500);
  digitalWrite(status_led, HIGH);
  digitalWrite(afterboot_pin, HIGH);
  Serial.println("____________ESP Ready!____________");
  lcd.clear();
}

void loop() {

  //Allow OTA Updates
  if (allow_OTA){
    OTA_Status = "Ready";
    if (OTA_begin){
      OTA_setup();
      OTA_begin = false;
    }
    ArduinoOTA.handle();
  } else {
    OTA_Status = "OFF";
  }

  //Check MQTT
  if (!client.connected()){
    mqtt_status = "Not connected";
  }

  //Fetch Data
  fetch_pzemvalues(1000); //Interval

  client.loop();

  read_io();
  lcd_menu_update();
}


//add to webpage OTA/Price/
//Check PZEM STATUS 
//ADD RECONNECT MQTT
//Lower LED Bright
