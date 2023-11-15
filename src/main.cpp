// return IPAddress(0U);        ==>  return IPAddress((uint32_t)0);
// mbedtls_md5_starts(&_ctx);   ==>  mbedtls_md5_starts_ret(&_ctx);

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <Wire.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LiquidCrystal_I2C.h>

const char* ssid = "MS_Station";
const char* password = "MS_Station2115";
IPAddress staticIP(192, 168, 1, 1);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
#define SW0 9
#define SW_BACK 8
#define SW_DOWN 7
#define SW_UP 4
#define SW_OK 3
#define SDA_PIN 5
#define SCL_PIN 6

struct Data {
    bool detected;
    float batt;
    int day; // tu dodac strukture z node
    int month;
    int year;
};

TaskHandle_t task1;
LiquidCrystal_I2C lcd(0x27,2,1,0,4,5,6,7,3,POSITIVE);
AsyncWebServer server(80);
uint httpIndex=0;
Data receivedData={0,36.37,15,11,2023};
// Data receivedData={0,0.0,0,0,0};

// time control
bool APrunning=0;
bool LCDStartDone=0;
bool acquiredData=0;

// LCD
char LCDcontent[4][21] = {
  "   Oczekiwanie na   ",
  "     polaczenie     ",
  "        ...         ",
  "                    "
};
byte customCharZ[8] = {4,0,31,2,4,8,31,0}; // ż

void taskPrint(void *parameter);
void scanI2C(void *parameter);
void serverConfig(void *parameter);
void accessPointConfig(void *parameter);
void LCDCtrl(void *parameter);
void LCDStart(void *parameter);

void setup() {
  Serial.begin(115200);
  // while(!Serial);

  pinMode(SW0, INPUT_PULLUP);
  Wire.begin(SDA_PIN,SCL_PIN);

  // delay(5000);
  // xTaskCreatePinnedToCore(accessPointConfig,"accessPointConfig",4096,nullptr,1,&task1,1 );
  // xTaskCreatePinnedToCore(serverConfig,"serverConfig",2048,nullptr,1,&task1,1 );
  
  xTaskCreatePinnedToCore(LCDStart,"LCDStart",1024,nullptr,1,&task1,1 );
  xTaskCreatePinnedToCore(LCDCtrl,"LCDCtrl",1024,nullptr,1,&task1,1 );

}

void loop() {
  int pushedSwitch = digitalRead(SW0);

  if(pushedSwitch==LOW){
    Serial.println("WCISNIETO.");
    delay(100);
  }

  

}
///////////////////


// Control LCD
void LCDCtrl(void *parameter){
  for(;;){
    if(!LCDStartDone){
      taskYIELD();
    }else{
      break;
    }
  }

  for(int i=0;i<4;i++){
    if(!acquiredData){ // data not received yet
      if(i==0){
        lcd.setCursor(8,2);
        lcd.print("   ");
      }else{
        lcd.setCursor(7+i,2);
        lcd.print(".");
      }
      if(i>=3){
        i=-1;
        acquiredData=1; // TEMP
      }

      delay(1000);
    }else{
      break;
    }
  }

  
  if(receivedData.detected){
    std::strcpy(LCDcontent[0], " Wykryto list!      ");
  }else{
    std::strcpy(LCDcontent[0], " Skrzynka pusta     ");
  }
  
  String battStr="";
  if((int)receivedData.batt>=100){
    battStr = " Bateria: " + String((int)receivedData.batt) + String("%      ");
  }else if((int)receivedData.batt>=10){
    battStr = " Bateria: " + String((int)receivedData.batt) + String("%       ");
  }else{
    battStr = " Bateria: " + String((int)receivedData.batt) + String("%        ");
  }
  std::strcpy(LCDcontent[1], battStr.c_str());

  std::strcpy(LCDcontent[2], " Ostatni list:      ");

  std::strcpy(LCDcontent[3], "      14.11 12:34   ");

  lcd.clear();
  lcd.setCursor(0,0);
  // tu menu glowne
  
  for(int i=0;i<4;i++){
    lcd.setCursor(0, i);
    lcd.print(LCDcontent[i]);
  }
  

  vTaskDelete(NULL);
}

// LCD starting message
void LCDStart(void *parameter){
  // memset(LCDcontent, ' ', sizeof(LCDcontent));

  lcd.begin(20, 4);
  lcd.createChar(0, customCharZ);

  lcd.clear();
  lcd.print("Projekt in");
  lcd.write(byte(0));
  lcd.print("ynierski");

  lcd.setCursor(0,2);
  lcd.print("    MAIL SENSOR     ");
  lcd.setCursor(0,3);
  lcd.print("   Dominik Kijak    ");

  delay(2000);
  lcd.setCursor(0,0);
  for(int i=0;i<20;i++){
    for(int j=0;j<4;j++){
      lcd.setCursor(i,j);
      lcd.print(" ");
    }
    delay(20);
  }

  lcd.clear();
  for(int i=0;i<4;i++){
    lcd.setCursor(0, i);
    lcd.print(LCDcontent[i]);
  }

  LCDStartDone=1;

  vTaskDelete(NULL);
}

// Set up AP
void accessPointConfig(void *parameter) {
  WiFi.softAP(ssid, password, 1, 0, 4, 0);
  WiFi.softAPConfig(staticIP, gateway, subnet);
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);
  APrunning=1;

  vTaskDelete(NULL);
}

// Set up http server
void serverConfig(void *parameter){
  for(;;){
    if(WiFi.getMode() == WIFI_AP){
      server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "text/plain", "Hello, world!");
        Serial.print("WYSLANO NR: ");
        Serial.println(httpIndex);
        httpIndex++;
      });

      server.on("/post", HTTP_POST, [](AsyncWebServerRequest *request){
          String message;
          String message2;
          if (request->hasParam("message", true)) {
              message = request->getParam("message", true)->value();
          } else {
              message = "No message sent";
          }
          if (request->hasParam("message2", true)) {
              message2 = request->getParam("message2", true)->value();
          } else {
              message2 = "No message2 sent";
          }

          request->send(200, "text/plain", "Hello, POST: message=" + message + ", message2=" + message2);
      });

      server.onNotFound([](AsyncWebServerRequest *request){
        request->send(404, "text/plain", "Not Found");
      });

      server.begin();

      vTaskDelete(NULL);
    }else{
      Serial.println("HTTP waiting for AP...");
      taskYIELD();
    }
  }

  vTaskDelete(NULL);
}

// Serial monitor test
void taskPrint(void *parameter){
  for(int i=0;i<10;i++){
    Serial.println("taskPrint!");
    delay(1000);
  }
  vTaskDelete(NULL);
}

// Find I2C devices
void scanI2C(void *parameter){
  for(;;){
    byte error, address;
    int nDevices;
  
    Serial.println("Scanning...");
    nDevices = 0;
    for(address = 1; address < 127; address++ )
    {
      Wire.beginTransmission(address);
      error = Wire.endTransmission();
  
      if (error == 0)
      {
        Serial.print("I2C device found at address 0x");
        if (address<16)
          Serial.print("0");
        Serial.print(address,HEX);
        Serial.println("  !");
  
        nDevices++;
      }
      else if (error==4)
      {
        Serial.print("Unknown error at address 0x");
        if (address<16)
          Serial.print("0");
        Serial.println(address,HEX);
      }    
    }
    if (nDevices == 0)
      Serial.println("No I2C devices found\n");
    else
      Serial.println("done\n");
  
    delay(5000);
  }
  vTaskDelete(NULL);
}
