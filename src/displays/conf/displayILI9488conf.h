/*************************************************************************************
    ST7796 480X320 displays configuration file.
    Copy this file to yoRadio/src/displays/conf/displayST7789conf_custom.h
    and modify it
    More info on https://github.com/e2002/yoradio/wiki/Widgets#widgets-description
*************************************************************************************/

#ifndef displayILI9488conf_h
#define displayILI9488conf_h

#define DSP_WIDTH       480
#define DSP_HEIGHT      320
#define TFT_FRAMEWDT    10
#define MAX_WIDTH       DSP_WIDTH-TFT_FRAMEWDT*2

// Coordinates for 60x60 weather icons (can be overridden per-custom config)
#ifndef ICON_WEATHER_LEFT
#define ICON_WEATHER_LEFT 360// Координата X для иконки календаря, просто название осталось от прошлой версии
#endif
#ifndef ICON_WEATHER_TOP
#define ICON_WEATHER_TOP 254// Координата Y для иконки календаря, просто название осталось от прошлой версии
#endif

#if BITRATE_FULL
  #define TITLE_FIX 44
#else
  #define TITLE_FIX 0
#endif
#define bootLogoTop     110

/* SROLLS  */                            /* {{ left, top, fontsize, align }, buffsize, uppercase, width, scrolldelay, scrolldelta, scrolltime } */
const ScrollConfig metaConf       PROGMEM = {{ TFT_FRAMEWDT+64, TFT_FRAMEWDT+34, 3, WA_RIGHT }, 140, true, MAX_WIDTH-64, 5000, 7, 40 };
const ScrollConfig title1Conf     PROGMEM = {{ TFT_FRAMEWDT+74, 48+28, 2, WA_RIGHT }, 140, true, MAX_WIDTH-74, 5000, 7, 40 };
const ScrollConfig title2Conf     PROGMEM = {{ TFT_FRAMEWDT+74, 72+28, 2, WA_RIGHT }, 140, false, MAX_WIDTH-74, 5000, 7, 40 };
const ScrollConfig playlistConf   PROGMEM = {{ TFT_FRAMEWDT, 146, 3, WA_LEFT }, 140, true, MAX_WIDTH, 1000, 7, 40 };
const ScrollConfig apTitleConf    PROGMEM = {{ TFT_FRAMEWDT, TFT_FRAMEWDT, 4, WA_CENTER }, 140, false, MAX_WIDTH, 0, 7, 40 };
const ScrollConfig apSettConf     PROGMEM = {{ TFT_FRAMEWDT, 320-TFT_FRAMEWDT-16, 2, WA_LEFT }, 140, false, MAX_WIDTH, 0, 7, 40 };
const ScrollConfig weatherConf    PROGMEM = {{ TFT_FRAMEWDT+140, 424, 2, WA_LEFT }, 140, false, MAX_WIDTH-130, 0, 5, 40 };

/* BACKGROUNDS  */                       /* {{ left, top, fontsize, align }, width, height, outlined } */
const FillConfig   metaBGConf     PROGMEM = {{ 0, 0, 0, WA_LEFT }, DSP_WIDTH, 40, false };
const FillConfig   metaBGConfInv  PROGMEM = {{ 0, 50, 0, WA_LEFT }, DSP_WIDTH, 0, false };
const FillConfig   volbarConf     PROGMEM = {{ TFT_FRAMEWDT, 330, 0, WA_LEFT }, MAX_WIDTH, 0, true };
const FillConfig  playlBGConf     PROGMEM = {{ 0, 138, 0, WA_LEFT }, DSP_WIDTH, 36, false };
const FillConfig  heapbarConf     PROGMEM = {{ TFT_FRAMEWDT, 36, 0, WA_LEFT }, MAX_WIDTH, 1, false };

/* WIDGETS  */                           /* { left, top, fontsize, align } */
const WidgetConfig bootstrConf    PROGMEM = { 0, 243, 2, WA_CENTER };
const WidgetConfig bitrateConf    PROGMEM = { 6, 62, 2, WA_RIGHT };
const WidgetConfig voltxtConf     PROGMEM = { 250+48, 8, 2, WA_LEFT };
const WidgetConfig  iptxtConf     PROGMEM = { 42, 8, 2, WA_LEFT };
const WidgetConfig   rssiConf     PROGMEM = { 0, 8, 2, WA_RIGHT };
const WidgetConfig numConf        PROGMEM = { 0, 200, 0, WA_CENTER };
const WidgetConfig apNameConf     PROGMEM = { TFT_FRAMEWDT, 88, 3, WA_CENTER };
const WidgetConfig apName2Conf    PROGMEM = { TFT_FRAMEWDT, 120, 3, WA_CENTER };
const WidgetConfig apPassConf     PROGMEM = { TFT_FRAMEWDT, 173, 3, WA_CENTER };
const WidgetConfig apPass2Conf    PROGMEM = { TFT_FRAMEWDT, 205, 3, WA_CENTER };
const WidgetConfig  clockConf     PROGMEM = { 130, 313, 0, WA_RIGHT };
const WidgetConfig vuConf         PROGMEM = { TFT_FRAMEWDT+31, 125, 1, WA_LEFT };

const WidgetConfig bootWdtConf    PROGMEM = { 0, 216, 2, WA_CENTER };
const ProgressConfig bootPrgConf  PROGMEM = { 90, 14, 4 };
const BitrateConfig fullbitrateConf PROGMEM = {{TFT_FRAMEWDT+149, 137, 2, WA_LEFT}, 56 };

/* BANDS  */                             /* { onebandwidth, onebandheight, bandsHspace, bandsVspace, numofbands, fadespeed } */
//const VUBandsConfig bandsConf     PROGMEM = { 440, 12, 8, 2, 16, 3 };
const VUBandsConfig bandsConf   	PROGMEM = { 290, 13,4, 2, 10, 3 };

/* STRINGS  */
const char         numtxtFmt[]    PROGMEM = "%d";
const char           rssiFmt[]    PROGMEM = "%d dBm";
const char          iptxtFmt[]    PROGMEM = "%s";
const char         voltxtFmt[]    PROGMEM = "%d";
const char        bitrateFmt[]    PROGMEM = "%d kBs";

/* MOVES  */                             /* { left, top, width } */
const MoveConfig    clockMove     PROGMEM = { 0, 230, -1 };
const MoveConfig   weatherMove    PROGMEM = { 8, 196, -1};
const MoveConfig   weatherMoveVU  PROGMEM = { 8, 196, -1};

#endif
