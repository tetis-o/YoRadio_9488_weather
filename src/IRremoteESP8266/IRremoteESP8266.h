 /***************************************************
 * IRremote for ESP8266
 *
 * Based on the IRremote library for Arduino by Ken Shirriff
 * Version 0.11 August, 2009
 * Copyright 2009 Ken Shirriff
 * For details, see http://arcfn.com/2009/08/multi-protocol-infrared-remote-library.html
 *
 * Edited by Mitra to add new controller SANYO
 *
 * Interrupt code based on NECIRrcv by Joe Knapp
 * http://www.arduino.cc/cgi-bin/yabb2/YaBB.pl?num=1210243556
 * Also influenced by http://zovirl.com/2008/11/12/building-a-universal-remote-with-an-arduino/
 *
 * JVC and Panasonic protocol added by Kristian Lauszus (Thanks to zenwheel and other people at the original blog post)
 * LG added by Darryl Smith (based on the JVC protocol)
 * Whynter A/C ARC-110WD added by Francesco Meschia
 * Coolix A/C / heatpump added by (send) bakrus & (decode) crankyoldgit
 * Denon: sendDenon, decodeDenon added by Massimiliano Pinto
          (from https://github.com/z3t0/Arduino-IRremote/blob/master/ir_Denon.cpp)
 * Kelvinator A/C and Sherwood added by crankyoldgit
 * Mitsubishi (TV) sending added by crankyoldgit
 * Pronto code sending added by crankyoldgit
 * Mitsubishi & Toshiba A/C added by crankyoldgit
 *     (derived from https://github.com/r45635/HVAC-IR-Control)
 * DISH decode by marcosamarinho
 * Gree Heatpump sending added by Ville Skyttä (scop)
 *     (derived from https://github.com/ToniA/arduino-heatpumpir/blob/master/GreeHeatpumpIR.cpp)
 * Updated by markszabo (https://github.com/crankyoldgit/IRremoteESP8266) for sending IR code on ESP8266
 * Updated by Sebastien Warin (http://sebastien.warin.fr) for receiving IR code on ESP8266
 *
 * Updated by sillyfrog for Daikin, adopted from
 * (https://github.com/mharizanov/Daikin-AC-remote-control-over-the-Internet/)
 * Fujitsu A/C code added by jonnygraham
 * Trotec AC code by stufisher
 * Carrier & Haier AC code by crankyoldgit
 * Vestel AC code by Erdem U. Altınyurt
 * Teco AC code by Fabien Valthier (hcoohb)
 * Mitsubishi 112 AC Code by kuchel77
 * Kelon AC code by Davide Depau (Depau)
 *
 *  GPL license, all text above must be included in any redistribution
 ****************************************************/

#ifndef IRREMOTEESP8266_H_
#define IRREMOTEESP8266_H_

#define __STDC_LIMIT_MACROS
#include <stdint.h>
#ifdef UNIT_TEST
#include <iostream>
#include <string>
#endif  // UNIT_TEST

// Library Version Information
// Major version number (X.x.x)
#define _IRREMOTEESP8266_VERSION_MAJOR 2
// Minor version number (x.X.x)
#define _IRREMOTEESP8266_VERSION_MINOR 8
// Patch version number (x.x.X)
#define _IRREMOTEESP8266_VERSION_PATCH 6
// Macro to convert version info into an integer
#define _IRREMOTEESP8266_VERSION_VAL(major, minor, patch) \
                                    (((major) << 16) | ((minor) << 8) | (patch))
// Macro to convert literal into a string
#define MKSTR_HELPER(x) #x
#define MKSTR(x) MKSTR_HELPER(x)
// Integer version
#define _IRREMOTEESP8266_VERSION _IRREMOTEESP8266_VERSION_VAL(\
    _IRREMOTEESP8266_VERSION_MAJOR, \
    _IRREMOTEESP8266_VERSION_MINOR, \
    _IRREMOTEESP8266_VERSION_PATCH)
// String version
#define _IRREMOTEESP8266_VERSION_STR MKSTR(_IRREMOTEESP8266_VERSION_MAJOR) "." \
                                     MKSTR(_IRREMOTEESP8266_VERSION_MINOR) "." \
                                     MKSTR(_IRREMOTEESP8266_VERSION_PATCH)
// String version (DEPRECATED)
#define _IRREMOTEESP8266_VERSION_ _IRREMOTEESP8266_VERSION_STR

// Set the language & locale for the library. See the `locale` dir for options.
#ifndef _IR_LOCALE_
#define _IR_LOCALE_ en-AU
#endif  // _IR_LOCALE_

// Do we enable all the protocols by default (true), or disable them (false)?
// This allows users of the library to disable or enable all protocols at
// compile-time with `-D_IR_ENABLE_DEFAULT_=true` or
// `-D_IR_ENABLE_DEFAULT_=false` compiler flags respectively.
// Everything is included by default.
// e.g. If you only want to enable use of he NEC protocol to save program space,
//      you would use something like:
//        `-D_IR_ENABLE_DEFAULT_=false -DDECODE_NEC=true -DSEND_NEC=true`
//
//      or alter your 'platform.ini' file accordingly:
//        ```
//        build_flags = -D_IR_ENABLE_DEFAULT_=false
//                      -DDECODE_NEC=true
//                      -DSEND_NEC=true
//        ```
//      If you want to enable support for every protocol *except* _decoding_ the
//      Kelvinator protocol, you would use:
//        `-DDECODE_KELVINATOR=false`
#ifndef _IR_ENABLE_DEFAULT_
#define _IR_ENABLE_DEFAULT_ true  // Unless set externally, the default is on.
#endif  // _IR_ENABLE_DEFAULT_

// Supported IR protocols
// Each protocol you include costs memory and, during decode, costs time
// Disable (set to false) all the protocols you do not need/want!
// The Air Conditioner protocols are the most expensive memory-wise.
//

// Semi-unique code for unknown messages
#ifndef DECODE_HASH
#define DECODE_HASH            _IR_ENABLE_DEFAULT_
#endif  // DECODE_HASH

#ifndef SEND_RAW
#define SEND_RAW               _IR_ENABLE_DEFAULT_
#endif  // SEND_RAW

#ifndef DECODE_NEC
#define DECODE_NEC             _IR_ENABLE_DEFAULT_
#endif  // DECODE_NEC
#ifndef SEND_NEC
#define SEND_NEC               _IR_ENABLE_DEFAULT_
#endif  // SEND_NEC

#ifndef DECODE_PANASONIC
#define DECODE_PANASONIC       _IR_ENABLE_DEFAULT_
#endif  // DECODE_PANASONIC
#ifndef SEND_PANASONIC
#define SEND_PANASONIC         _IR_ENABLE_DEFAULT_
#endif  // SEND_PANASONIC

#ifndef DECODE_SAMSUNG
#define DECODE_SAMSUNG         _IR_ENABLE_DEFAULT_
#endif  // DECODE_SAMSUNG
#ifndef SEND_SAMSUNG
#define SEND_SAMSUNG           _IR_ENABLE_DEFAULT_
#endif  // SEND_SAMSUNG

#ifndef DECODE_SAMSUNG36
#define DECODE_SAMSUNG36       _IR_ENABLE_DEFAULT_
#endif  // DECODE_SAMSUNG36
#ifndef SEND_SAMSUNG36
#define SEND_SAMSUNG36         _IR_ENABLE_DEFAULT_
#endif  // SEND_SAMSUNG36

#ifndef DECODE_SAMSUNG_AC
#define DECODE_SAMSUNG_AC      _IR_ENABLE_DEFAULT_
#endif  // DECODE_SAMSUNG_AC
#ifndef SEND_SAMSUNG_AC
#define SEND_SAMSUNG_AC        _IR_ENABLE_DEFAULT_
#endif  // SEND_SAMSUNG_AC

#ifndef DECODE_SANYO
#define DECODE_SANYO           _IR_ENABLE_DEFAULT_
#endif  // DECODE_SANYO
#ifndef SEND_SANYO
#define SEND_SANYO             _IR_ENABLE_DEFAULT_
#endif  // SEND_SANYO

#ifndef DECODE_SANYO_AC
#define DECODE_SANYO_AC        _IR_ENABLE_DEFAULT_
#endif  // DECODE_SANYO_AC
#ifndef SEND_SANYO_AC
#define SEND_SANYO_AC          _IR_ENABLE_DEFAULT_
#endif  // SEND_SANYO_AC

#ifndef DECODE_SANYO_AC88
#define DECODE_SANYO_AC88      _IR_ENABLE_DEFAULT_
#endif  // DECODE_SANYO_AC88
#ifndef SEND_SANYO_AC88
#define SEND_SANYO_AC88        _IR_ENABLE_DEFAULT_
#endif  // SEND_SANYO_AC88

#ifndef DECODE_SANYO_AC152
#define DECODE_SANYO_AC152     _IR_ENABLE_DEFAULT_
#endif  // DECODE_SANYO_AC152
#ifndef SEND_SANYO_AC152
#define SEND_SANYO_AC152       _IR_ENABLE_DEFAULT_
#endif  // SEND_SANYO_AC152

#ifndef DECODE_SHARP
#define DECODE_SHARP           _IR_ENABLE_DEFAULT_
#endif  // DECODE_SHARP
#ifndef SEND_SHARP
#define SEND_SHARP             _IR_ENABLE_DEFAULT_
#endif  // SEND_SHARP

#ifndef DECODE_SHARP_AC
#define DECODE_SHARP_AC        _IR_ENABLE_DEFAULT_
#endif  // DECODE_SHARP_AC
#ifndef SEND_SHARP_AC
#define SEND_SHARP_AC          _IR_ENABLE_DEFAULT_
#endif  // SEND_SHARP_AC

#ifndef DECODE_TOSHIBA_AC
#define DECODE_TOSHIBA_AC      _IR_ENABLE_DEFAULT_
#endif  // DECODE_TOSHIBA_AC
#ifndef SEND_TOSHIBA_AC
#define SEND_TOSHIBA_AC        _IR_ENABLE_DEFAULT_
#endif  // SEND_TOSHIBA_AC

#ifndef DECODE_PANASONIC_AC
#define DECODE_PANASONIC_AC    _IR_ENABLE_DEFAULT_
#endif  // DECODE_PANASONIC_AC
#ifndef SEND_PANASONIC_AC
#define SEND_PANASONIC_AC      _IR_ENABLE_DEFAULT_
#endif  // SEND_PANASONIC_AC

#ifndef DECODE_PANASONIC_AC32
#define DECODE_PANASONIC_AC32  _IR_ENABLE_DEFAULT_
#endif  // DECODE_PANASONIC_AC32
#ifndef SEND_PANASONIC_AC32
#define SEND_PANASONIC_AC32    _IR_ENABLE_DEFAULT_
#endif  // SEND_PANASONIC_AC32

#ifndef DECODE_TCL96AC
#define DECODE_TCL96AC        _IR_ENABLE_DEFAULT_
#endif  // DECODE_TCL96AC
#ifndef SEND_TCL96AC
#define SEND_TCL96AC          _IR_ENABLE_DEFAULT_
#endif  // SEND_TCL96AC

#ifndef DECODE_TCL112AC
#define DECODE_TCL112AC        _IR_ENABLE_DEFAULT_
#endif  // DECODE_TCL112AC
#ifndef SEND_TCL112AC
#define SEND_TCL112AC          _IR_ENABLE_DEFAULT_
#endif  // SEND_TCL112AC

#if (DECODE_ARGO || DECODE_DAIKIN || DECODE_FUJITSU_AC || DECODE_GREE || \
     DECODE_KELVINATOR || DECODE_MITSUBISHI_AC || DECODE_TOSHIBA_AC || \
     DECODE_TROTEC || DECODE_HAIER_AC || DECODE_HITACHI_AC || \
     DECODE_HITACHI_AC1 || DECODE_HITACHI_AC2 || DECODE_HAIER_AC_YRW02 || \
     DECODE_WHIRLPOOL_AC || DECODE_SAMSUNG_AC || DECODE_ELECTRA_AC || \
     DECODE_PANASONIC_AC || DECODE_MWM || DECODE_DAIKIN2 || \
     DECODE_VESTEL_AC || DECODE_TCL112AC || DECODE_MITSUBISHIHEAVY || \
     DECODE_DAIKIN216 || DECODE_SHARP_AC || DECODE_DAIKIN160 || \
     DECODE_NEOCLIMA || DECODE_DAIKIN176 || DECODE_DAIKIN128 || \
     DECODE_AMCOR || DECODE_DAIKIN152 || DECODE_MITSUBISHI136 || \
     DECODE_MITSUBISHI112 || DECODE_HITACHI_AC424 || DECODE_HITACHI_AC3 || \
     DECODE_HITACHI_AC344 || DECODE_CORONA_AC || DECODE_SANYO_AC || \
     DECODE_VOLTAS || DECODE_MIRAGE || DECODE_HAIER_AC176 || \
     DECODE_TEKNOPOINT || DECODE_KELON || DECODE_TROTEC_3550 || \
     DECODE_SANYO_AC88 || DECODE_RHOSS || DECODE_HITACHI_AC264 || \
     DECODE_KELON168 || DECODE_HITACHI_AC296 || DECODE_CARRIER_AC128 || \
     DECODE_DAIKIN200 || DECODE_HAIER_AC160 || DECODE_TCL96AC || \
     DECODE_BOSCH144 || DECODE_SANYO_AC152 || DECODE_DAIKIN312 || \
     DECODE_CARRIER_AC84 || DECODE_YORK || \
     false)
  // Add any DECODE to the above if it uses result->state (see kStateSizeMax)
  // you might also want to add the protocol to hasACState function
#define DECODE_AC true  // We need some common infrastructure for decoding A/Cs.
#else
#define DECODE_AC false   // We don't need that infrastructure.
#endif

// Use millisecond 'delay()' calls where we can to avoid tripping the WDT.
// Note: If you plan to send IR messages in the callbacks of the AsyncWebserver
//       library, you need to set ALLOW_DELAY_CALLS to false.
//       Ref: https://github.com/crankyoldgit/IRremoteESP8266/issues/430
#ifndef ALLOW_DELAY_CALLS
#define ALLOW_DELAY_CALLS true
#endif  // ALLOW_DELAY_CALLS

// Enable a run-time settable high-pass filter on captured data **before**
// trying any protocol decoding.
// i.e. Try to remove/merge any really short pulses detected in the raw data.
// Note: Even when this option is enabled, it is _off_ by default, and requires
//       a user who knows what they are doing to enable it.
//       The option to disable this feature is here if your project is _really_
//       tight on resources. i.e. Saves a small handful of bytes and cpu time.
// WARNING: If you use this feature at runtime, you can no longer trust the
//          **raw** data captured. It will now have been slightly **cooked**!
// DANGER: If you set the `noise_floor` value too high, it **WILL** break
//         decoding of some protocols. You have been warned. Here Be Dragons!
//
// See: `irrecv::decode()` in IRrecv.cpp for more info.
#ifndef ENABLE_NOISE_FILTER_OPTION
#define ENABLE_NOISE_FILTER_OPTION true
#endif  // ENABLE_NOISE_FILTER_OPTION

/// Enumerator for defining and numbering of supported IR protocol.
/// @note Always add to the end of the list and should never remove entries
///  or change order. Projects may save the type number for later usage
///  so numbering should always stay the same.
enum decode_type_t {
  UNKNOWN = -1,
  UNUSED = 0,
  RC5,
  RC6,
  NEC,
  SONY,
  PANASONIC,  // (5)
  JVC,
  SAMSUNG,
  WHYNTER,
  AIWA_RC_T501,
  LG,  // (10)
  SANYO,
  MITSUBISHI,
  DISH,
  SHARP,
  COOLIX,  // (15)
  DAIKIN,
  DENON,
  KELVINATOR,
  SHERWOOD,
  MITSUBISHI_AC,  // (20)
  RCMM,
  SANYO_LC7461,
  RC5X,
  GREE,
  PRONTO,  // Technically not a protocol, but an encoding. (25)
  NEC_LIKE,
  ARGO,
  TROTEC,
  NIKAI,
  RAW,  // Technically not a protocol, but an encoding. (30)
  GLOBALCACHE,  // Technically not a protocol, but an encoding.
  TOSHIBA_AC,
  FUJITSU_AC,
  MIDEA,
  MAGIQUEST,  // (35)
  LASERTAG,
  CARRIER_AC,
  HAIER_AC,
  MITSUBISHI2,
  HITACHI_AC,  // (40)
  HITACHI_AC1,
  HITACHI_AC2,
  GICABLE,
  HAIER_AC_YRW02,
  WHIRLPOOL_AC,  // (45)
  SAMSUNG_AC,
  LUTRON,
  ELECTRA_AC,
  PANASONIC_AC,
  PIONEER,  // (50)
  LG2,
  MWM,
  DAIKIN2,
  VESTEL_AC,
  TECO,  // (55)
  SAMSUNG36,
  TCL112AC,
  LEGOPF,
  MITSUBISHI_HEAVY_88,
  MITSUBISHI_HEAVY_152,  // 60
  DAIKIN216,
  SHARP_AC,
  GOODWEATHER,
  INAX,
  DAIKIN160,  // 65
  NEOCLIMA,
  DAIKIN176,
  DAIKIN128,
  AMCOR,
  DAIKIN152,  // 70
  MITSUBISHI136,
  MITSUBISHI112,
  HITACHI_AC424,
  SONY_38K,
  EPSON,  // 75
  SYMPHONY,
  HITACHI_AC3,
  DAIKIN64,
  AIRWELL,
  DELONGHI_AC,  // 80
  DOSHISHA,
  MULTIBRACKETS,
  CARRIER_AC40,
  CARRIER_AC64,
  HITACHI_AC344,  // 85
  CORONA_AC,
  MIDEA24,
  ZEPEAL,
  SANYO_AC,
  VOLTAS,  // 90
  METZ,
  TRANSCOLD,
  TECHNIBEL_AC,
  MIRAGE,
  ELITESCREENS,  // 95
  PANASONIC_AC32,
  MILESTAG2,
  ECOCLIM,
  XMP,
  TRUMA,  // 100
  HAIER_AC176,
  TEKNOPOINT,
  KELON,
  TROTEC_3550,
  SANYO_AC88,  // 105
  BOSE,
  ARRIS,
  RHOSS,
  AIRTON,
  COOLIX48,  // 110
  HITACHI_AC264,
  KELON168,
  HITACHI_AC296,
  DAIKIN200,
  HAIER_AC160,  // 115
  CARRIER_AC128,
  TOTO,
  CLIMABUTLER,
  TCL96AC,
  BOSCH144,  // 120
  SANYO_AC152,
  DAIKIN312,
  GORENJE,
  WOWWEE,
  CARRIER_AC84,  // 125
  YORK,
  // Add new entries before this one, and update it to point to the last entry.
  kLastDecodeType = YORK,
};

// Message lengths & required repeat values
const uint16_t kNoRepeat = 0;
const uint16_t kSingleRepeat = 1;

// TODO(anyone): Verify that the Mitsubishi repeat is really needed.
const uint16_t kHitachiAcStateLength = 28;
const uint16_t kHitachiAcBits = kHitachiAcStateLength * 8;
const uint16_t kHitachiAcDefaultRepeat = kNoRepeat;
const uint16_t kHitachiAc1StateLength = 13;
const uint16_t kHitachiAc1Bits = kHitachiAc1StateLength * 8;
const uint16_t kHitachiAc2StateLength = 53;
const uint16_t kHitachiAc2Bits = kHitachiAc2StateLength * 8;
const uint16_t kHitachiAc3StateLength = 27;
const uint16_t kHitachiAc3Bits = kHitachiAc3StateLength * 8;
const uint16_t kHitachiAc3MinStateLength = 15;
const uint16_t kHitachiAc3MinBits = kHitachiAc3MinStateLength * 8;
const uint16_t kHitachiAc264StateLength = 33;
const uint16_t kHitachiAc264Bits = kHitachiAc264StateLength * 8;
const uint16_t kHitachiAc296StateLength = 37;
const uint16_t kHitachiAc296Bits = kHitachiAc296StateLength * 8;
const uint16_t kHitachiAc344StateLength = 43;
const uint16_t kHitachiAc344Bits = kHitachiAc344StateLength * 8;
const uint16_t kHitachiAc424StateLength = 53;
const uint16_t kHitachiAc424Bits = kHitachiAc424StateLength * 8;
//               Based on marcosamarinho's code.
const uint16_t kNECBits = 32;
const uint16_t kPanasonicBits = 48;
const uint32_t kPanasonicManufacturer = 0x4004;
const uint32_t kPanasonic40Manufacturer = 0x34;
const uint16_t kPanasonic40Bits = 40;
const uint16_t kPanasonicAcStateLength = 27;
const uint16_t kPanasonicAcStateShortLength = 16;
const uint16_t kPanasonicAcBits = kPanasonicAcStateLength * 8;
const uint16_t kPanasonicAcShortBits = kPanasonicAcStateShortLength * 8;
const uint16_t kPanasonicAcDefaultRepeat = kNoRepeat;
const uint16_t kPanasonicAc32Bits = 32;
const uint16_t kPioneerBits = 64;
const uint16_t kSamsungBits = 32;
const uint16_t kSamsung36Bits = 36;
const uint16_t kSamsungAcStateLength = 14;
const uint16_t kSamsungAcBits = kSamsungAcStateLength * 8;
const uint16_t kSamsungAcExtendedStateLength = 21;
const uint16_t kSamsungAcExtendedBits = kSamsungAcExtendedStateLength * 8;
const uint16_t kSamsungAcDefaultRepeat = kNoRepeat;
const uint16_t kSanyoAcStateLength = 9;
const uint16_t kSanyoAcBits = kSanyoAcStateLength * 8;
const uint16_t kSanyoAc88StateLength = 11;
const uint16_t kSanyoAc88Bits = kSanyoAc88StateLength * 8;
const uint16_t kSanyoAc88MinRepeat = 2;
const uint16_t kSanyoAc152StateLength = 19;
const uint16_t kSanyoAc152Bits = kSanyoAc152StateLength * 8;
const uint16_t kSanyoAc152MinRepeat = kNoRepeat;
const uint16_t kSanyoSA8650BBits = 12;
const uint16_t kSanyoLC7461AddressBits = 13;
const uint16_t kSanyoLC7461CommandBits = 8;
const uint16_t kSanyoLC7461Bits = (kSanyoLC7461AddressBits +
                                   kSanyoLC7461CommandBits) * 2;
const uint8_t  kSharpAddressBits = 5;
const uint8_t  kSharpCommandBits = 8;
const uint16_t kSharpBits = kSharpAddressBits + kSharpCommandBits + 2;  // 15
const uint16_t kSharpAcStateLength = 13;
const uint16_t kSharpAcBits = kSharpAcStateLength * 8;  // 104
const uint16_t kSharpAcDefaultRepeat = kNoRepeat;
const uint16_t kSony12Bits = 12;
const uint16_t kSony15Bits = 15;
const uint16_t kSony20Bits = 20;
const uint16_t kSonyMinBits = 12;
const uint16_t kSonyMinRepeat = 2;
const uint16_t kTcl96AcStateLength = 12;
const uint16_t kTcl96AcBits = kTcl96AcStateLength * 8;
const uint16_t kTcl96AcDefaultRepeat = kNoRepeat;
const uint16_t kTcl112AcStateLength = 14;
const uint16_t kTcl112AcBits = kTcl112AcStateLength * 8;
const uint16_t kTcl112AcDefaultRepeat = kNoRepeat;
const uint16_t kToshibaACStateLength = 9;
const uint16_t kToshibaACBits = kToshibaACStateLength * 8;
const uint16_t kToshibaACMinRepeat = kSingleRepeat;
const uint16_t kToshibaACStateLengthShort = kToshibaACStateLength - 2;
const uint16_t kToshibaACBitsShort = kToshibaACStateLengthShort * 8;
const uint16_t kToshibaACStateLengthLong = kToshibaACStateLength + 1;
const uint16_t kToshibaACBitsLong = kToshibaACStateLengthLong * 8;
const uint16_t kTrumaBits = 56;


// Legacy defines. (Deprecated)
#define NEC_BITS                      kNECBits
#define PANASONIC_BITS                kPanasonicBits
#define SANYO_LC7461_BITS             kSanyoLC7461Bits
#define SAMSUNG_BITS                  kSamsungBits
#define SANYO_SA8650B_BITS            kSanyoSA8650BBits
#define SONY_12_BITS                  kSony12Bits
#define SONY_15_BITS                  kSony15Bits
#define SONY_20_BITS                  kSony20Bits

// Turn on Debugging information by uncommenting the following line.
// #define DEBUG 1

#ifdef DEBUG
#ifdef UNIT_TEST
#define DPRINT(x) do { std::cout << x; } while (0)
#define DPRINTLN(x) do { std::cout << x << std::endl; } while (0)
#endif  // UNIT_TEST
#ifdef ARDUINO
#define DPRINT(x) do { Serial.print(x); } while (0)
#define DPRINTLN(x) do { Serial.println(x); } while (0)
#endif  // ARDUINO
#else  // DEBUG
#define DPRINT(x)
#define DPRINTLN(x)
#endif  // DEBUG

#ifdef UNIT_TEST
#ifndef F
// Create a no-op F() macro so the code base still compiles outside of the
// Arduino framework. Thus we can safely use the Arduino 'F()' macro through-out
// the code base. That macro stores constants in Flash (PROGMEM) memory.
// See: https://github.com/crankyoldgit/IRremoteESP8266/issues/667
#define F(x) x
#endif  // F
typedef std::string String;
#endif  // UNIT_TEST

#endif  // IRREMOTEESP8266_H_
