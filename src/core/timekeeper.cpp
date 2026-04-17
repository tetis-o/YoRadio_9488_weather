#include "options.h"
#include "Arduino.h"
#include "timekeeper.h"
#include "config.h"
#include "network.h"
#include "display.h"
#include "player.h"
#include "netserver.h"
#include "rtcsupport.h"
#include "../displays/tools/l10n.h"
#include "../pluginsManager/pluginsManager.h"
#include "../displays/fonts/iconsweather.h"
//#include "../displays/fonts/bootlogo62x40.h"
#include "../displays/displayILI9488.h"
#include "../displays/fonts/AvignonProMedium42.h"// шрифт секунд
#include "../displays/fonts/BahnschriftSemiCondensed16.h"
#include "../displays/fonts/BahnschriftSemiCondensed13.h"

constexpr int IC_top     = 176;
constexpr int IC_name    = IC_top+53;
constexpr int IC_left     = 424;

constexpr int ST_LEFT   = 4;
constexpr int ST_TOP    = 44;

#ifdef USE_NEXTION
#include "../displays/nextion.h"
#endif

#if DSP_MODEL==DSP_DUMMY
#define DUMMYDISPLAY
#endif



#if RTCSUPPORTED
  //#define TIME_SYNC_INTERVAL  24*60*60*1000
  #define TIME_SYNC_INTERVAL  config.store.timeSyncIntervalRTC*60*60*1000
#else
  #define TIME_SYNC_INTERVAL  config.store.timeSyncInterval*60*1000
#endif
#define WEATHER_SYNC_INTERVAL config.store.weatherSyncInterval*60*1000

#define SYNC_STACK_SIZE       1024 * 4
#define SYNC_TASK_CORE        0
#define SYNC_TASK_PRIORITY    3
#define WEATHER_STRING_L      254
const char* weatherFmt = "%s %.1f°C (ощущ. %.1f°C) %dмм %d%% %.1fм/с %s %s";
#ifdef HEAP_DBG
  void printHeapFragmentationInfo(const char* title){
    size_t freeHeap = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    size_t largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
    float fragmentation = 100.0 * (1.0 - ((float)largestBlock / (float)freeHeap));
    Serial.printf("\n****** %s ******\n", title);
    Serial.printf("* Free heap: %u bytes\n", freeHeap);
    Serial.printf("* Largest free block: %u bytes\n", largestBlock);
    Serial.printf("* Fragmentation: %.2f%%\n", fragmentation);
    Serial.printf("*************************************\n\n");
  }
  #define HEAP_INFO() printHeapFragmentationInfo(__PRETTY_FUNCTION__)
#else
  #define HEAP_INFO()
#endif

char station[64];
float tempf, wind_speed;
int press, hum, wind_deg;

TimeKeeper timekeeper;

extern void u8fix(char *src);

void _syncTask(void *pvParameters) {
  if (timekeeper.forceWeather && timekeeper.forceTimeSync) {
    timekeeper.timeTask();
    timekeeper.weatherTask();
  } 
  else if (timekeeper.forceWeather) {
    timekeeper.weatherTask();
  }
  else if (timekeeper.forceTimeSync) {
    timekeeper.timeTask();
  }
  timekeeper.busy = false;
  vTaskDelete(NULL);
}

TimeKeeper::TimeKeeper(){
  busy          = false;
  forceWeather  = true;
  forceTimeSync = true;
  _returnPlayerTime = _doAfterTime = 0;
  weatherBuf=NULL;
  weatherIcon[0] = '\0';
  temperature = 0.0;
  #if (DSP_MODEL!=DSP_DUMMY || defined(USE_NEXTION)) && !defined(HIDE_WEATHER)
    weatherBuf = (char *) malloc(sizeof(char) * WEATHER_STRING_L);
    memset(weatherBuf, 0, WEATHER_STRING_L);
  #endif
}

bool TimeKeeper::loop0(){ // core0 (display)
  if (network.status != CONNECTED) return true;
  uint32_t currentTime = millis();
  static uint32_t _last1s = 0;
  static uint32_t _last2s = 0;
  static uint32_t _last5s = 0;
  if (currentTime - _last1s >= 1000) { // 1sec
    _last1s = currentTime;
//#ifndef DUMMYDISPLAY
#if !defined(DUMMYDISPLAY) || defined(USE_NEXTION)
  #ifndef UPCLOCK_CORE1
    _upClock();
  #endif
#endif
  }
  if (currentTime - _last2s >= 2000) { // 2sec
    _last2s = currentTime;
    _upRSSI();
  }
  if (currentTime - _last5s >= 5000) { // 5sec
    _last5s = currentTime;
    //HEAP_INFO();
  }

  return true; // just in case
}

bool TimeKeeper::loop1(){ // core1 (player)
  uint32_t currentTime = millis();
  static uint32_t _last1s = 0;
  static uint32_t _last2s = 0;
  if (currentTime - _last1s >= 1000) { // 1sec
    pm.on_ticker();
    _last1s = currentTime;
//#ifndef DUMMYDISPLAY
#if !defined(DUMMYDISPLAY) || defined(USE_NEXTION)
  #ifdef UPCLOCK_CORE1
    _upClock();
  #endif
#endif
    _upScreensaver();
    _upSDPos();
    _returnPlayer();
    _doAfterWait();
  }
  if (currentTime - _last2s >= 2000) { // 2sec
    _last2s = currentTime;
  }

  //#ifdef DUMMYDISPLAY
  #if defined(DUMMYDISPLAY) && !defined(USE_NEXTION)
  return true;
  #endif
  // Sync weather & time
  static uint32_t lastWeatherTime = 0;
  if (currentTime - lastWeatherTime >= WEATHER_SYNC_INTERVAL) {
    lastWeatherTime = currentTime;
    forceWeather = true;
  }
  static uint32_t lastTimeTime = 0;
  if (currentTime - lastTimeTime >= TIME_SYNC_INTERVAL) {
    lastTimeTime = currentTime;
    forceTimeSync = true;
  }
  if (!busy && (forceWeather || forceTimeSync) && network.status == CONNECTED) {
    busy = true;
    //config.setTimeConf();
    xTaskCreatePinnedToCore(
      _syncTask,
      "syncTask",
      SYNC_STACK_SIZE,
      NULL,           // Params
      SYNC_TASK_PRIORITY,
      NULL,           // Descriptor
      SYNC_TASK_CORE
    );
  }
  
  return true; // just in case
}

void TimeKeeper::waitAndReturnPlayer(uint8_t time_s){
  _returnPlayerTime = millis()+time_s*1000;
}
void TimeKeeper::_returnPlayer(){
  if(_returnPlayerTime>0 && millis()>=_returnPlayerTime){
    _returnPlayerTime = 0;
    display.putRequest(NEWMODE, PLAYER);
  }
}

void TimeKeeper::waitAndDo(uint8_t time_s, void (*callback)()){
  _doAfterTime = millis()+time_s*1000;
  _aftercallback = callback;
}
void TimeKeeper::_doAfterWait(){
  if(_doAfterTime>0 && millis()>=_doAfterTime){
    _doAfterTime = 0;
    _aftercallback();
  }
}

void TimeKeeper::_upClock(){
#if RTCSUPPORTED
  if(config.isRTCFound()) rtc.getTime(&network.timeinfo);
#else
  if(network.timeinfo.tm_year>100 || network.status == SDREADY) {
    network.timeinfo.tm_sec++;
    mktime(&network.timeinfo);
  }
#endif
  if(display.ready()) display.putRequest(CLOCK);
}

void TimeKeeper::_upScreensaver(){
#ifndef DSP_LCD
  if(!display.ready()) return;
  if(config.store.screensaverEnabled && display.mode()==PLAYER && !player.isRunning()){
    config.screensaverTicks++;
    if(config.screensaverTicks > config.store.screensaverTimeout+SCREENSAVERSTARTUPDELAY){
      if(config.store.screensaverBlank){
        display.putRequest(NEWMODE, SCREENBLANK);
      }else{
        display.putRequest(NEWMODE, SCREENSAVER);
      }
      config.screensaverTicks=SCREENSAVERSTARTUPDELAY;
    }
  }
  if(config.store.screensaverPlayingEnabled && display.mode()==PLAYER && player.isRunning()){
    config.screensaverPlayingTicks++;
    if(config.screensaverPlayingTicks > config.store.screensaverPlayingTimeout*60+SCREENSAVERSTARTUPDELAY){
      if(config.store.screensaverPlayingBlank){
        display.putRequest(NEWMODE, SCREENBLANK);
      }else{
        display.putRequest(NEWMODE, SCREENSAVER);
      }
      config.screensaverPlayingTicks=SCREENSAVERSTARTUPDELAY;
    }
  }
#endif
}

void TimeKeeper::_upRSSI(){
  if(network.status == CONNECTED){
    netserver.setRSSI(WiFi.RSSI());
    netserver.requestOnChange(NRSSI, 0);
    if(display.ready()) display.putRequest(DSPRSSI, netserver.getRSSI());
  }
#ifdef USE_SD
  if(display.mode()!=SDCHANGE) player.sendCommand({PR_CHECKSD, 0});
#endif
  player.sendCommand({PR_VUTONUS, 0});
}

void TimeKeeper::_upSDPos(){
  if(player.isRunning() && config.getMode()==PM_SDCARD) netserver.requestOnChange(SDPOS, 0);
}

void TimeKeeper::timeTask(){
  static uint8_t tsFailCnt = 0;
  config.waitConnection();
  if(getLocalTime(&network.timeinfo)){
    tsFailCnt = 0;
    forceTimeSync = false;
    mktime(&network.timeinfo);
    display.putRequest(CLOCK, 1);
    network.requestTimeSync(true);
    #if RTCSUPPORTED
      if (config.isRTCFound()) rtc.setTime(&network.timeinfo);
    #endif
  }else{
    if(tsFailCnt<4){
      forceTimeSync = true;
      tsFailCnt++;
    }else{
      forceTimeSync = false;
      tsFailCnt=0;
    }
  }
}
void TimeKeeper::weatherTask(){
  forceWeather = false;
  if(!weatherBuf || strlen(config.store.weatherkey)==0 || !config.store.showweather) return;
  _getWeather();
}

bool _getWeather() {
#if (DSP_MODEL!=DSP_DUMMY || defined(USE_NEXTION)) && !defined(HIDE_WEATHER)
  static AsyncClient * weatherClient = NULL;
  static const char* host = "api.openweathermap.org";
  if(weatherClient) return false;
  weatherClient = new AsyncClient();
  if(!weatherClient) return false;

  weatherClient->onError([](void * arg, AsyncClient * client, int error){
    Serial.println("##WEATHER###: connection error");
    weatherClient = NULL;
    delete client;
  }, NULL);

  weatherClient->onConnect([](void * arg, AsyncClient * client){
    weatherClient->onError(NULL, NULL);
    weatherClient->onDisconnect([](void * arg, AsyncClient * c){ weatherClient = NULL; delete c; }, NULL);
    
    char httpget[250] = {0};
    sprintf(httpget, "GET /data/2.5/weather?lat=%s&lon=%s&units=%s&lang=%s&appid=%s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", config.store.weatherlat, config.store.weatherlon, LANG::weatherUnits, LANG::weatherLang, config.store.weatherkey, host);
    client->write(httpget);
    
    client->onData([](void * arg, AsyncClient * c, void * data, size_t len){
      uint8_t * d = (uint8_t*)data;
      const char *bodyStart = strstr((const char*)d, "\r\n\r\n");
      if (bodyStart != NULL) {
        bodyStart += 4;
        size_t bodyLen = len - (bodyStart - (const char*)d);
        char line[bodyLen+1];
        memcpy(line, bodyStart, bodyLen);
        line[bodyLen] = '\0';
        /* parse it */
        char *cursor;
        char desc[120], icon[5];
        float tempfl;
        //int   wind_deg;
        bool result = true;
        //cursor = strstr(line, "\"name\":\"");
        //if (cursor) { sscanf(cursor, "\"name\":\"%63[^\"]", station); } else { strcpy(station, ""); }
        //char station_full[70];
        //snprintf(station_full, sizeof(station_full), "м. ст. %s", station);
        cursor = strstr(line, "\"name\":\"");
        if (cursor) { sscanf(cursor, "\"name\":\"%63[^\"]", station); } else { strcpy(station, ""); }
        u8fix(station);
        cursor = strstr(line, "\"description\":\"");
        if (cursor) { sscanf(cursor, "\"description\":\"%119[^\"]", desc); }else{ Serial.println("##WEATHER###: description not found !"); result=false; }
        u8fix(desc);
        cursor = strstr(line, "\"icon\":\"");
        if (cursor) { sscanf(cursor, "\"icon\":\"%4[^\"]", icon); strlcpy(timekeeper.weatherIcon, icon, sizeof(timekeeper.weatherIcon)); }else{ Serial.println("##WEATHER###: icon not found !"); result=false; }
        cursor = strstr(line, "\"temp\":");
        if (cursor) { sscanf(cursor, "\"temp\":%f", &tempf); }else{ Serial.println("##WEATHER###: temp not found !"); result=false; }
        cursor = strstr(line, "\"pressure\":");
        if (cursor) { sscanf(cursor, "\"pressure\":%d", &press); }else{ Serial.println("##WEATHER###: pressure not found !"); result=false; }
        cursor = strstr(line, "\"humidity\":");
        if (cursor) { sscanf(cursor, "\"humidity\":%d", &hum); }else{ Serial.println("##WEATHER###: humidity not found !"); result=false; }
        cursor = strstr(line, "\"feels_like\":");
        if (cursor) { sscanf(cursor, "\"feels_like\":%f", &tempfl); }else{ Serial.println("##WEATHER###: feels_like not found !"); result=false; }
        cursor = strstr(line, "\"grnd_level\":");
        if (cursor) { sscanf(cursor, "\"grnd_level\":%d", &press); }
        cursor = strstr(line, "\"speed\":");
        if (cursor) { sscanf(cursor, "\"speed\":%f", &wind_speed); }else{ Serial.println("##WEATHER###: wind speed not found !"); result=false; }
        cursor = strstr(line, "\"deg\":");
        if (cursor) { sscanf(cursor, "\"deg\":%d", &wind_deg); }else{ Serial.println("##WEATHER###: wind deg not found !"); result=false; }
       
        press = press / 1+5;

        if(!result) return;

        #ifdef USE_NEXTION
          nextion.putcmdf("press_txt.txt=\"%dmm\"", press);
          nextion.putcmdf("hum_txt.txt=\"%d%%\"", hum);
          char cmd[30];
          snprintf(cmd, sizeof(cmd)-1,"temp_txt.txt=\"%.1f\"", tempf);
          nextion.putcmd(cmd);
          int iconofset;
          if(strstr(icon,"01")!=NULL)      iconofset = 0;
          else if(strstr(icon,"02")!=NULL) iconofset = 1;
          else if(strstr(icon,"03")!=NULL) iconofset = 2;
          else if(strstr(icon,"04")!=NULL) iconofset = 3;
          else if(strstr(icon,"09")!=NULL) iconofset = 4;
          else if(strstr(icon,"10")!=NULL) iconofset = 5;
          else if(strstr(icon,"11")!=NULL) iconofset = 6;
          else if(strstr(icon,"13")!=NULL) iconofset = 7;
          else if(strstr(icon,"50")!=NULL) iconofset = 8;
          else                             iconofset = 9;
          nextion.putcmd("cond_img.pic", 50+iconofset);
          nextion.weatherVisible(1);
        #endif
        
        Serial.printf("##WEATHER###: description: %s, temp:%.1f C, pressure:%dmmHg, humidity:%d%%, wind: %d\n", desc, tempf, press, hum, (int)(wind_deg/22.5));
        #ifdef WEATHER_FMT_SHORT
        sprintf(timekeeper.weatherBuf, weatherFmt,  tempf, press, hum);
        #else
          #if EXT_WEATHER
            sprintf(timekeeper.weatherBuf, LANG::weatherFmt, utf8Rus(desc, false), tempf, tempfl, press, hum, wind_speed, LANG::wind[(int)(wind_deg/22.5)], utf8Rus(station, false));
          #else
            sprintf(timekeeper.weatherBuf, LANG::weatherFmt, tempf, press, wind_speed);
          #endif
        #endif
        display.putRequest(NEWWEATHER);
      } else {
        Serial.println("##WEATHER###: weather not found !");
      }
    }, NULL); // <-- client->onData
  }, NULL); // <-- weatherClient->onConnect
  config.waitConnection();
  if(!weatherClient->connect(host, 80)){
    Serial.println("##WEATHER###: connection failed");
    AsyncClient * client = weatherClient;
    weatherClient = NULL;
    delete client;
  }

  return true;
#endif // if (DSP_MODEL!=DSP_DUMMY || defined(USE_NEXTION)) && !defined(HIDE_WEATHER)
  return false;
}
void TimeKeeper::PrintIcon() {  
 //dsp.fillRect(ICON_WEATHER_LEFT, ICON_WEATHER_TOP-24, 106,89, 0x0000);
  //if (!display.ready()) return;
  if (config.store.showweather){//} && weatherIcon[0] != '\0') {
   // dsp.drawRGBBitmap(8, IC_top, hot, 32, 32);
    
    
     
      

    char tempStr[30];
    sprintf(tempStr, "%.1f^C", tempf);
    
    dsp.setFont(&BahnschriftSemiCondensed16);
    dsp.setTextColor (0xA510);
    dsp.setTextSize(1);
    dsp.setCursor(64-strlen(tempStr)*4.5, IC_name);
    dsp.fillRect(20, IC_name-17, 100, 20, 0x0000); 
    dsp.print(tempStr); 


    dsp.setTextColor (0xA510);//(0xE68B);
    sprintf(tempStr, "%d hPa", press);
    dsp.setCursor(130, IC_name);
    dsp.fillRect(130, IC_name-17, 78, 20, 0x0000); 
    if (press < 1000) dsp.drawRGBBitmap(144, IC_top, pressure1, 44, 32);
    else if (press > 1016) dsp.drawRGBBitmap(144, IC_top, pressure3, 44, 32);
    else   dsp.drawRGBBitmap(144, IC_top, pressure2, 44, 32); 
    
    dsp.print(tempStr);

      sprintf(tempStr, "%d%%", hum);
      dsp.setCursor(250, IC_name);
      dsp.fillRect(250, IC_name-17, 50, 20, 0x0000); 
      if (hum < 40) dsp.drawRGBBitmap(250, IC_top, humid1, 30, 32);
      else if (hum > 65) dsp.drawRGBBitmap(250, IC_top, humid3, 30, 32);
      else  dsp.drawRGBBitmap(250, IC_top, humid2, 30, 32);  
      
      dsp.print(tempStr);

      sprintf(tempStr, "%.1f m/s", wind_speed);
      dsp.setCursor(330, IC_name);
      dsp.fillRect(330, IC_name-17, 78, 21, 0x0000); 
if (wind_speed < 3.3) dsp.drawRGBBitmap(340, IC_top, wind1, 42, 32);
else if (wind_speed >= 3.4 && wind_speed < 7.9) dsp.drawRGBBitmap(340, IC_top, wind2, 42, 32);
else if (wind_speed < 10.8) dsp.drawRGBBitmap(340, IC_top, wind3, 42, 32);
else dsp.drawRGBBitmap(340, IC_top, wind4, 42, 32);
      dsp.print(tempStr);


    /*sprintf(tempStr, "[%s]", utf8Rus(LANG::wind[(int)(wind_deg/22.5)], false));
    dsp.setCursor(340, 234);
    dsp.fillRect(340, 218, 50, 20, 0x0000); 
    dsp.print(tempStr);*/


    sprintf(tempStr, "%d^", (int)(wind_deg));
    dsp.setCursor(424, IC_name);
    dsp.fillRect(424, IC_name-17, 50, 20, 0x0000); 
    dsp.print(tempStr);

    //выбираем иконку направления ветра
    uint16_t dir = (int)(wind_deg/22.5);
    if (dir==0||dir==15 || dir==16) dsp.drawRGBBitmap(IC_left , IC_top, wind_n, 32, 32);
    else if (dir==1||dir==2) dsp.drawRGBBitmap(IC_left , IC_top, wind_ne, 32, 32);
    else if (dir==3||dir==4) dsp.drawRGBBitmap(IC_left , IC_top, wind_e, 32, 32);
    else if (dir==5||dir==6) dsp.drawRGBBitmap(IC_left , IC_top, wind_se, 32, 32);
    else if (dir==7||dir==8) dsp.drawRGBBitmap(IC_left , IC_top, wind_s, 32, 32);
    else if (dir==9||dir==10) dsp.drawRGBBitmap(IC_left , IC_top, wind_sw, 32, 32);
    else if (dir==11||dir==12) dsp.drawRGBBitmap(IC_left , IC_top, wind_w, 32, 32);
    else if (dir==13||dir==14) dsp.drawRGBBitmap(IC_left , IC_top, wind_nw, 32, 32);
    else dsp.drawRGBBitmap(IC_left, IC_top, roza, 32, 32);

    //выводим название метеостанции
    //dsp.drawRGBBitmap(180, IC_top+30, weat_station, 60, 28);
   // dsp.setFont(&BahnschriftSemiCondensed13);
    //char station_conv[128];
    //strlcpy(station_conv, utf8Rus(station, false), sizeof(station_conv));
    //snprintf(tempStr, sizeof(tempStr), "%s", station_conv);
    //dsp.setCursor(246, IC_name+30);
    //dsp.fillRect(246, IC_name+13, 80, 20, 0x0000); 
    //dsp.print(tempStr);

dsp.setFont();



  if(strstr(timekeeper.weatherIcon, "01d") != NULL)      dsp.drawRGBBitmap(48, IC_top-2, img_01d, 36, 36);
  else if(strstr(timekeeper.weatherIcon, "01n") != NULL) dsp.drawRGBBitmap(48, IC_top-2, img_01n, 36, 36);
  else if(strstr(timekeeper.weatherIcon, "02d") != NULL) dsp.drawRGBBitmap(48, IC_top-2, img_02d, 36, 36);
  else if(strstr(timekeeper.weatherIcon, "02n") != NULL) dsp.drawRGBBitmap(48, IC_top-2, img_02n, 36, 36);
  else if(strstr(timekeeper.weatherIcon, "03d") != NULL) dsp.drawRGBBitmap(48, IC_top-2, img_03d, 36, 36);
  else if(strstr(timekeeper.weatherIcon, "03n") != NULL) dsp.drawRGBBitmap(48, IC_top-2, img_03n, 36, 36);
  else if(strstr(timekeeper.weatherIcon, "04d") != NULL) dsp.drawRGBBitmap(48, IC_top-2, img_04d, 36, 36);
  else if(strstr(timekeeper.weatherIcon, "04n") != NULL) dsp.drawRGBBitmap(48, IC_top-2, img_04n, 36, 36);
  else if(strstr(timekeeper.weatherIcon, "09d") != NULL) dsp.drawRGBBitmap(48, IC_top-2, img_09d, 36, 36);
  else if(strstr(timekeeper.weatherIcon, "09n") != NULL) dsp.drawRGBBitmap(48, IC_top-2, img_09n, 36, 36);
  else if(strstr(timekeeper.weatherIcon, "10d") != NULL) dsp.drawRGBBitmap(48, IC_top-2, img_10d, 36, 36);
  else if(strstr(timekeeper.weatherIcon, "10n") != NULL) dsp.drawRGBBitmap(48, IC_top-2, img_10n, 36, 36);
  else if(strstr(timekeeper.weatherIcon, "11d") != NULL) dsp.drawRGBBitmap(48, IC_top-2, img_11d, 36, 36);
  else if(strstr(timekeeper.weatherIcon, "11n") != NULL) dsp.drawRGBBitmap(48, IC_top-2, img_11n, 36, 36);
  else if(strstr(timekeeper.weatherIcon, "13d") != NULL) dsp.drawRGBBitmap(48, IC_top-2, img_13d, 36, 36);
  else if(strstr(timekeeper.weatherIcon, "13n") != NULL) dsp.drawRGBBitmap(48, IC_top-2, img_13n, 36, 36);
  else if(strstr(timekeeper.weatherIcon, "50d") != NULL) dsp.drawRGBBitmap(48, IC_top-2, img_50d, 36, 36);
  else if(strstr(timekeeper.weatherIcon, "50n") != NULL) dsp.drawRGBBitmap(48, IC_top-2, img_50n, 36, 36);
  else  dsp.drawRGBBitmap(48, IC_top-2, weather4, 36, 36);
  
// } else if (config.store.showweather) {
 //  PrintWeate();
   
 } else {
   //dsp.drawRGBBitmap(ICON_WEATHER_LEFT, ICON_WEATHER_TOP, eradio, 99, 64);

 }
}
 void TimeKeeper::PrintWeate() {
//******************************* вывод номера канала *******************************
   dsp.setFont(&BahnschriftSemiCondensed16);
    dsp.setTextColor(0xE68B);
    dsp.setTextSize(1);
    dsp.fillRect(ST_LEFT, ST_TOP+47, 58 , 20,  0x0000); // очистка области номера канала
        dsp.setCursor(ST_LEFT, ST_TOP+62);
    char chbuf[12];
    if (config.getMode() == PM_SDCARD) {
      snprintf(chbuf, sizeof(chbuf), "Tr.%d ", config.lastStation());
      dsp.print(chbuf);
      dsp.drawRGBBitmap(ST_LEFT, ST_TOP, sdcard2, 51, 40); // sdcard icon for SD playback
    } else {

      snprintf(chbuf, sizeof(chbuf), "Ch.%d ", config.lastStation());
      dsp.print(chbuf);
      dsp.fillRect(ST_LEFT, ST_TOP, 51 , 40,  0x0000);
      dsp.drawRGBBitmap(ST_LEFT, ST_TOP, station5, 45, 40); // station icon for web streams
    }
  dsp.setFont();
 }
//******************
