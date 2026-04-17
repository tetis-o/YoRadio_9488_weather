// Copyright 2009 Ken Shirriff
// Copyright 2015 Mark Szabo
// Copyright 2017 David Conran
#ifndef IRSEND_H_
#define IRSEND_H_

#define __STDC_LIMIT_MACROS
#include <stdint.h>
#include "IRremoteESP8266.h"

// Originally from https://github.com/shirriff/Arduino-IRremote/
// Updated by markszabo (https://github.com/crankyoldgit/IRremoteESP8266) for
// sending IR code on ESP8266

#if TEST || UNIT_TEST
#define VIRTUAL virtual
#else
#define VIRTUAL
#endif

// Constants
// Offset (in microseconds) to use in Period time calculations to account for
// code excution time in producing the software PWM signal.
#if defined(ESP32)
// Calculated on a generic ESP-WROOM-32 board with v3.2-18 SDK @ 240MHz
const int8_t kPeriodOffset = -2;
#elif (defined(ESP8266) && F_CPU == 160000000L)  // NOLINT(whitespace/parens)
// Calculated on an ESP8266 NodeMCU v2 board using:
// v2.6.0 with v2.5.2 ESP core @ 160MHz
const int8_t kPeriodOffset = -2;
#else  // (defined(ESP8266) && F_CPU == 160000000L)
// Calculated on ESP8266 Wemos D1 mini using v2.4.1 with v2.4.0 ESP core @ 40MHz
const int8_t kPeriodOffset = -5;
#endif  // (defined(ESP8266) && F_CPU == 160000000L)
const uint8_t kDutyDefault = 50;  // Percentage
const uint8_t kDutyMax = 100;     // Percentage
// delayMicroseconds() is only accurate to 16383us.
// Ref: https://www.arduino.cc/en/Reference/delayMicroseconds
const uint16_t kMaxAccurateUsecDelay = 16383;
//  Usecs to wait between messages we don't know the proper gap time.
const uint32_t kDefaultMessageGap = 100000;
/// Placeholder for missing sensor temp value
/// @note Not using "-1" as it may be a valid external temp
const float kNoTempValue = -100.0;

/// Enumerators and Structures for the Common A/C API.
namespace stdAc {
/// Common A/C settings for A/C operating modes.
enum class opmode_t {
  kOff  = -1,
  kAuto =  0,
  kCool =  1,
  kHeat =  2,
  kDry  =  3,
  kFan  =  4,
  // Add new entries before this one, and update it to point to the last entry
  kLastOpmodeEnum = kFan,
};

/// Common A/C settings for Fan Speeds.
enum class fanspeed_t {
  kAuto =       0,
  kMin =        1,
  kLow =        2,
  kMedium =     3,
  kHigh =       4,
  kMax =        5,
  kMediumHigh = 6,
  // Add new entries before this one, and update it to point to the last entry
  kLastFanspeedEnum = kMediumHigh,
};

/// Common A/C settings for Vertical Swing.
enum class swingv_t {
  kOff =    -1,
  kAuto =    0,
  kHighest = 1,
  kHigh =    2,
  kMiddle =  3,
  kLow =     4,
  kLowest =  5,
  kUpperMiddle = 6,
  // Add new entries before this one, and update it to point to the last entry
  kLastSwingvEnum = kUpperMiddle,
};

/// @brief Tyoe of A/C command (if the remote uses different codes for each)
/// @note Most remotes support only a single command or aggregate multiple
///       into one (e.g. control+timer). Use @c kControlCommand in such case
enum class ac_command_t {
  kControlCommand = 0,
  kSensorTempReport = 1,
  kTimerCommand = 2,
  kConfigCommand = 3,
  // Add new entries before this one, and update it to point to the last entry
  kLastAcCommandEnum = kConfigCommand,
};

/// Common A/C settings for Horizontal Swing.
enum class swingh_t {
  kOff =     -1,
  kAuto =     0,  // a.k.a. On.
  kLeftMax =  1,
  kLeft =     2,
  kMiddle =   3,
  kRight =    4,
  kRightMax = 5,
  kWide =     6,  // a.k.a. left & right at the same time.
  // Add new entries before this one, and update it to point to the last entry
  kLastSwinghEnum = kWide,
};

/// Structure to hold a common A/C state.
struct state_t {
  decode_type_t protocol = decode_type_t::UNKNOWN;
  int16_t model = -1;  // `-1` means unused.
  bool power = false;
  stdAc::opmode_t mode = stdAc::opmode_t::kOff;
  float degrees = 25;
  bool celsius = true;
  stdAc::fanspeed_t fanspeed = stdAc::fanspeed_t::kAuto;
  stdAc::swingv_t swingv = stdAc::swingv_t::kOff;
  stdAc::swingh_t swingh = stdAc::swingh_t::kOff;
  bool quiet = false;
  bool turbo = false;
  bool econo = false;
  bool light = false;
  bool filter = false;
  bool clean = false;
  bool beep = false;
  int16_t sleep = -1;  // `-1` means off.
  int16_t clock = -1;  // `-1` means not set.
  stdAc::ac_command_t command = stdAc::ac_command_t::kControlCommand;
  bool iFeel = false;
  float sensorTemperature = kNoTempValue;  // `kNoTempValue` means not set.
};
};  // namespace stdAc

/// Panasonic A/C model numbers
enum panasonic_ac_remote_model_t {
  kPanasonicUnknown = 0,
  kPanasonicLke = 1,
  kPanasonicNke = 2,
  kPanasonicDke = 3,  // PKR too.
  kPanasonicJke = 4,
  kPanasonicCkp = 5,
  kPanasonicRkr = 6,
};

/// Sharp A/C model numbers
enum sharp_ac_remote_model_t {
  A907 = 1,
  A705 = 2,
  A903 = 3,  // 820 too
};

/// TCL (& Teknopoint) A/C model numbers
enum tcl_ac_remote_model_t {
  TAC09CHSD = 1,
  GZ055BE1 = 2,  // Also Teknopoint GZ01-BEJ0-000
};

// Classes

/// Class for sending all basic IR protocols.
/// @note Originally from https://github.com/shirriff/Arduino-IRremote/
///  Updated by markszabo (https://github.com/crankyoldgit/IRremoteESP8266) for
///  sending IR code on ESP8266
class IRsend {
 public:
  explicit IRsend(uint16_t IRsendPin, bool inverted = false,
                  bool use_modulation = true);
  void begin();
  void enableIROut(uint32_t freq, uint8_t duty = kDutyDefault);
  VIRTUAL void _delayMicroseconds(uint32_t usec);
  VIRTUAL uint16_t mark(uint16_t usec);
  VIRTUAL void space(uint32_t usec);
  int8_t calibrate(uint16_t hz = 38000U);
  void sendRaw(const uint16_t buf[], const uint16_t len, const uint16_t hz);
  void sendData(uint16_t onemark, uint32_t onespace, uint16_t zeromark,
                uint32_t zerospace, uint64_t data, uint16_t nbits,
                bool MSBfirst = true);
  void sendManchesterData(const uint16_t half_period, const uint64_t data,
                          const uint16_t nbits, const bool MSBfirst = true,
                          const bool GEThomas = true);
  void sendManchester(const uint16_t headermark, const uint32_t headerspace,
                      const uint16_t half_period, const uint16_t footermark,
                      const uint32_t gap, const uint64_t data,
                      const uint16_t nbits, const uint16_t frequency = 38,
                      const bool MSBfirst = true,
                      const uint16_t repeat = kNoRepeat,
                      const uint8_t dutycycle = kDutyDefault,
                      const bool GEThomas = true);
  void sendGeneric(const uint16_t headermark, const uint32_t headerspace,
                   const uint16_t onemark, const uint32_t onespace,
                   const uint16_t zeromark, const uint32_t zerospace,
                   const uint16_t footermark, const uint32_t gap,
                   const uint64_t data, const uint16_t nbits,
                   const uint16_t frequency, const bool MSBfirst,
                   const uint16_t repeat, const uint8_t dutycycle);
  void sendGeneric(const uint16_t headermark, const uint32_t headerspace,
                   const uint16_t onemark, const uint32_t onespace,
                   const uint16_t zeromark, const uint32_t zerospace,
                   const uint16_t footermark, const uint32_t gap,
                   const uint32_t mesgtime, const uint64_t data,
                   const uint16_t nbits, const uint16_t frequency,
                   const bool MSBfirst, const uint16_t repeat,
                   const uint8_t dutycycle);
  void sendGeneric(const uint16_t headermark, const uint32_t headerspace,
                   const uint16_t onemark, const uint32_t onespace,
                   const uint16_t zeromark, const uint32_t zerospace,
                   const uint16_t footermark, const uint32_t gap,
                   const uint8_t *dataptr, const uint16_t nbytes,
                   const uint16_t frequency, const bool MSBfirst,
                   const uint16_t repeat, const uint8_t dutycycle);
  static uint16_t minRepeats(const decode_type_t protocol);
  static uint16_t defaultBits(const decode_type_t protocol);
  bool send(const decode_type_t type, const uint64_t data,
            const uint16_t nbits, const uint16_t repeat = kNoRepeat);
  bool send(const decode_type_t type, const uint8_t *state,
            const uint16_t nbytes);
#if (SEND_NEC || SEND_SANYO)
  void sendNEC(uint64_t data, uint16_t nbits = kNECBits,
               uint16_t repeat = kNoRepeat);
  uint32_t encodeNEC(uint16_t address, uint16_t command);
#endif
#if SEND_SONY
  // sendSony() should typically be called with repeat=2 as Sony devices
  // expect the code to be sent at least 3 times. (code + 2 repeats = 3 codes)
  // Legacy use of this procedure was to only send a single code so call it with
  // repeat=0 for backward compatibility. As of v2.0 it defaults to sending
  // a Sony command that will be accepted be a device.
  void sendSony(const uint64_t data, const uint16_t nbits = kSony20Bits,
                const uint16_t repeat = kSonyMinRepeat);
  void sendSony38(const uint64_t data, const uint16_t nbits = kSony20Bits,
                  const uint16_t repeat = kSonyMinRepeat + 1);
  uint32_t encodeSony(const uint16_t nbits, const uint16_t command,
                      const uint16_t address, const uint16_t extended = 0);
#endif  // SEND_SONY
  // `sendSAMSUNG()` is required by `sendLG()`
#if (SEND_SAMSUNG || SEND_LG)
  void sendSAMSUNG(const uint64_t data, const uint16_t nbits = kSamsungBits,
                   const uint16_t repeat = kNoRepeat);
  uint32_t encodeSAMSUNG(const uint8_t customer, const uint8_t command);
#endif  // (SEND_SAMSUNG || SEND_LG)
#if SEND_SAMSUNG36
  void sendSamsung36(const uint64_t data, const uint16_t nbits = kSamsung36Bits,
                     const uint16_t repeat = kNoRepeat);
#endif
#if SEND_SAMSUNG_AC
  void sendSamsungAC(const unsigned char data[],
                     const uint16_t nbytes = kSamsungAcStateLength,
                     const uint16_t repeat = kSamsungAcDefaultRepeat);
#endif
#if (SEND_SHARP)
  uint32_t encodeSharp(const uint16_t address, const uint16_t command,
                       const uint16_t expansion = 1, const uint16_t check = 0,
                       const bool MSBfirst = false);
  void sendSharp(const uint16_t address, const uint16_t command,
                 const uint16_t nbits = kSharpBits,
                 const uint16_t repeat = kNoRepeat);
  void sendSharpRaw(const uint64_t data, const uint16_t nbits = kSharpBits,
                    const uint16_t repeat = kNoRepeat);
#endif
#if SEND_SHARP_AC
  void sendSharpAc(const unsigned char data[],
                   const uint16_t nbytes = kSharpAcStateLength,
                   const uint16_t repeat = kSharpAcDefaultRepeat);
#endif  // SEND_SHARP_AC
#if SEND_SANYO
  uint64_t encodeSanyoLC7461(uint16_t address, uint8_t command);
  void sendSanyoLC7461(const uint64_t data,
                       const uint16_t nbits = kSanyoLC7461Bits,
                       const uint16_t repeat = kNoRepeat);
#endif
#if SEND_SANYO_AC
  void sendSanyoAc(const uint8_t *data,
                   const uint16_t nbytes = kSanyoAcStateLength,
                   const uint16_t repeat = kNoRepeat);
#endif  // SEND_SANYO_AC
#if SEND_SANYO_AC88
  void sendSanyoAc88(const uint8_t *data,
                     const uint16_t nbytes = kSanyoAc88StateLength,
                     const uint16_t repeat = kSanyoAc88MinRepeat);
#endif  // SEND_SANYO_AC88
#if SEND_SANYO_AC152
  void sendSanyoAc152(const uint8_t *data,
                     const uint16_t nbytes = kSanyoAc152StateLength,
                     const uint16_t repeat = kSanyoAc152MinRepeat);
#endif  // SEND_SANYO_AC152
#if (SEND_PANASONIC || SEND_DENON)
  void sendPanasonic64(const uint64_t data,
                       const uint16_t nbits = kPanasonicBits,
                       const uint16_t repeat = kNoRepeat);
  void sendPanasonic(const uint16_t address, const uint32_t data,
                     const uint16_t nbits = kPanasonicBits,
                     const uint16_t repeat = kNoRepeat);
  uint64_t encodePanasonic(const uint16_t manufacturer, const uint8_t device,
                           const uint8_t subdevice, const uint8_t function);
#endif
#if SEND_GLOBALCACHE
  void sendGC(uint16_t buf[], uint16_t len);
#endif
#if SEND_TOSHIBA_AC
  void sendToshibaAC(const uint8_t data[],
                     const uint16_t nbytes = kToshibaACStateLength,
                     const uint16_t repeat = kToshibaACMinRepeat);
#endif
#if SEND_PANASONIC_AC
  void sendPanasonicAC(const unsigned char data[],
                       const uint16_t nbytes = kPanasonicAcStateLength,
                       const uint16_t repeat = kPanasonicAcDefaultRepeat);
#endif  // SEND_PANASONIC_AC
#if SEND_PANASONIC_AC32
  void sendPanasonicAC32(const uint64_t data,
                         const uint16_t nbits = kPanasonicAc32Bits,
                         const uint16_t repeat = kPanasonicAcDefaultRepeat);
#endif  // SEND_PANASONIC_AC32
#if SEND_TCL96AC
  void sendTcl96Ac(const unsigned char data[],
                    const uint16_t nbytes = kTcl96AcStateLength,
                    const uint16_t repeat = kTcl96AcDefaultRepeat);
#endif  // SEND_TCL96AC
#if SEND_TCL112AC
  void sendTcl112Ac(const unsigned char data[],
                    const uint16_t nbytes = kTcl112AcStateLength,
                    const uint16_t repeat = kTcl112AcDefaultRepeat);
#endif  // SEND_TCL112AC

 protected:
#ifdef UNIT_TEST
#ifndef HIGH
#define HIGH 0x1
#endif
#ifndef LOW
#define LOW 0x0
#endif
#endif  // UNIT_TEST
  uint8_t outputOn;
  uint8_t outputOff;
  VIRTUAL void ledOff();
  VIRTUAL void ledOn();
#ifndef UNIT_TEST

 private:
#else
  uint32_t _freq_unittest;
#endif  // UNIT_TEST
  uint16_t onTimePeriod;
  uint16_t offTimePeriod;
  uint16_t IRpin;
  int8_t periodOffset;
  uint8_t _dutycycle;
  bool modulation;
  uint32_t calcUSecPeriod(uint32_t hz, bool use_offset = true);
#if SEND_SONY
  void _sendSony(const uint64_t data, const uint16_t nbits,
                 const uint16_t repeat, const uint16_t freq);
#endif  // SEND_SONY
};

#endif  // IRSEND_H_
