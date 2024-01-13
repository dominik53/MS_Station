/*
  Dominik Kijak, 2024r.
*/

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <Wire.h>
#include <SPI.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LiquidCrystal_I2C.h>
#include "Adafruit_FRAM_I2C.h"

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

#define MIN_CONNECT_TIME 10 // seconds
#define MIN_WAKE_INTERVAL 300 // seconds
const float detectPrecision = 0.965; // 0 - 1.0

struct Data {
    bool detected;
    float distance;
    float batt;
    int day;
    int month;
    int year;
    int hours;
    int minutes;
    int wakeInterval; // seconds
    int connectTime;  // seconds
};

struct UpdateData {
    int wakeInterval; // seconds
    int connectTime;  // seconds
    bool calibrate;
};

struct TimeDate {
    int milliseconds;
    int seconds;
    int minutes;
    int hours;
    int day;
    int month;
    int year;
};

TaskHandle_t task1;
LiquidCrystal_I2C lcd(0x27,2,1,0,4,5,6,7,3,POSITIVE);
Adafruit_FRAM_I2C fram = Adafruit_FRAM_I2C();
AsyncWebServer server(80);
Data receivedData={0,0.0,0,MIN_WAKE_INTERVAL,MIN_CONNECT_TIME};
UpdateData sendData={-1,-1,0}; // -1 = do not update
TimeDate currentTime={0,0,0,0,1,1,2024};
unsigned long startTime=0;
unsigned long lastTime=0;
float ToFCalibratedDist=0; // mm

// time control
bool APrunning=0;
bool acquiredData=0;
bool acquiredDataPart[2]={0,0};

// changeable variables in screens: 4,5,6
int screenPointer4=0;
int screenVariables4[5]={1,1,2023,0,0};
int screenPointer5=0;
int screenVariables5[3]={0,6,0};
int screenPointer6=0;
int screenVariables6=10;
bool updateRequest=0;

// LCD
int currentScreen=0;
int menuPointer=0;
int menuFirstRow=0;
uint8_t customCharZ[8] = {4,0,31,2,4,8,31,0}; // ż
uint8_t customCharL[8] = {12,4,6,4,12,4,14,0}; // ł
uint8_t customCharA[8] = {0,14,1,15,17,15,2,1}; // ą
uint8_t customCharPointer[8] = {0,4,2,31,2,4,0,0}; // menu pointer
uint8_t customCharC[8] = {2,4,14,16,16,17,14,0}; // ć
uint8_t customCharArrowUp[8] = {4,14,21,4,4,4,0,0}; // arrow up
uint8_t customCharArrowDown[8] = {0,0,4,4,4,21,14,4}; // arrow down
        
void serverConfig(void *parameter);         // configure HTTP server
void accessPointConfig(void *parameter);    // configure and start AP
void displayScreen(int screenIndex);        // LCD handler
void calculateTime(void *parameter);        // control updating time related values
void FRAMRead(void *parameter);             // read values from FRAM

void setup() {
  Serial.begin(115200);
  // while(!Serial);

  pinMode(SW0, INPUT_PULLUP);
  pinMode(SW_BACK, INPUT_PULLUP);
  pinMode(SW_DOWN, INPUT_PULLUP);
  pinMode(SW_UP, INPUT_PULLUP);
  pinMode(SW_OK, INPUT_PULLUP);

  Wire.begin(SDA_PIN,SCL_PIN);
  lcd.begin(20, 4);
  lcd.createChar(0, customCharZ); // location 7 reserved 
  lcd.createChar(1, customCharL);
  lcd.createChar(2, customCharA);
  lcd.createChar(3, customCharPointer);
  lcd.createChar(4, customCharC);
  lcd.createChar(5, customCharArrowDown);
  lcd.createChar(6, customCharArrowUp);

  xTaskCreatePinnedToCore(FRAMRead,"FRAMRead",2048,nullptr,1,&task1,1 );
  xTaskCreatePinnedToCore(accessPointConfig,"accessPointConfig",4096,nullptr,1,&task1,1 );
  xTaskCreatePinnedToCore(serverConfig,"serverConfig",2048,nullptr,1,&task1,1 );
  xTaskCreatePinnedToCore(calculateTime,"calculateTime",8192,nullptr,1,&task1,1 );

  displayScreen(currentScreen++); // welcome
  displayScreen(currentScreen++); // await for data
  displayScreen(currentScreen);   // info
}

void loop() {
  if(digitalRead(SW0)==LOW){ // only for presentation
    delay(50);

    sendData.wakeInterval=5;
    receivedData.wakeInterval=5;
    updateRequest=1;

    if(currentScreen==8){
      displayScreen(currentScreen);
    }
    
    while (digitalRead(SW0) == LOW) {
      delay(50);
    }
  }


  if(digitalRead(SW_OK)==LOW){
    delay(50);

    switch(currentScreen){
      case 2:{
        displayScreen(++currentScreen);
        break;
      }
      case 3:{
        if(menuPointer==0){
          currentScreen=4;
        }else if(menuPointer==1){
          currentScreen=5;
        }else if(menuPointer==3){
          currentScreen=6;
        }else if(menuPointer==5){
          currentScreen=7;
        }
        displayScreen(currentScreen);
        break;
      }
      case 4:{
        if(screenPointer4<5){
          screenPointer4++;
          displayScreen(currentScreen);
        }else{
          currentTime.day=screenVariables4[0];
          currentTime.month=screenVariables4[1];
          currentTime.year=screenVariables4[2];
          currentTime.hours=screenVariables4[3];
          currentTime.minutes=screenVariables4[4];
          currentTime.seconds=0;
          currentTime.milliseconds=0;

          fram.write(0x01, currentTime.month);
          fram.write(0x02, currentTime.day);
          uint8_t tempBuffer1[2];
          tempBuffer1[0] = currentTime.year & 0xFF;
          tempBuffer1[1] = (currentTime.year >> 8) & 0xFF;
          fram.write(0x03, tempBuffer1, 2);

          fram.write(0x05, currentTime.hours);
          fram.write(0x06, currentTime.minutes);
          fram.write(0x07, currentTime.seconds);

          screenPointer4=0;
          currentScreen=2;
          displayScreen(currentScreen);
        }

        break;
      }
      case 5:{
        if(screenPointer5<3){
          screenPointer5++;
          displayScreen(currentScreen);
        }else{
          sendData.wakeInterval=60*(screenVariables5[0]+60*(screenVariables5[1]+screenVariables5[2]*24));
          receivedData.wakeInterval=sendData.wakeInterval;
          updateRequest=1;

          screenPointer5=0;
          currentScreen=2;
          displayScreen(currentScreen);
        }

        break;
      }
      case 6:{
        if(screenPointer6<1){
          screenPointer6++;
          displayScreen(currentScreen);
        }else{
          sendData.connectTime=screenVariables6;
          receivedData.connectTime=sendData.connectTime;
          updateRequest=1;

          screenPointer6=0;
          currentScreen=2;
          displayScreen(currentScreen);
        }

        break;
      }
      case 7:{
        sendData.calibrate=1;
        updateRequest=1;

        currentScreen=2;
        displayScreen(currentScreen);
        break;
      }
      case 8:{
        currentScreen=3;
        displayScreen(currentScreen);
        break;
      }
      
      default:
          break;
    }


    while (digitalRead(SW_OK) == LOW) {
      delay(50);
    }
  }

  if(digitalRead(SW_BACK)==LOW){
    delay(50);

    switch(currentScreen){
      case 3:{
        currentScreen=2;
        displayScreen(currentScreen);
        break;
      }
      case 4:{
        if(screenPointer4==0){
          currentScreen=3;
          displayScreen(currentScreen);
        }else{
          screenPointer4--;
          displayScreen(currentScreen);
        }
        break;
      }
      case 5:{
        if(screenPointer5==0){
          currentScreen=3;
          displayScreen(currentScreen);
        }else{
          screenPointer5--;
          displayScreen(currentScreen);
        }
        break;
      }
      case 6:{
        if(screenPointer6==0){
          currentScreen=3;
          displayScreen(currentScreen);
        }else{
          screenPointer6--;
          displayScreen(currentScreen);
        }
        break;
      }
      case 7:{
        currentScreen=3;
        displayScreen(currentScreen);
        break;
      }
      
      default:
          break;
    }

    while (digitalRead(SW_BACK) == LOW) {
      delay(50);
    }
  }

  if(digitalRead(SW_DOWN)==LOW){
    delay(50);
    
    switch(currentScreen){
      case 2:{
        currentScreen=8;
        displayScreen(currentScreen);
        
        break;
      }
      case 3:{
        if(menuPointer<5){
          if(menuPointer==1 || menuPointer==3){
            menuPointer+=2;
          }else{
            menuPointer++;
          }
          displayScreen(currentScreen);
        }
        
        break;
      }
      case 4:{
        if(screenPointer4==0){
          if(screenVariables4[screenPointer4]>1){
            screenVariables4[screenPointer4]--;
          }else{
            screenVariables4[screenPointer4]=31;
          }
        }else if(screenPointer4==1){
          if(screenVariables4[screenPointer4]>1){
            screenVariables4[screenPointer4]--;
          }else{
            screenVariables4[screenPointer4]=12;
          }
        }else if(screenPointer4==2){
          if(screenVariables4[screenPointer4]>2000){
            screenVariables4[screenPointer4]--;
          }else{
            screenVariables4[screenPointer4]=2100;
          }
        }else if(screenPointer4==3){
          if(screenVariables4[screenPointer4]>0){
            screenVariables4[screenPointer4]--;
          }else{
            screenVariables4[screenPointer4]=23;
          }
        }else if(screenPointer4==4){
          if(screenVariables4[screenPointer4]>0){
            screenVariables4[screenPointer4]--;
          }else{
            screenVariables4[screenPointer4]=59;
          }
        }
        if(screenPointer4<5) displayScreen(currentScreen);
        
        break;
      }
      case 5:{
        if(screenPointer5==0){
          if(screenVariables5[screenPointer5]>0){
            screenVariables5[screenPointer5]--;
          }else{
            screenVariables5[screenPointer5]=59;
          }
        }else if(screenPointer5==1){
          if(screenVariables5[screenPointer5]>0){
            screenVariables5[screenPointer5]--;
          }else{
            screenVariables5[screenPointer5]=23;
          }
        }else if(screenPointer5==2){
          if(screenVariables5[screenPointer5]>0){
            screenVariables5[screenPointer5]--;
          }else{
            screenVariables5[screenPointer5]=7;
          }
        }
        if(screenPointer5<3) displayScreen(currentScreen);
        break;
      }
      case 6:{
        if(screenPointer6==0){
          if(screenVariables6>0){
            screenVariables6--;
          }else{
            screenVariables6=60;
          }
          displayScreen(currentScreen);
        }
        break;
      }
      
      default:
          break;
    }
    
    while (digitalRead(SW_DOWN) == LOW) {
      delay(50);
    }
  }

  if(digitalRead(SW_UP)==LOW){
    delay(50);
    
    switch(currentScreen){
      case 3:{
        if(menuPointer>0){
          if(menuPointer==3 || menuPointer==5){
            menuPointer-=2;
          }else{
            menuPointer--;
          }
          displayScreen(currentScreen);
        }
        
        break;
      }
      case 4:{
        if(screenPointer4==0){
          if(screenVariables4[screenPointer4]<31){
            screenVariables4[screenPointer4]++;
          }else{
            screenVariables4[screenPointer4]=1;
          }
        }else if(screenPointer4==1){
          if(screenVariables4[screenPointer4]<12){
            screenVariables4[screenPointer4]++;
          }else{
            screenVariables4[screenPointer4]=1;
          }
        }else if(screenPointer4==2){
          if(screenVariables4[screenPointer4]<2100){
            screenVariables4[screenPointer4]++;
          }else{
            screenVariables4[screenPointer4]=2000;
          }
        }else if(screenPointer4==3){
          if(screenVariables4[screenPointer4]<23){
            screenVariables4[screenPointer4]++;
          }else{
            screenVariables4[screenPointer4]=0;
          }
        }else if(screenPointer4==4){
          if(screenVariables4[screenPointer4]<59){
            screenVariables4[screenPointer4]++;
          }else{
            screenVariables4[screenPointer4]=0;
          }
        }

        if(screenPointer4<5) displayScreen(currentScreen);

        break;
      }
      case 5:{
        if(screenPointer5==0){
          if(screenVariables5[screenPointer5]<59){
            screenVariables5[screenPointer5]++;
          }else{
            screenVariables5[screenPointer5]=0;
          }
        }else if(screenPointer5==1){
          if(screenVariables5[screenPointer5]<23){
            screenVariables5[screenPointer5]++;
          }else{
            screenVariables5[screenPointer5]=0;
          }
        }else if(screenPointer5==2){
          if(screenVariables5[screenPointer5]<7){
            screenVariables5[screenPointer5]++;
          }else{
            screenVariables5[screenPointer5]=0;
          }
        }
        if(screenPointer5<3) displayScreen(currentScreen);
        break;
      }
      case 6:{
        if(screenPointer6==0){
          if(screenVariables6<60){
            screenVariables6++;
          }else{
            screenVariables6=0;
          }
          displayScreen(currentScreen);
        }
        break;
      }
      case 8:{
        currentScreen=2;
        displayScreen(currentScreen);
        
        break;
      }
      
      default:
          break;
    }
    
    while (digitalRead(SW_UP) == LOW) {
      delay(50);
    }
  }  

}
///////////////////


void calculateTime(void *parameter){
  lastTime=0;
  startTime=millis();

  bool changeSec=0;
  bool changeMin=0;
  bool changeHour=0;
  bool changeDay=0;
  bool changeMonth=0;
  bool changeYear=0;

  for(;;){
    bool changeHappened=0;
    unsigned long checkTime=millis();
    unsigned long elapsedTime=0;
    if(checkTime<lastTime){
      elapsedTime=checkTime;
    }else{
      elapsedTime=checkTime-lastTime;
    }

    currentTime.milliseconds+=elapsedTime;
    while(currentTime.milliseconds>1000){
      changeSec=1;
      currentTime.seconds+=1;
      currentTime.milliseconds-=1000;
      if(currentTime.seconds>60){
        changeMin=1;
        currentTime.minutes+=1;
        currentTime.seconds-=60;
        if(currentTime.minutes>60){
          changeHour=1;
          changeHappened=1;
          currentTime.hours+=1;
          currentTime.minutes-=60;
          if(currentTime.hours>24){
            changeDay=1;
            currentTime.day+=1;
            currentTime.hours-=24;
            if(currentTime.day==31 && (currentTime.month==1 || currentTime.month==3 || currentTime.month==5 || currentTime.month==7 || currentTime.month==8 || currentTime.month==10 || currentTime.month==12)){
              changeMonth=1;
              currentTime.day=1;
              currentTime.month+=1;
            }else if(currentTime.day==30 && (currentTime.month==4 || currentTime.month==6 || currentTime.month==9 || currentTime.month==11)){
              changeMonth=1;
              currentTime.day=1;
              currentTime.month+=1;
            }else if(currentTime.day==28 && currentTime.month==2){
              changeMonth=1;
              currentTime.day=1;
              currentTime.month+=1;
            }
            if(currentTime.month>12){
              changeYear=1;
              currentTime.year+=1;
              currentTime.month=1;
            }
          }
        }
      }
    }

    if(changeSec){
      fram.write(0x07, currentTime.seconds);
      if(changeMin){
        fram.write(0x06, currentTime.minutes);
        if(changeHour){
          fram.write(0x05, currentTime.hours);
          if(changeDay){
            fram.write(0x02, currentTime.day);
            if(changeMonth){
              fram.write(0x01, currentTime.month);
              if(changeYear){
                uint8_t tempBuffer[2];
                tempBuffer[0] = currentTime.year & 0xFF;
                tempBuffer[1] = (currentTime.year >> 8) & 0xFF;
                fram.write(0x03, tempBuffer, 2);
              }
            }
          }
        }
      }
    }

    if(changeHappened && currentScreen==8){
      displayScreen(currentScreen);
    }

    lastTime=checkTime;
    delay(1000);
  }

  vTaskDelete(NULL);
}

void displayScreen(int screenIndex){
  lcd.clear();
  lcd.noBlink();
  lcd.setCursor(0, 0);
  
  switch (screenIndex) {
    case 0: // title screen
    {
      char LCDcontent[4][21] = {
        "Projekt inzynierski ",
        "                    ",
        "    MAIL SENSOR     ",
        "   Dominik Kijak    "
      };

      for(int i=0;i<4;i++){
        for(int j=0;j<20;j++){
          if(i==0 && j==10){ // ż
            lcd.write(byte(0));
          }else{
            lcd.print(LCDcontent[i][j]);
          }
        }
      }

      delay(2000);
      lcd.setCursor(0,0);
      for(int i=0;i<20;i++){
        for(int j=0;j<4;j++){
          lcd.setCursor(i,j);
          lcd.print(" ");
        }
        delay(20);
      }

      break;
    }
    case 1: // awaiting data screen
    {
      char LCDcontent[4][21] = {
        "   Oczekiwanie na   ",
        "     polaczenie     ",
        "        ...         ",
        "                    "
      };
      
      for(int i=0;i<4;i++){
        lcd.setCursor(0, i);
        for(int j=0;j<20;j++){
          if(i==1 && j==7){ // ł
            lcd.write(byte(1));
          }else if(i==1 && j==8){
            lcd.write(byte(2)); // ą
          }else{
            lcd.print(LCDcontent[i][j]);
          }
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
          }

          delay(1000);
        }else{
          break;
        }
      }

      break;
    }
    case 2: // info screen 1
    {
      char LCDcontent[4][21];

      if(receivedData.detected){
        std::strcpy(LCDcontent[0], " Wykryto list!      ");
      }else{
        std::strcpy(LCDcontent[0], " Skrzynka pusta     ");
      }

      String battStr="";
      if((int)receivedData.batt>=100){
        receivedData.batt=100.0;
        battStr = " Bateria: " + String((int)receivedData.batt) + String("%      ");
      }else if((int)receivedData.batt>=10){
        battStr = " Bateria: " + String((int)receivedData.batt) + String("%       ");
      }else{
        battStr = " Bateria: " + String((int)receivedData.batt) + String("%        ");
      }
      std::strcpy(LCDcontent[1], battStr.c_str());
      std::strcpy(LCDcontent[2], " Ostatni kontakt:   ");

      String dateStr="    ";
    
      if((int)receivedData.day>9){
        dateStr += String((int)receivedData.day) + ".";
      }else{
        dateStr += "0" + String((int)receivedData.day) + ".";
      }

      if((int)receivedData.month>9){
        dateStr += String((int)receivedData.month) + ".";
      }else{
        dateStr += "0" + String((int)receivedData.month) + ".";
      }
      if((int)receivedData.year>999){
        dateStr += String((int)receivedData.year) + " ";
      }else{
        dateStr += "x     ";
      }

      if((int)receivedData.hours>9){
        dateStr += String((int)receivedData.hours) + ":";
      }else{
        dateStr += "0" + String((int)receivedData.hours) + ":";
      }
      
      if((int)receivedData.minutes>9){
        dateStr += String((int)receivedData.minutes);
      }else{
        dateStr += "0" + String((int)receivedData.minutes);
      }
      
      std::strcpy(LCDcontent[3], dateStr.c_str());
      
      for(int i=0;i<4;i++){
        lcd.setCursor(0, i);
        for(int j=0;j<20;j++){
          if(i==3 && j==0){ // arrow down
            lcd.write(byte(5));
          }else{
            lcd.print(LCDcontent[i][j]);
          }
        }
      }

      break;
    }
    case 3: // menu screen
    {
      char LCDcontent[6][21] = {
        " 1. Data i godzina  ",
        " 2. Interwal ...    ",
        "          wykrywania",
        " 3. Czas na  ...    ",
        "          polaczenie",
        " 4. Kalibracja      "
      };


      if(menuPointer>=menuFirstRow+4){ // if pointer above screen
        menuFirstRow++;
      }else if(menuPointer<menuFirstRow){ // if under
        menuFirstRow--;
      }

      if(menuPointer==3 && menuFirstRow==0){
        menuFirstRow++;
      }

      for(int i=menuFirstRow;i<menuFirstRow+4;i++){
        lcd.setCursor(0, i-menuFirstRow);
        for(int j=0;j<20;j++){
          if(menuPointer==i && j==0){
            lcd.write(byte(3));
          }else if((i==1 && j==11)||(i==4 && j==12)){ // ł
            lcd.write(byte(1));
          }else if(i==4 && j==13){ // ą
            lcd.write(byte(2));
          }else{
            lcd.print(LCDcontent[i][j]);
          }
        }
      }
      
      break;
    }
    case 4: // edit time
    {
      char LCDcontent[4][21];
        // "  Data i godzina:   ",
        // "  xx.xx.xxxx xx:xx  ",
        // "                    ",
        // "         OK         "
      
      std::strcpy(LCDcontent[0], "  Data i godzina:   ");

      String tempVariable="  ";
      if(screenVariables4[0]>9){
        tempVariable += String(screenVariables4[0]) + ".";
      }else{
        tempVariable += "0" + String(screenVariables4[0]) + ".";
      }
      if(screenVariables4[1]>9){
        tempVariable += String(screenVariables4[1]) + ".";
      }else{
        tempVariable += "0" + String(screenVariables4[1]) + ".";
      }
      if(screenVariables4[2]>999 && screenVariables4[2]<10000){
        tempVariable += String(screenVariables4[2]) + " ";
      }else{
        tempVariable += "XXXX ";
      }
      if(screenVariables4[3]>9){
        tempVariable += String(screenVariables4[3]) + ":";
      }else{
        tempVariable += "0" + String(screenVariables4[3]) + ":";
      }
      if(screenVariables4[4]>9){
        tempVariable += String(screenVariables4[4]) + "  ";
      }else{
        tempVariable += "0" + String(screenVariables4[4]) + "  ";
      }
      std::strcpy(LCDcontent[1], tempVariable.c_str());
      std::strcpy(LCDcontent[2], "                    ");
      std::strcpy(LCDcontent[3], "         OK         ");

      for(int i=0;i<4;i++){
        lcd.setCursor(0, i);
        for(int j=0;j<20;j++){
          if(i==3 && j==8){
            lcd.write(byte(3));
          }else{
            lcd.print(LCDcontent[i][j]);
          }
        }
      }

      if(screenPointer4==0){
        lcd.setCursor(3,1);
      }else if(screenPointer4==1){
        lcd.setCursor(6,1);
      }else if(screenPointer4==2){
        lcd.setCursor(11,1);
      }else if(screenPointer4==3){
        lcd.setCursor(14,1);
      }else if(screenPointer4==4){
        lcd.setCursor(17,1);
      }else if(screenPointer4==5){
        lcd.setCursor(8,3);
      }
      lcd.blink();

      break;
    }
    case 5: // edit wake up time
    {
      char LCDcontent[4][21];
        // "     Interwal:      ",
        // " minuty:godziny:dni ",
        // "      xx:xx:xx      ",
        // "         OK         "
      
      std::strcpy(LCDcontent[0], "     Interwal:      ");
      std::strcpy(LCDcontent[1], "[minuty:godziny:dni]");

      String tempVariable="      ";
      if(screenVariables5[0]>9){
        tempVariable += String(screenVariables5[0]) + ":";
      }else{
        tempVariable += "0" + String(screenVariables5[0]) + ":";
      }
      if(screenVariables5[1]>9){
        tempVariable += String(screenVariables5[1]) + ":";
      }else{
        tempVariable += "0" + String(screenVariables5[1]) + ":";
      }
      if(screenVariables5[2]>9){
        tempVariable += String(screenVariables5[2]) + "      ";
      }else{
        tempVariable += "0" + String(screenVariables5[2]) + "      ";
      }
      std::strcpy(LCDcontent[2], tempVariable.c_str());
      std::strcpy(LCDcontent[3], "         OK         ");

      for(int i=0;i<4;i++){
        lcd.setCursor(0, i);
        for(int j=0;j<20;j++){
          if(i==0 && j==12){ // ł
            lcd.write(byte(1));
          }else if(i==3 && j==8){
            lcd.write(byte(3));
          }else{
            lcd.print(LCDcontent[i][j]);
          }
        }
      }

      if(screenPointer5==0){
        lcd.setCursor(7,2);
      }else if(screenPointer5==1){
        lcd.setCursor(10,2);
      }else if(screenPointer5==2){
        lcd.setCursor(13,2);
      }else if(screenPointer5==3){
        lcd.setCursor(8,3);
      }
      lcd.blink();

      break;
    }
    case 6: // edit connection time
    {
      char LCDcontent[4][21];
        // "Czas na polaczenie: ",
        // "     [sekundy]      ",
        // "         xx         ",
        // "         OK         "
      
      std::strcpy(LCDcontent[0], "Czas na polaczenie: ");
      std::strcpy(LCDcontent[1], "     [sekundy]      ");

      String tempVariable="         ";
      if(screenVariables6>9){
        tempVariable += String(screenVariables6) + "         ";
      }else{
        tempVariable += "0" + String(screenVariables6) + "         ";
      }
      std::strcpy(LCDcontent[2], tempVariable.c_str());
      std::strcpy(LCDcontent[3], "         OK         ");

      for(int i=0;i<4;i++){
        lcd.setCursor(0, i);
        for(int j=0;j<20;j++){
          if(i==0 && j==10){ // ł
            lcd.write(byte(1));
          }else if(i==0 && j==11){ // ą
            lcd.write(byte(2));
          }else if(i==3 && j==8){
            lcd.write(byte(3));
          }else{
            lcd.print(LCDcontent[i][j]);
          }
        }
      }

      if(screenPointer6==0){
        lcd.setCursor(10,2);
      }else if(screenPointer6==1){
        lcd.setCursor(8,3);
      }
      lcd.blink();

      break;
    }
    case 7: // calibrate
    {
      char LCDcontent[4][21]={
        " Czy chcesz dokonac ",
        "    kalibracji?     ",
        "                    ",
        "         OK         "
      };

      for(int i=0;i<4;i++){
        lcd.setCursor(0, i);
        for(int j=0;j<20;j++){
          if(i==0 && j==18){ // ć
            lcd.write(byte(4));
          }else if(i==3 && j==8){
            lcd.write(byte(3));
          }else{
            lcd.print(LCDcontent[i][j]);
          }
        }
      }

      lcd.setCursor(8,3);
      lcd.blink();

      break;
    }
    case 8: // info
    {
      char LCDcontent[4][21];
        // " Wybudzanie: xxxxx m",
        // " Polaczenie: xx     ",
        // " Aktualna data:     ",
        // "    xx.xx.xxxx xx:xx"

      String tempVariable1=" Wybudzanie: ";
      
      if(receivedData.wakeInterval>9999){
        tempVariable1 += String(receivedData.wakeInterval) + " s";
      }else if(receivedData.wakeInterval>999){
        tempVariable1 += "0" + String(receivedData.wakeInterval) + " s";
      }else if(receivedData.wakeInterval>99){
        tempVariable1 += "00" + String(receivedData.wakeInterval) + " s";
      }else if(receivedData.wakeInterval>9){
        tempVariable1 += "000" + String(receivedData.wakeInterval) + " s";
      }else{
        tempVariable1 += "0000" + String(receivedData.wakeInterval) + " s";
      }
      std::strcpy(LCDcontent[0], tempVariable1.c_str());


      String tempVariable2=" Polaczenie: ";
      if(receivedData.connectTime>9){
        tempVariable2 += String(receivedData.connectTime) + "    s";
      }else{
        tempVariable2 += "0" + String(receivedData.connectTime) + "    s";
      }
      std::strcpy(LCDcontent[1], tempVariable2.c_str());


      std::strcpy(LCDcontent[2], " Aktualna data:     ");
      String tempVariable="    ";
      if(currentTime.day>9){
        tempVariable += String(currentTime.day) + ".";
      }else{
        tempVariable += "0" + String(currentTime.day) + ".";
      }
      if(currentTime.month>9){
        tempVariable += String(currentTime.month) + ".";
      }else{
        tempVariable += "0" + String(currentTime.month) + ".";
      }
      if(currentTime.year>999){
        tempVariable += String(currentTime.year) + " ";
      }else{
        tempVariable += "xxxx ";
      }
      if(currentTime.hours>9){
        tempVariable += String(currentTime.hours) + ":";
      }else{
        tempVariable += "0" + String(currentTime.hours) + ":";
      }
      if(currentTime.minutes>9){
        tempVariable += String(currentTime.minutes);
      }else{
        tempVariable += "0" + String(currentTime.minutes);
      }
      std::strcpy(LCDcontent[3], tempVariable.c_str());

      for(int i=0;i<4;i++){
        lcd.setCursor(0, i);
        for(int j=0;j<20;j++){
          if(i==1 && j==3){ // ł
            lcd.write(byte(1));
          }else if(i==1 && j==4){ // ą
            lcd.write(byte(2));
          }else if(i==0 && j==0){ // arrow up
            lcd.write(byte(6));
          }else{
            lcd.print(LCDcontent[i][j]);
          }
        }
      }

      break;
    }

    default:
      break;
  }
}

/////////////////////////////////

void FRAMRead(void *parameter){
  if (!fram.begin(0x50)) {
    for(;;){
      Serial.println("I2C FRAM not identified");
      delay(1000);
      if(fram.begin(0x50)) break;
    }
  }
  Serial.println("I2C FRAM CONNECTED");
  
  /*
    FRAM Memory:
      
    0x00 
    0x01 month
    0x02 date
    0x03 year[0]
    0x04 year[1]

    0x05 hours
    0x06 minutes
    0x07 seconds

    0x08 calibratedDist[0]
    0x09 calibratedDist[1]
    0x0a calibratedDist[2]
    0x0b calibratedDist[3]
    0x0c wakeInterval[0]
    0x0d wakeInterval[1]
    0x0e wakeInterval[2]
    0x0f wakeInterval[3]
    0x10 connectTime[0]
    0x11 connectTime[1]
    0x12 connectTime[2]
    0x13 connectTime[3]

    0x14 last connection: day
    0x15 last connection: month
    0x16 last connection: year[0]
    0x17 last connection: year[1]
    0x18 last connection: minutes
    0x19 last connection: hours

    0x1a batt[0]
    0x1b batt[1]
    0x1c batt[2]
    0x1d batt[3]
  */

  currentTime.month=fram.read(0x01);
  currentTime.day=fram.read(0x02);
  uint8_t tempBuffer[2];
  if(fram.read(0x03,tempBuffer,2)){
    currentTime.year=(tempBuffer[1] << 8) | tempBuffer[0];
  }else{
    currentTime.year=0;
  }

  currentTime.hours=fram.read(0x05);
  currentTime.minutes=fram.read(0x06);
  currentTime.seconds=fram.read(0x07);

  uint8_t tempBuffer2[4];
  if(fram.read(0x08,tempBuffer2,4)){
    ToFCalibratedDist=(tempBuffer2[3] << 24) | (tempBuffer2[2] << 16) | (tempBuffer2[1] << 8) | tempBuffer2[0];
  }
  Serial.print("FRAM calibrated dist: ");
  Serial.println(ToFCalibratedDist);


  if(fram.read(0x0c,tempBuffer2,4)){
    receivedData.wakeInterval=(tempBuffer2[3] << 24) | (tempBuffer2[2] << 16) | (tempBuffer2[1] << 8) | tempBuffer2[0];
  }
  Serial.print("Wake up interval: ");
  Serial.println(receivedData.wakeInterval);

  if(fram.read(0x10,tempBuffer2,4)){
    receivedData.connectTime=(tempBuffer2[3] << 24) | (tempBuffer2[2] << 16) | (tempBuffer2[1] << 8) | tempBuffer2[0];
  }
  Serial.print("Connect time: ");
  Serial.println(receivedData.connectTime);

  uint8_t tempBuffer3[4];
  fram.read(0x1a, tempBuffer3, 4);
  memcpy(&receivedData.batt, tempBuffer3, sizeof(float));

  //last connection data
  receivedData.hours=fram.read(0x19);
  receivedData.minutes=fram.read(0x18);

  uint8_t tempBuffer1[2];
  if(fram.read(0x16,tempBuffer1,2)){
    receivedData.year=(tempBuffer1[1] << 8) | tempBuffer1[0];
  }else{
    receivedData.year=0;
  }

  receivedData.month=fram.read(0x15);
  receivedData.day=fram.read(0x14);


  acquiredData=1;
  vTaskDelete(NULL);
}


// Set up AP
void accessPointConfig(void *parameter) {
  int numNetworks = WiFi.scanNetworks();
  Serial.println("Scan complete");
  delay(100);

  // info about networks
  for (int i = 0; i < numNetworks; ++i) {
    Serial.print("SSID: ");
    Serial.println(WiFi.SSID(i));
    Serial.print("Signal Strength: ");
    Serial.println(WiFi.RSSI(i));
    Serial.print("Channel: ");
    Serial.println(WiFi.channel(i));
    Serial.println("-------------------");
  }

  // simple detection of the best channel
  int bestChannel=1;
  int minConnections=0;
  for(int i=1;i<=13;i++){
    int networksOnChannel=0;
    for(int j=0;j<numNetworks;j++){
      if(WiFi.channel(j)==i){
        networksOnChannel++;
      }
    }
    if(i==1){
      minConnections=networksOnChannel;
      bestChannel=1;
    }else if(networksOnChannel<minConnections){
      minConnections=networksOnChannel;
      bestChannel=i;
    }
  }
  
  WiFi.enableSTA(0);

  Serial.print("Best channel: ");
  Serial.println(bestChannel);
  
  WiFi.softAPConfig(staticIP, gateway, subnet);
  WiFi.softAP(ssid, password, bestChannel, 1, 4, 0);
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
      });

      server.on("/post", HTTP_POST, [](AsyncWebServerRequest *request){
          String messageDistance, messageBatt;

          if (request->hasParam("messageDistance", true)) {
              messageDistance = request->getParam("messageDistance", true)->value();
              acquiredDataPart[0]=1;
              
              receivedData.distance = atof(messageDistance.c_str());

              if(sendData.calibrate){
                ToFCalibratedDist=receivedData.distance;

                uint8_t tempBuffer[4];
                tempBuffer[0] = (int)round(ToFCalibratedDist) & 0xFF;
                tempBuffer[1] = ((int)round(ToFCalibratedDist) >> 8) & 0xFF;
                tempBuffer[2] = ((int)round(ToFCalibratedDist) >> 16) & 0xFF;
                tempBuffer[3] = ((int)round(ToFCalibratedDist) >> 24) & 0xFF;
                fram.write(0x08, tempBuffer, 4);
              }
              
              if(receivedData.distance<ToFCalibratedDist*detectPrecision){
                receivedData.detected=1;
              }else{
                receivedData.detected=0;
              }

          }

          if (request->hasParam("messageBatt", true)) {
              messageBatt = request->getParam("messageBatt", true)->value();
              acquiredDataPart[1]=1;
              receivedData.batt = atof(messageBatt.c_str());
          }

          bool temp=1;
          for(int i=0;i<2;i++){
            if(!acquiredDataPart[i]){
              temp=0;
            }
          }
          if(temp){
            receivedData.minutes=currentTime.minutes;
            receivedData.hours=currentTime.hours;
            receivedData.day=currentTime.day;
            receivedData.month=currentTime.month;
            receivedData.year=currentTime.year;

            fram.write(0x14, receivedData.day);
            fram.write(0x15, receivedData.month);
            uint8_t tempBuffer1[2];
            tempBuffer1[0] = receivedData.year & 0xFF;
            tempBuffer1[1] = (receivedData.year >> 8) & 0xFF;
            fram.write(0x16, tempBuffer1, 2);

            fram.write(0x19, receivedData.hours);
            fram.write(0x18, receivedData.minutes);

            uint8_t tempBuffer[4];
            memcpy(tempBuffer, &receivedData.batt, sizeof(float));
            fram.write(0x1a, tempBuffer, 4);


            if(currentScreen==2 || currentScreen==8){
              displayScreen(currentScreen);
            }
          }else{
            request->send(201, "text/plain", "Error: send data in one frame");
          }

          // check updates
          if(updateRequest==1){
            request->send(200, "text/plain", "update");
          }

          request->send(200, "text/plain", "OK");
      });

      server.on("/sendUpdate", HTTP_GET, [](AsyncWebServerRequest *request){
        String updateMsg = String(sendData.wakeInterval);
        updateMsg += "," + String(sendData.connectTime);
        updateMsg += "," + String(sendData.calibrate);

        uint8_t tempBuffer[4];
        tempBuffer[0] = (int)round(receivedData.wakeInterval) & 0xFF;
        tempBuffer[1] = ((int)round(receivedData.wakeInterval) >> 8) & 0xFF;
        tempBuffer[2] = ((int)round(receivedData.wakeInterval) >> 16) & 0xFF;
        tempBuffer[3] = ((int)round(receivedData.wakeInterval) >> 24) & 0xFF;
        fram.write(0x0c, tempBuffer, 4);

        uint8_t tempBuffer1[4];
        tempBuffer1[0] = (int)round(receivedData.connectTime) & 0xFF;
        tempBuffer1[1] = ((int)round(receivedData.connectTime) >> 8) & 0xFF;
        tempBuffer1[2] = ((int)round(receivedData.connectTime) >> 16) & 0xFF;
        tempBuffer1[3] = ((int)round(receivedData.connectTime) >> 24) & 0xFF;
        fram.write(0x10, tempBuffer1, 4);      

        sendData.calibrate=0;
        updateRequest=0;
        request->send(200, "text/plain", updateMsg);
      });

      server.onNotFound([](AsyncWebServerRequest *request){
        request->send(404, "text/plain", "Not Found");
      });

      server.begin();

      vTaskDelete(NULL);
    }else{
      delay(10);
    }
  }

  vTaskDelete(NULL);
}
