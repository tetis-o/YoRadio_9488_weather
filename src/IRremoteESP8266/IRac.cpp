// Copyright 2019 David Conran

// Provide a universal/standard interface for sending A/C nessages.
// It does not provide complete and maximum granular control but tries
// to offer most common functionality across all supported devices.

#include "IRac.h"
#ifndef UNIT_TEST
#include <Arduino.h>
#endif
#include <string.h>
#ifndef ARDUINO
#include <string>
#endif
#include <cmath>
#if __cplusplus >= 201103L && defined(_GLIBCXX_USE_C99_MATH_TR1)
    using std::roundf;
#else
    using ::roundf;
#endif
#include "IRsend.h"
#include "IRremoteESP8266.h"
#include "IRtext.h"
#include "IRutils.h"
#include "ir_Panasonic.h"
#include "ir_Samsung.h"
#include "ir_Sanyo.h"
#include "ir_Sharp.h"
#include "ir_Tcl.h"
#include "ir_Toshiba.h"

// On the ESP8266 platform we need to use a special version of string handling
// functions to handle the strings stored in the flash address space.
#ifndef STRCASECMP
#if defined(ESP8266)
#define STRCASECMP(LHS, RHS) \
    strcasecmp_P(LHS, reinterpret_cast<const char*>(RHS))
#else  // ESP8266
#define STRCASECMP(LHS, RHS) strcasecmp(LHS, RHS)
#endif  // ESP8266
#endif  // STRCASECMP

#ifndef UNIT_TEST
#define OUTPUT_DECODE_RESULTS_FOR_UT(ac)
#else
/* NOTE: THIS IS NOT A DOXYGEN COMMENT (would require ENABLE_PREPROCESSING-YES)
/// If compiling for UT *and* a test receiver @c IRrecv is provided via the
/// @c _utReceived param, this injects an "output" gadget @c _lastDecodeResults
/// into the @c IRAc::sendAc method, so that the UT code may parse the "sent"
/// value and drive further assertions
///
/// @note The @c decode_results "returned" is a shallow copy (empty rawbuf),
///       mostly b/c the class does not have a custom/deep copy c-tor
///       and defining it would be an overkill for this purpose
/// @note For future maintainers: If @c IRAc class is ever refactored to use
///       polymorphism (static or dynamic)... this macro should be removed
///       and replaced with proper GMock injection.
*/
#define OUTPUT_DECODE_RESULTS_FOR_UT(ac)                        \
  {                                                             \
    if (_utReceiver) {                                          \
      _lastDecodeResults = nullptr;                             \
      (ac)._irsend.makeDecodeResult();                          \
      if (_utReceiver->decode(&(ac)._irsend.capture)) {         \
        _lastDecodeResults = std::unique_ptr<decode_results>(   \
          new decode_results((ac)._irsend.capture));            \
        _lastDecodeResults->rawbuf = nullptr;                   \
      }                                                         \
    }                                                           \
  }
#endif  // UNIT_TEST

/// Class constructor
/// @param[in] pin Gpio pin to use when transmitting IR messages.
/// @param[in] inverted true, gpio output defaults to high. false, to low.
/// @param[in] use_modulation true means use frequency modulation. false, don't.
IRac::IRac(const uint16_t pin, const bool inverted, const bool use_modulation) {
  _pin = pin;
  _inverted = inverted;
  _modulation = use_modulation;
  this->markAsSent();
}

/// Initialise the given state with the supplied settings.
/// @param[out] state A Ptr to where the settings will be stored.
/// @param[in] vendor The vendor/protocol type.
/// @param[in] model The A/C model if applicable.
/// @param[in] power The power setting.
/// @param[in] mode The operation mode setting.
/// @param[in] degrees The temperature setting in degrees.
/// @param[in] celsius Temperature units. True is Celsius, False is Fahrenheit.
/// @param[in] fan The speed setting for the fan.
/// @param[in] swingv The vertical swing setting.
/// @param[in] swingh The horizontal swing setting.
/// @param[in] quiet Run the device in quiet/silent mode.
/// @param[in] turbo Run the device in turbo/powerful mode.
/// @param[in] econo Run the device in economical mode.
/// @param[in] light Turn on the LED/Display mode.
/// @param[in] filter Turn on the (ion/pollen/etc) filter mode.
/// @param[in] clean Turn on the self-cleaning mode. e.g. Mould, dry filters etc
/// @param[in] beep Enable/Disable beeps when receiving IR messages.
/// @param[in] sleep Nr. of minutes for sleep mode.
///  -1 is Off, >= 0 is on. Some devices it is the nr. of mins to run for.
///  Others it may be the time to enter/exit sleep mode.
///  i.e. Time in Nr. of mins since midnight.
/// @param[in] clock The time in Nr. of mins since midnight. < 0 is ignore.
void IRac::initState(stdAc::state_t *state,
                     const decode_type_t vendor, const int16_t model,
                     const bool power, const stdAc::opmode_t mode,
                     const float degrees, const bool celsius,
                     const stdAc::fanspeed_t fan,
                     const stdAc::swingv_t swingv, const stdAc::swingh_t swingh,
                     const bool quiet, const bool turbo, const bool econo,
                     const bool light, const bool filter, const bool clean,
                     const bool beep, const int16_t sleep,
                     const int16_t clock) {
  state->protocol = vendor;
  state->model = model;
  state->power = power;
  state->mode = mode;
  state->degrees = degrees;
  state->celsius = celsius;
  state->fanspeed = fan;
  state->swingv = swingv;
  state->swingh = swingh;
  state->quiet = quiet;
  state->turbo = turbo;
  state->econo = econo;
  state->light = light;
  state->filter = filter;
  state->clean = clean;
  state->beep = beep;
  state->sleep = sleep;
  state->clock = clock;
}

/// Initialise the given state with the supplied settings.
/// @param[out] state A Ptr to where the settings will be stored.
/// @note Sets all the parameters to reasonable base/automatic defaults.
void IRac::initState(stdAc::state_t *state) {
  stdAc::state_t def;
  *state = def;
}

/// Get the current internal A/C climate state.
/// @return A Ptr to a state containing the current (to be sent) settings.
stdAc::state_t IRac::getState(void) { return next; }

/// Get the previous internal A/C climate state that should have already been
/// sent to the device. i.e. What the A/C unit should already be set to.
/// @return A Ptr to a state containing the previously sent settings.
stdAc::state_t IRac::getStatePrev(void) { return _prev; }

/// Is the given protocol supported by the IRac class?
/// @param[in] protocol The vendor/protocol type.
/// @return true if the protocol is supported by this class, otherwise false.
bool IRac::isProtocolSupported(const decode_type_t protocol) {
  switch (protocol) {
#if SEND_PANASONIC_AC
    case decode_type_t::PANASONIC_AC:
#endif
#if SEND_PANASONIC_AC32
    case decode_type_t::PANASONIC_AC32:
#endif
#if SEND_SAMSUNG_AC
    case decode_type_t::SAMSUNG_AC:
#endif
#if SEND_SANYO_AC
    case decode_type_t::SANYO_AC:
#endif
#if SEND_SANYO_AC88
    case decode_type_t::SANYO_AC88:
#endif
#if SEND_SHARP_AC
    case decode_type_t::SHARP_AC:
#endif
#if SEND_TCL112AC
    case decode_type_t::TCL112AC:
#endif
#if SEND_TOSHIBA_AC
    case decode_type_t::TOSHIBA_AC:
#endif
      return true;
    default:
      return false;
  }
}

#if SEND_AIRTON
/// Send an Airton 56-bit A/C message with the supplied settings.
/// @param[in, out] ac A Ptr to an IRAirtonAc object to use.
/// @param[in] on The power setting.
/// @param[in] mode The operation mode setting.
/// @param[in] degrees The temperature setting in degrees.
/// @param[in] fan The speed setting for the fan.
/// @param[in] swingv The vertical swing setting.
/// @param[in] turbo Run the device in turbo/powerful mode.
/// @param[in] light Turn on the LED/Display mode.
/// @param[in] econo Run the device in economical mode.
/// @param[in] filter Turn on the (ion/pollen/health/etc) filter mode.
/// @param[in] sleep Nr. of minutes for sleep mode.
/// @note -1 is Off, >= 0 is on.
void IRac::airton(IRAirtonAc *ac,
                  const bool on, const stdAc::opmode_t mode,
                  const float degrees, const stdAc::fanspeed_t fan,
                  const stdAc::swingv_t swingv, const bool turbo,
                  const bool light, const bool econo, const bool filter,
                  const int16_t sleep) {
  ac->begin();
  ac->setPower(on);
  ac->setMode(ac->convertMode(mode));
  ac->setTemp(degrees);
  ac->setFan(ac->convertFan(fan));
  ac->setSwingV(swingv != stdAc::swingv_t::kOff);
  // No Quiet setting available.
  ac->setLight(light);
  ac->setHealth(filter);
  ac->setTurbo(turbo);
  ac->setEcono(econo);
  // No Clean setting available.
  // No Beep setting available.
  ac->setSleep(sleep >= 0);  // Convert to a boolean.
  ac->send();
}
#endif  // SEND_AIRTON

#if SEND_AIRWELL
/// Send an Airwell A/C message with the supplied settings.
/// @param[in, out] ac A Ptr to an IRAirwellAc object to use.
/// @param[in] on The power setting.
/// @param[in] mode The operation mode setting.
/// @param[in] degrees The temperature setting in degrees.
/// @param[in] fan The speed setting for the fan.
void IRac::airwell(IRAirwellAc *ac,
                   const bool on, const stdAc::opmode_t mode,
                   const float degrees, const stdAc::fanspeed_t fan) {
  ac->begin();
  ac->setPowerToggle(on);
  ac->setMode(ac->convertMode(mode));
  ac->setTemp(degrees);
  ac->setFan(ac->convertFan(fan));
  // No Swing setting available.
  // No Quiet setting available.
  // No Light setting available.
  // No Filter setting available.
  // No Turbo setting available.
  // No Economy setting available.
  // No Clean setting available.
  // No Beep setting available.
  // No Sleep setting available.
  ac->send();
}
#endif  // SEND_AIRWELL

#if SEND_AMCOR
/// Send an Amcor A/C message with the supplied settings.
/// @param[in, out] ac A Ptr to an IRAmcorAc object to use.
/// @param[in] on The power setting.
/// @param[in] mode The operation mode setting.
/// @param[in] degrees The temperature setting in degrees.
/// @param[in] fan The speed setting for the fan.
void IRac::amcor(IRAmcorAc *ac,
                const bool on, const stdAc::opmode_t mode, const float degrees,
                const stdAc::fanspeed_t fan) {
  ac->begin();
  ac->setPower(on);
  ac->setMode(ac->convertMode(mode));
  ac->setTemp(degrees);
  ac->setFan(ac->convertFan(fan));
  // No Swing setting available.
  // No Quiet setting available.
  // No Light setting available.
  // No Filter setting available.
  // No Turbo setting available.
  // No Economy setting available.
  // No Clean setting available.
  // No Beep setting available.
  // No Sleep setting available.
  ac->send();
}
#endif  // SEND_AMCOR

#if SEND_ARGO
/// Send an Argo A/C message with the supplied settings.
/// @param[in, out] ac A Ptr to an IRArgoAC object to use.
/// @param[in] on The power setting.
/// @param[in] mode The operation mode setting.
/// @param[in] degrees The temperature setting in degrees.
/// @param[in] sensorTemp The room (iFeel) temperature sensor reading in degrees
///                       Celsius.
/// @param[in] fan The speed setting for the fan.
/// @param[in] swingv The vertical swing setting.
/// @param[in] iFeel Whether to enable iFeel (remote temp) mode on the A/C unit.
/// @param[in] turbo Run the device in turbo/powerful mode.
/// @param[in] sleep Nr. of minutes for sleep mode.
/// @note -1 is Off, >= 0 is on.
void IRac::argo(IRArgoAC *ac,
                const bool on, const stdAc::opmode_t mode, const float degrees,
                const float sensorTemp, const stdAc::fanspeed_t fan,
                const stdAc::swingv_t swingv, const bool iFeel,
                const bool turbo, const int16_t sleep) {
  ac->begin();
  ac->setPower(on);
  ac->setMode(ac->convertMode(mode));
  ac->setTemp(static_cast<uint8_t>(roundf(degrees)));
  if (sensorTemp != kNoTempValue) {
    ac->setSensorTemp(static_cast<uint8_t>(roundf(sensorTemp)));
  }
  ac->setiFeel(iFeel);
  ac->setFan(ac->convertFan(fan));
  ac->setFlap(ac->convertSwingV(swingv));
  // No Quiet setting available.
  // No Light setting available.
  // No Filter setting available.
  ac->setMax(turbo);
  // No Economy setting available.
  // No Clean setting available.
  // No Beep setting available.
  ac->setNight(sleep >= 0);  // Convert to a boolean.
  ac->send();
}

/// Send an Argo A/C WREM-3 AC **control** message with the supplied settings.
/// @param[in, out] ac A Ptr to an IRArgoAC_WREM3 object to use.
/// @param[in] on The power setting.
/// @param[in] mode The operation mode setting.
/// @param[in] degrees The set temperature setting in degrees Celsius.
/// @param[in] sensorTemp The room (iFeel) temperature sensor reading in degrees
///                       Celsius.
/// @warning The @c sensorTemp param is assumed to be in 0..255 range (uint8_t)
///          The overflow is *not* checked, though.
/// @note The value is rounded to nearest integer, rounding halfway cases
///       away from zero. E.g. 1.5 [C] becomes 2 [C].
/// @param[in] fan The speed setting for the fan.
/// @param[in] swingv The vertical swing setting.
/// @param[in] iFeel Whether to enable iFeel (remote temp) mode on the A/C unit.
/// @param[in] night Enable night mode (raises temp by +1*C after 1h).
/// @param[in] econo Enable eco mode (limits power consumed).
/// @param[in] turbo Run the device in turbo/powerful mode.
/// @param[in] filter Enable filter mode
/// @param[in] light Enable device display/LEDs
void IRac::argoWrem3_ACCommand(IRArgoAC_WREM3 *ac, const bool on,
    const stdAc::opmode_t mode, const float degrees, const float sensorTemp,
    const stdAc::fanspeed_t fan, const stdAc::swingv_t swingv, const bool iFeel,
    const bool night, const bool econo, const bool turbo, const bool filter,
    const bool light) {
  ac->begin();
  ac->setMessageType(argoIrMessageType_t::AC_CONTROL);
  ac->setPower(on);
  ac->setMode(ac->convertMode(mode));
  ac->setTemp(degrees);
  if (sensorTemp != kNoTempValue) {
    ac->setSensorTemp(static_cast<uint8_t>(roundf(sensorTemp)));
  }
  ac->setiFeel(iFeel);
  ac->setFan(ac->convertFan(fan));
  ac->setFlap(ac->convertSwingV(swingv));
  ac->setNight(night);
  ac->setEco(econo);
  ac->setMax(turbo);
  ac->setFilter(filter);
  ac->setLight(light);
  // No Clean setting available.
  // No Beep setting available - always beeps in this mode :)
  ac->send();
}

/// Send an Argo A/C WREM-3 iFeel (room temp) silent (no beep) report.
/// @param[in, out] ac A Ptr to an IRArgoAC_WREM3 object to use.
/// @param[in] sensorTemp The room (iFeel) temperature setting
///                       in degrees Celsius.
/// @warning The @c sensorTemp param is assumed to be in 0..255 range (uint8_t)
///          The overflow is *not* checked, though.
/// @note The value is rounded to nearest integer, rounding halfway cases
///       away from zero. E.g. 1.5 [C] becomes 2 [C].
void IRac::argoWrem3_iFeelReport(IRArgoAC_WREM3 *ac, const float sensorTemp) {
  ac->begin();
  ac->setMessageType(argoIrMessageType_t::IFEEL_TEMP_REPORT);
  ac->setSensorTemp(static_cast<uint8_t>(roundf(sensorTemp)));
  ac->send();
}

/// Send an Argo A/C WREM-3 Config command.
/// @param[in, out] ac A Ptr to an IRArgoAC_WREM3 object to use.
/// @param[in] param The parameter ID.
/// @param[in] value The parameter value.
/// @param[in] safe If true, will only allow setting the below parameters
///                 in order to avoid accidentally setting a restricted
///                 vendor-specific param and breaking the A/C device
/// @note Known parameters (P<xx>, where xx is the @c param)
///       P05 - Temperature Scale (0-Celsius, 1-Fahrenheit)
///       P06 - Transmission channel (0..3)
///       P12 - ECO mode power input limit (30..99, default: 75)
void IRac::argoWrem3_ConfigSet(IRArgoAC_WREM3 *ac, const uint8_t param,
    const uint8_t value, bool safe /*= true*/) {
  if (safe) {
    switch (param) {
      case 5:  // temp. scale (note this is likely excess as not transmitted)
        if (value > 1) { return;  /* invalid */ }
        break;
      case 6:  // channel (note this is likely excess as not transmitted)
        if (value > 3) { return;  /* invalid */ }
        break;
      case 12:  // eco power limit
        if (value < 30 || value > 99) { return;  /* invalid */ }
        break;
      default:
        return;  /* invalid */
    }
  }
  ac->begin();
  ac->setMessageType(argoIrMessageType_t::CONFIG_PARAM_SET);
  ac->setConfigEntry(param, value);
  ac->send();
}

/// Send an Argo A/C WREM-3 Delay timer command.
/// @param[in, out] ac A Ptr to an IRArgoAC_WREM3 object to use.
/// @param[in] on Whether the unit is currently on. The timer, upon elapse
///               will toggle this state
/// @param[in] currentTime currentTime in minutes, starting from 00:00
/// @note For timer mode, this value is not really used much so can be zero.
/// @param[in] delayMinutes Number of minutes after which the @c on state should
///                         be toggled
/// @note Schedule timers are not exposed via this interface
void IRac::argoWrem3_SetTimer(IRArgoAC_WREM3 *ac, bool on,
    const uint16_t currentTime, const uint16_t delayMinutes) {
  ac->begin();
  ac->setMessageType(argoIrMessageType_t::TIMER_COMMAND);
  ac->setPower(on);
  ac->setTimerType(argoTimerType_t::DELAY_TIMER);
  ac->setCurrentTimeMinutes(currentTime);
  // Note: Day of week is not set (no need)
  ac->setDelayTimerMinutes(delayMinutes);
  ac->send();
}
#endif  // SEND_ARGO

#if SEND_BOSCH144
/// Send a Bosch144 A/C message with the supplied settings.
/// @note May result in multiple messages being sent.
/// @param[in, out] ac A Ptr to an IRBosch144AC object to use.
/// @param[in] on The power setting.
/// @param[in] mode The operation mode setting.
/// @param[in] degrees The temperature setting in degrees.
/// @param[in] fan The speed setting for the fan.
/// @param[in] quiet Run the device in quiet/silent mode.
/// @note -1 is Off, >= 0 is on.
void IRac::bosch144(IRBosch144AC *ac,
                  const bool on, const stdAc::opmode_t mode,
                  const float degrees, const stdAc::fanspeed_t fan,
                  const bool quiet) {
  ac->begin();
  ac->setPower(on);
  if (!on) {
      // after turn off AC no more commands should
      // be accepted
      ac->send();
      return;
  }
  ac->setTemp(degrees);
  ac->setFan(ac->convertFan(fan));
  ac->setMode(ac->convertMode(mode));
  ac->setQuiet(quiet);
  ac->send();  // Send the state, which will also power on the unit.
  // The following are all options/settings that create their own special
  // messages. Often they only make sense to be sent after the unit is turned
  // on. For instance, assuming a person wants to have the a/c on and in turbo
  // mode. If we send the turbo message, it is ignored if the unit is off.
  // Hence we send the special mode/setting messages after a normal message
  // which will turn on the device.
  // No Filter setting available.
  // No Beep setting available.
  // No Clock setting available.
  // No Econo setting available.
  // No Sleep setting available.
}
#endif  // SEND_BOSCH144

#if SEND_CARRIER_AC64
/// Send a Carrier 64-bit A/C message with the supplied settings.
/// @param[in, out] ac A Ptr to an IRCarrierAc64 object to use.
/// @param[in] on The power setting.
/// @param[in] mode The operation mode setting.
/// @param[in] degrees The temperature setting in degrees.
/// @param[in] fan The speed setting for the fan.
/// @param[in] swingv The vertical swing setting.
/// @param[in] sleep Nr. of minutes for sleep mode.
/// @note -1 is Off, >= 0 is on.
void IRac::carrier64(IRCarrierAc64 *ac,
                     const bool on, const stdAc::opmode_t mode,
                     const float degrees, const stdAc::fanspeed_t fan,
                     const stdAc::swingv_t swingv, const int16_t sleep) {
  ac->begin();
  ac->setPower(on);
  ac->setMode(ac->convertMode(mode));
  ac->setTemp(degrees);
  ac->setFan(ac->convertFan(fan));
  ac->setSwingV((int8_t)swingv >= 0);
  // No Quiet setting available.
  // No Light setting available.
  // No Filter setting available.
  // No Turbo setting available.
  // No Economy setting available.
  // No Clean setting available.
  // No Beep setting available.
  ac->setSleep(sleep >= 0);  // Convert to a boolean.
  ac->send();
}
#endif  // SEND_CARRIER_AC64

#if SEND_COOLIX
/// Send a Coolix A/C message with the supplied settings.
/// @note May result in multiple messages being sent.
/// @param[in, out] ac A Ptr to an IRCoolixAC object to use.
/// @param[in] on The power setting.
/// @param[in] mode The operation mode setting.
/// @param[in] degrees The temperature setting in degrees.
/// @param[in] sensorTemp The room (iFeel) temperature sensor reading in degrees
///                       Celsius.
/// @param[in] fan The speed setting for the fan.
/// @param[in] swingv The vertical swing setting.
/// @param[in] swingh The horizontal swing setting.
/// @param[in] iFeel Whether to enable iFeel (remote temp) mode on the A/C unit.
/// @param[in] turbo Run the device in turbo/powerful mode.
/// @param[in] light Turn on the LED/Display mode.
/// @param[in] clean Turn on the self-cleaning mode. e.g. Mould, dry filters etc
/// @param[in] sleep Nr. of minutes for sleep mode.
/// @note -1 is Off, >= 0 is on.
void IRac::coolix(IRCoolixAC *ac,
                  const bool on, const stdAc::opmode_t mode,
                  const float degrees, const float sensorTemp,
                  const stdAc::fanspeed_t fan,
                  const stdAc::swingv_t swingv, const stdAc::swingh_t swingh,
                  const bool iFeel, const bool turbo, const bool light,
                  const bool clean, const int16_t sleep) {
  ac->begin();
  ac->setPower(on);
  if (!on) {
      // after turn off AC no more commands should
      // be accepted
      ac->send();
      return;
  }
  ac->setMode(ac->convertMode(mode));
  ac->setTemp(degrees);
  ac->setFan(ac->convertFan(fan));
  // No Filter setting available.
  // No Beep setting available.
  // No Clock setting available.
  // No Econo setting available.
  // No Quiet setting available.
  if (sensorTemp != kNoTempValue) {
    ac->setSensorTemp(static_cast<uint8_t>(roundf(sensorTemp)));
  } else {
    ac->clearSensorTemp();
  }
  ac->setZoneFollow(iFeel);
  ac->send();  // Send the state, which will also power on the unit.
  // The following are all options/settings that create their own special
  // messages. Often they only make sense to be sent after the unit is turned
  // on. For instance, assuming a person wants to have the a/c on and in turbo
  // mode. If we send the turbo message, it is ignored if the unit is off.
  // Hence we send the special mode/setting messages after a normal message
  // which will turn on the device.
  if (swingv != stdAc::swingv_t::kOff || swingh != stdAc::swingh_t::kOff) {
    // Swing has a special command that needs to be sent independently.
    ac->setSwing();
    ac->send();
  }
  if (turbo) {
    // Turbo has a special command that needs to be sent independently.
    ac->setTurbo();
    ac->send();
  }
  if (sleep >= 0) {
    // Sleep has a special command that needs to be sent independently.
    ac->setSleep();
    ac->send();
  }
  if (light) {
    // Light has a special command that needs to be sent independently.
    ac->setLed();
    ac->send();
  }
  if (clean) {
    // Clean has a special command that needs to be sent independently.
    ac->setClean();
    ac->send();
  }
}
#endif  // SEND_COOLIX

#if SEND_PANASONIC_AC
/// Send a Panasonic A/C message with the supplied settings.
/// @param[in, out] ac A Ptr to an IRPanasonicAc object to use.
/// @param[in] model The A/C model to use.
/// @param[in] on The power setting.
/// @param[in] mode The operation mode setting.
/// @param[in] degrees The temperature setting in degrees.
/// @param[in] fan The speed setting for the fan.
/// @param[in] swingv The vertical swing setting.
/// @param[in] swingh The horizontal swing setting.
/// @param[in] quiet Run the device in quiet/silent mode.
/// @param[in] turbo Run the device in turbo/powerful mode.
/// @param[in] filter Turn on the (ion/pollen/etc) filter mode.
/// @param[in] clock The time in Nr. of mins since midnight. < 0 is ignore.
void IRac::panasonic(IRPanasonicAc *ac, const panasonic_ac_remote_model_t model,
                     const bool on, const stdAc::opmode_t mode,
                     const float degrees, const stdAc::fanspeed_t fan,
                     const stdAc::swingv_t swingv, const stdAc::swingh_t swingh,
                     const bool quiet, const bool turbo, const bool filter,
                     const int16_t clock) {
  ac->begin();
  ac->setModel(model);
  ac->setPower(on);
  ac->setMode(ac->convertMode(mode));
  ac->setTemp(degrees);
  ac->setFan(ac->convertFan(fan));
  ac->setSwingVertical(ac->convertSwingV(swingv));
  ac->setSwingHorizontal(ac->convertSwingH(swingh));
  ac->setQuiet(quiet);
  ac->setPowerful(turbo);
  ac->setIon(filter);
  // No Light setting available.
  // No Econo setting available.
  // No Clean setting available.
  // No Beep setting available.
  // No Sleep setting available.
  if (clock >= 0) ac->setClock(clock);
  ac->send();
}
#endif  // SEND_PANASONIC_AC

#if SEND_PANASONIC_AC32
/// Send a Panasonic A/C message with the supplied settings.
/// @param[in, out] ac A Ptr to an IRPanasonicAc32 object to use.
/// @param[in] on The power setting.
/// @param[in] mode The operation mode setting.
/// @param[in] degrees The temperature setting in degrees.
/// @param[in] fan The speed setting for the fan.
/// @param[in] swingv The vertical swing setting.
/// @param[in] swingh The horizontal swing setting.
void IRac::panasonic32(IRPanasonicAc32 *ac,
                       const bool on, const stdAc::opmode_t mode,
                       const float degrees, const stdAc::fanspeed_t fan,
                       const stdAc::swingv_t swingv,
                       const stdAc::swingh_t swingh) {
  ac->begin();
  ac->setPowerToggle(on);
  ac->setMode(ac->convertMode(mode));
  ac->setTemp(degrees);
  ac->setFan(ac->convertFan(fan));
  ac->setSwingVertical(ac->convertSwingV(swingv));
  ac->setSwingHorizontal(swingh != stdAc::swingh_t::kOff);
  // No Quiet setting available.
  // No Turbo setting available.
  // No Filter setting available.
  // No Light setting available.
  // No Econo setting available.
  // No Clean setting available.
  // No Beep setting available.
  // No Sleep setting available.
  // No Clock setting available.
  ac->send();
}
#endif  // SEND_PANASONIC_AC32

#if SEND_SAMSUNG_AC
/// Send a Samsung A/C message with the supplied settings.
/// @note Multiple IR messages may be generated & sent.
/// @param[in, out] ac A Ptr to an IRSamsungAc object to use.
/// @param[in] on The power setting.
/// @param[in] mode The operation mode setting.
/// @param[in] degrees The temperature setting in degrees.
/// @param[in] fan The speed setting for the fan.
/// @param[in] swingv The vertical swing setting.
/// @param[in] swingh The horizontal swing setting.
/// @param[in] quiet Run the device in quiet/silent mode.
/// @param[in] turbo Run the device in turbo/powerful mode.
/// @param[in] econo Run the device in economical mode.
/// @param[in] light Turn on the LED/Display mode.
/// @param[in] filter Turn on the (ion/pollen/etc) filter mode.
/// @param[in] clean Toggle the self-cleaning mode. e.g. Mould, dry filters etc
/// @param[in] beep Toggle beep setting for receiving IR messages.
/// @param[in] sleep Nr. of minutes for sleep mode. <= 0 is Off, > 0 is on.
/// @param[in] prevpower The power setting from the previous A/C state.
/// @param[in] prevsleep Nr. of minutes for sleep from the previous A/C state.
/// @param[in] forceextended Do we force sending the special extended message?
void IRac::samsung(IRSamsungAc *ac,
                   const bool on, const stdAc::opmode_t mode,
                   const float degrees,
                   const stdAc::fanspeed_t fan,
                   const stdAc::swingv_t swingv, const stdAc::swingh_t swingh,
                   const bool quiet, const bool turbo, const bool econo,
                   const bool light,
                   const bool filter, const bool clean,
                   const bool beep, const int16_t sleep,
                   const bool prevpower, const int16_t prevsleep,
                   const bool forceextended) {
  ac->begin();
  ac->stateReset(forceextended || (sleep != prevsleep), prevpower);
  ac->setPower(on);
  ac->setMode(ac->convertMode(mode));
  ac->setTemp(degrees);
  ac->setFan(ac->convertFan(fan));
  ac->setSwing(swingv != stdAc::swingv_t::kOff);
  ac->setSwingH(swingh != stdAc::swingh_t::kOff);
  ac->setQuiet(quiet);
  ac->setPowerful(turbo);  // FYI, `setEcono(true)` will override this.
  ac->setDisplay(light);
  ac->setEcono(econo);
  ac->setIon(filter);
  ac->setClean(clean);  // Toggle
  ac->setBeep(beep);  // Toggle
  ac->setSleepTimer((sleep <= 0) ? 0 : sleep);
  // No Clock setting available.
  // Do setMode() again as it can affect fan speed.
  ac->setMode(ac->convertMode(mode));
  ac->send();
}
#endif  // SEND_SAMSUNG_AC

#if SEND_SANYO_AC
/// Send a Sanyo A/C message with the supplied settings.
/// @param[in, out] ac A Ptr to an IRSanyoAc object to use.
/// @param[in] on The power setting.
/// @param[in] mode The operation mode setting.
/// @param[in] degrees The temperature setting in degrees.
/// @param[in] sensorTemp The room (iFeel) temperature sensor reading in degrees
///                       Celsius.
/// @param[in] fan The speed setting for the fan.
/// @param[in] swingv The vertical swing setting.
/// @param[in] iFeel Whether to enable iFeel (remote temp) mode on the A/C unit.
/// @param[in] beep Enable/Disable beeps when receiving IR messages.
/// @param[in] sleep Nr. of minutes for sleep mode. -1 is Off, >= 0 is on.
void IRac::sanyo(IRSanyoAc *ac,
                 const bool on, const stdAc::opmode_t mode,
                 const float degrees, const float sensorTemp,
                 const stdAc::fanspeed_t fan, const stdAc::swingv_t swingv,
                 const bool iFeel, const bool beep, const int16_t sleep) {
  ac->begin();
  ac->setPower(on);
  ac->setMode(ac->convertMode(mode));
  ac->setTemp(degrees);
  if (sensorTemp != kNoTempValue) {
    ac->setSensorTemp(static_cast<uint8_t>(roundf(sensorTemp)));
  } else {
    ac->setSensorTemp(degrees);  // Set the sensor temp to the desired
                                 // (normal) temp.
  }
  ac->setSensor(!iFeel);
  ac->setFan(ac->convertFan(fan));
  ac->setSwingV(ac->convertSwingV(swingv));
  // No Horizontal swing setting available.
  // No Quiet setting available.
  // No Turbo setting available.
  // No Econo setting available.
  // No Light setting available.
  // No Filter setting available.
  // No Clean setting available.
  ac->setBeep(beep);
  ac->setSleep(sleep >= 0);  // Sleep is either on/off, so convert to boolean.
  // No Clock setting available.
  ac->send();
}
#endif  // SEND_SANYO_AC

#if SEND_SANYO_AC88
/// Send a Sanyo 88-bit A/C message with the supplied settings.
/// @param[in, out] ac A Ptr to an IRSanyoAc88 object to use.
/// @param[in] on The power setting.
/// @param[in] mode The operation mode setting.
/// @param[in] degrees The temperature setting in degrees.
/// @param[in] fan The speed setting for the fan.
/// @param[in] swingv The vertical swing setting.
/// @param[in] turbo Run the device in turbo/powerful mode.
/// @param[in] filter Turn on the (ion/pollen/etc) filter mode.
/// @param[in] sleep Nr. of minutes for sleep mode. -1 is Off, >= 0 is on.
/// @param[in] clock The time in Nr. of mins since midnight. < 0 is ignore.
void IRac::sanyo88(IRSanyoAc88 *ac,
                   const bool on, const stdAc::opmode_t mode,
                   const float degrees, const stdAc::fanspeed_t fan,
                   const stdAc::swingv_t swingv, const bool turbo,
                   const bool filter, const int16_t sleep,
                   const int16_t clock) {
  ac->begin();
  ac->setPower(on);
  ac->setMode(ac->convertMode(mode));
  ac->setTemp(degrees);
  ac->setFan(ac->convertFan(fan));
  ac->setSwingV(swingv != stdAc::swingv_t::kOff);
  // No Horizontal swing setting available.
  // No Quiet setting available.
  ac->setTurbo(turbo);
  // No Econo setting available.
  // No Light setting available.
  ac->setFilter(filter);
  // No Clean setting available.
  // No Beep setting available.
  ac->setSleep(sleep >= 0);  // Sleep is either on/off, so convert to boolean.
  if (clock >= 0) ac->setClock(clock);
  ac->send();
}
#endif  // SEND_SANYO_AC88

#if SEND_SHARP_AC
/// Send a Sharp A/C message with the supplied settings.
/// @note Multiple IR messages may be generated & sent.
/// @param[in, out] ac A Ptr to an IRSharpAc object to use.
/// @param[in] model The A/C model to use.
/// @param[in] on The power setting.
/// @param[in] prev_power The power setting from the previous A/C state.
/// @param[in] mode The operation mode setting.
/// @param[in] degrees The temperature setting in degrees.
/// @param[in] fan The speed setting for the fan.
/// @param[in] swingv The vertical swing setting.
/// @param[in] swingv_prev The previous vertical swing setting.
/// @param[in] turbo Run the device in turbo/powerful mode.
/// @param[in] light Turn on the LED/Display mode.
/// @param[in] filter Turn on the (ion/pollen/etc) filter mode.
/// @param[in] clean Turn on the self-cleaning mode. e.g. Mould, dry filters etc
void IRac::sharp(IRSharpAc *ac, const sharp_ac_remote_model_t model,
                 const bool on, const bool prev_power,
                 const stdAc::opmode_t mode,
                 const float degrees, const stdAc::fanspeed_t fan,
                 const stdAc::swingv_t swingv,
                 const stdAc::swingv_t swingv_prev, const bool turbo,
                 const bool light, const bool filter, const bool clean) {
  ac->begin();
  ac->setModel(model);
  ac->setMode(ac->convertMode(mode));
  ac->setTemp(degrees);
  ac->setFan(ac->convertFan(fan, model));
  if (swingv != swingv_prev) ac->setSwingV(ac->convertSwingV(swingv));
  // Econo  deliberately not used as it cycles through 3 modes uncontrollably.
  // ac->setEconoToggle(econo);
  ac->setIon(filter);
  // No Horizontal swing setting available.
  // No Quiet setting available.
  ac->setLightToggle(light);
  // No Beep setting available.
  // No Sleep setting available.
  // No Clock setting available.
  // Do setMode() again as it can affect fan speed and temp.
  ac->setMode(ac->convertMode(mode));
  // Clean after mode, as it can affect the mode, temp & fan speed.
  if (clean) {
    // A/C needs to be off before we can enter clean mode.
    ac->setPower(false, prev_power);
    ac->send();
  }
  ac->setClean(clean);
  ac->setPower(on, prev_power);
  if (turbo) {
    ac->send();  // Send the current state.
    // Set up turbo mode as it needs to be sent after everything else.
    ac->setTurbo(true);
  }
  ac->send();
}
#endif  // SEND_SHARP_AC

#if SEND_TCL112AC
/// Send a TCL 112-bit A/C message with the supplied settings.
/// @param[in, out] ac A Ptr to an IRTcl112Ac object to use.
/// @param[in] model The A/C model to use.
/// @param[in] on The power setting.
/// @param[in] mode The operation mode setting.
/// @param[in] degrees The temperature setting in degrees.
/// @param[in] fan The speed setting for the fan.
/// @param[in] swingv The vertical swing setting.
/// @param[in] swingh The horizontal swing setting.
/// @param[in] quiet Run the device in quiet/silent mode.
/// @param[in] turbo Run the device in turbo/powerful mode.
/// @param[in] light Turn on the LED/Display mode.
/// @param[in] econo Run the device in economical mode.
/// @param[in] filter Turn on the (ion/pollen/etc) filter mode.
void IRac::tcl112(IRTcl112Ac *ac, const tcl_ac_remote_model_t model,
                  const bool on, const stdAc::opmode_t mode,
                  const float degrees, const stdAc::fanspeed_t fan,
                  const stdAc::swingv_t swingv, const stdAc::swingh_t swingh,
                  const bool quiet, const bool turbo, const bool light,
                  const bool econo, const bool filter) {
  ac->begin();
  ac->setModel(model);
  ac->setPower(on);
  ac->setMode(ac->convertMode(mode));
  ac->setTemp(degrees);
  ac->setFan(ac->convertFan(fan));
  ac->setSwingVertical(ac->convertSwingV(swingv));
  ac->setSwingHorizontal(swingh != stdAc::swingh_t::kOff);
  ac->setQuiet(quiet);
  ac->setTurbo(turbo);
  ac->setLight(light);
  ac->setEcono(econo);
  ac->setHealth(filter);
  // No Clean setting available.
  // No Beep setting available.
  // No Sleep setting available.
  // No Clock setting available.
  ac->send();
}
#endif  // SEND_TCL112AC

#if SEND_TOSHIBA_AC
/// Send a Toshiba A/C message with the supplied settings.
/// @param[in, out] ac A Ptr to an IRToshibaAC object to use.
/// @param[in] on The power setting.
/// @param[in] mode The operation mode setting.
/// @param[in] degrees The temperature setting in degrees.
/// @param[in] fan The speed setting for the fan.
/// @param[in] swingv The vertical swing setting.
/// @param[in] turbo Run the device in turbo/powerful mode.
/// @param[in] econo Run the device in economical mode.
/// @param[in] filter Turn on the (Pure/ion/pollen/etc) filter mode.
void IRac::toshiba(IRToshibaAC *ac,
                   const bool on, const stdAc::opmode_t mode,
                   const float degrees, const stdAc::fanspeed_t fan,
                   const stdAc::swingv_t swingv,
                   const bool turbo, const bool econo, const bool filter) {
  ac->begin();
  ac->setMode(ac->convertMode(mode));
  ac->setTemp(degrees);
  ac->setFan(ac->convertFan(fan));
  // The API has no "step" option, so off is off, anything else is on.
  ac->setSwing((swingv == stdAc::swingv_t::kOff) ? kToshibaAcSwingOff
                                                 : kToshibaAcSwingOn);
  // No Horizontal swing setting available.
  // No Quiet setting available.
  ac->setTurbo(turbo);
  ac->setEcono(econo);
  // No Light setting available.
  ac->setFilter(filter);
  // No Clean setting available.
  // No Beep setting available.
  // No Sleep setting available.
  // No Clock setting available.
  // Do this last because Toshiba A/C has an odd quirk with how power off works.
  ac->setPower(on);
  ac->send();
}
#endif  // SEND_TOSHIBA_AC

/// Create a new state base on the provided state that has been suitably fixed.
/// @note This is for use with Home Assistant, which requires mode to be off if
///   the power is off.
/// @param[in] state The state_t structure describing the desired a/c state.
/// @return A stdAc::state_t with the needed settings.
stdAc::state_t IRac::cleanState(const stdAc::state_t state) {
  stdAc::state_t result = state;
  // A hack for Home Assistant, it appears to need/want an Off opmode.
  // So enforce the power is off if the mode is also off.
  if (state.mode == stdAc::opmode_t::kOff) result.power = false;
  return result;
}

/// Create a new state base on desired & previous states but handle
/// any state changes for options that need to be toggled.
/// @param[in] desired The state_t structure describing the desired a/c state.
/// @param[in] prev A Ptr to the previous state_t structure.
/// @return A stdAc::state_t with the needed settings.
stdAc::state_t IRac::handleToggles(const stdAc::state_t desired,
                                   const stdAc::state_t *prev) {
  stdAc::state_t result = desired;
  // If we've been given a previous state AND the it's the same A/C basically.
  if (prev != NULL && desired.protocol == prev->protocol &&
      desired.model == prev->model) {
    // Check if we have to handle toggle settings for specific A/C protocols.
    switch (desired.protocol) {
        // FALL THRU
      case decode_type_t::SHARP_AC:
        result.light = desired.light ^ prev->light;
        if ((desired.swingv == stdAc::swingv_t::kOff) ^
            (prev->swingv == stdAc::swingv_t::kOff))  // It changed, so toggle.
          result.swingv = stdAc::swingv_t::kAuto;
        else
          result.swingv = stdAc::swingv_t::kOff;  // No change, so no toggle.
        break;
      case decode_type_t::KELON:
        if ((desired.swingv == stdAc::swingv_t::kOff) ^
            (prev->swingv == stdAc::swingv_t::kOff))  // It changed, so toggle.
          result.swingv = stdAc::swingv_t::kAuto;
        else
          result.swingv = stdAc::swingv_t::kOff;  // No change, so no toggle.
        // FALL-THRU
      case decode_type_t::PANASONIC_AC32:
      case decode_type_t::PANASONIC_AC:
        // CKP models use a power mode toggle.
        if (desired.model == panasonic_ac_remote_model_t::kPanasonicCkp)
          result.power = desired.power ^ prev->power;
        break;
      case decode_type_t::SAMSUNG_AC:
        result.beep = desired.beep ^ prev->beep;
        result.clean = desired.clean ^ prev->clean;
        break;
      default:
        {};
    }
  }
  return result;
}

/// Send A/C message for a given device using common A/C settings.
/// @param[in] vendor The vendor/protocol type.
/// @param[in] model The A/C model if applicable.
/// @param[in] power The power setting.
/// @param[in] mode The operation mode setting.
/// @note Changing mode from "Off" to something else does NOT turn on a device.
/// You need to use `power` for that.
/// @param[in] degrees The temperature setting in degrees.
/// @param[in] celsius Temperature units. True is Celsius, False is Fahrenheit.
/// @param[in] fan The speed setting for the fan.
/// @note The following are all "if supported" by the underlying A/C classes.
/// @param[in] swingv The vertical swing setting.
/// @param[in] swingh The horizontal swing setting.
/// @param[in] quiet Run the device in quiet/silent mode.
/// @param[in] turbo Run the device in turbo/powerful mode.
/// @param[in] econo Run the device in economical mode.
/// @param[in] light Turn on the LED/Display mode.
/// @param[in] filter Turn on the (ion/pollen/etc) filter mode.
/// @param[in] clean Turn on the self-cleaning mode. e.g. Mould, dry filters etc
/// @param[in] beep Enable/Disable beeps when receiving IR messages.
/// @param[in] sleep Nr. of minutes for sleep mode.
///  -1 is Off, >= 0 is on. Some devices it is the nr. of mins to run for.
///  Others it may be the time to enter/exit sleep mode.
///  i.e. Time in Nr. of mins since midnight.
/// @param[in] clock The time in Nr. of mins since midnight. < 0 is ignore.
/// @return True, if accepted/converted/attempted etc. False, if unsupported.
bool IRac::sendAc(const decode_type_t vendor, const int16_t model,
                  const bool power, const stdAc::opmode_t mode,
                  const float degrees, const bool celsius,
                  const stdAc::fanspeed_t fan,
                  const stdAc::swingv_t swingv, const stdAc::swingh_t swingh,
                  const bool quiet, const bool turbo, const bool econo,
                  const bool light, const bool filter, const bool clean,
                  const bool beep, const int16_t sleep, const int16_t clock) {
  stdAc::state_t to_send;
  initState(&to_send, vendor, model, power, mode, degrees, celsius, fan, swingv,
            swingh, quiet, turbo, econo, light, filter, clean, beep, sleep,
            clock);
  return this->sendAc(to_send, &to_send);
}

/// Send A/C message for a given device using state_t structures.
/// @param[in] desired The state_t structure describing the desired new ac state
/// @param[in] prev A Ptr to the state_t structure containing the previous state
/// @note Changing mode from "Off" to something else does NOT turn on a device.
/// You need to use `power` for that.
/// @return True, if accepted/converted/attempted etc. False, if unsupported.
bool IRac::sendAc(const stdAc::state_t desired, const stdAc::state_t *prev) {
  // Convert the temp from Fahrenheit to Celsius if we are not in Celsius mode.
  float degC __attribute__((unused)) =
      desired.celsius ? desired.degrees : fahrenheitToCelsius(desired.degrees);
  // Convert the sensorTemp from Fahrenheit to Celsius if we are not in Celsius
  // mode.
  float sensorTempC __attribute__((unused)) =
      desired.sensorTemperature ? desired.sensorTemperature
          : fahrenheitToCelsius(desired.sensorTemperature);
  // special `state_t` that is required to be sent based on that.
  stdAc::state_t send = this->handleToggles(this->cleanState(desired), prev);
  // Some protocols expect a previous state for power.
  // Construct a pointer-safe previous power state incase prev is NULL/NULLPTR.
#if (SEND_HITACHI_AC1 || SEND_SAMSUNG_AC || SEND_SHARP_AC)
  const bool prev_power = (prev != NULL) ? prev->power : !send.power;
  const int16_t prev_sleep = (prev != NULL) ? prev->sleep : -1;
#endif  // (SEND_HITACHI_AC1 || SEND_SAMSUNG_AC || SEND_SHARP_AC)
#if (SEND_LG || SEND_SHARP_AC)
  const stdAc::swingv_t prev_swingv = (prev != NULL) ? prev->swingv
                                                     : stdAc::swingv_t::kOff;
#endif  // (SEND_LG || SEND_SHARP_AC)
  // Per vendor settings & setup.
  switch (send.protocol) {

#if SEND_PANASONIC_AC
    case PANASONIC_AC:
    {
      IRPanasonicAc ac(_pin, _inverted, _modulation);
      panasonic(&ac, (panasonic_ac_remote_model_t)send.model, send.power,
                send.mode, degC, send.fanspeed, send.swingv, send.swingh,
                send.quiet, send.turbo, send.clock);
      break;
    }
#endif  // SEND_PANASONIC_AC
#if SEND_PANASONIC_AC32
    case PANASONIC_AC32:
    {
      IRPanasonicAc32 ac(_pin, _inverted, _modulation);
      panasonic32(&ac, send.power, send.mode, degC, send.fanspeed,
                  send.swingv, send.swingh);
      break;
    }
#endif  // SEND_PANASONIC_AC32

#if SEND_SAMSUNG_AC
    case SAMSUNG_AC:
    {
      IRSamsungAc ac(_pin, _inverted, _modulation);
      samsung(&ac, send.power, send.mode, degC, send.fanspeed, send.swingv,
              send.swingh, send.quiet, send.turbo, send.econo, send.light,
              send.filter, send.clean, send.beep, send.sleep,
              prev_power, prev_sleep);
      break;
    }
#endif  // SEND_SAMSUNG_AC
#if SEND_SANYO_AC
    case SANYO_AC:
    {
      IRSanyoAc ac(_pin, _inverted, _modulation);
      sanyo(&ac, send.power, send.mode, degC, sensorTempC, send.fanspeed,
            send.swingv, send.iFeel, send.beep, send.sleep);
      break;
    }
#endif  // SEND_SANYO_AC
#if SEND_SANYO_AC88
    case SANYO_AC88:
    {
      IRSanyoAc88 ac(_pin, _inverted, _modulation);
      sanyo88(&ac, send.power, send.mode, degC, send.fanspeed, send.swingv,
              send.turbo, send.filter, send.sleep, send.clock);
      break;
    }
#endif  // SEND_SANYO_AC88
#if SEND_SHARP_AC
    case SHARP_AC:
    {
      IRSharpAc ac(_pin, _inverted, _modulation);
      sharp(&ac, (sharp_ac_remote_model_t)send.model, send.power, prev_power,
            send.mode, degC, send.fanspeed, send.swingv, prev_swingv,
            send.turbo, send.light, send.filter, send.clean);
      break;
    }
#endif  // SEND_SHARP_AC
#if (SEND_TCL112AC || SEND_TEKNOPOINT)
    case TCL112AC:
    case TEKNOPOINT:
    {
      IRTcl112Ac ac(_pin, _inverted, _modulation);
      tcl_ac_remote_model_t model = (tcl_ac_remote_model_t)send.model;
      if (send.protocol == decode_type_t::TEKNOPOINT)
        model = tcl_ac_remote_model_t::GZ055BE1;
      tcl112(&ac, model, send.power, send.mode,
             degC, send.fanspeed, send.swingv, send.swingh, send.quiet,
             send.turbo, send.light, send.econo, send.filter);
      break;
    }
#endif  // (SEND_TCL112AC || SEND_TEKNOPOINT)
#if SEND_TOSHIBA_AC
    case TOSHIBA_AC:
    {
      IRToshibaAC ac(_pin, _inverted, _modulation);
      toshiba(&ac, send.power, send.mode, degC, send.fanspeed, send.swingv,
              send.turbo, send.econo, send.filter);
      break;
    }
#endif  // SEND_TOSHIBA_AC

    default:
      return false;  // Fail, didn't match anything.
  }
  return true;  // Success.
}  // NOLINT(readability/fn_size)

/// Update the previous state to the current one.
void IRac::markAsSent(void) {
  _prev = next;
}

/// Send an A/C message based soley on our internal state.
/// @return True, if accepted/converted/attempted. False, if unsupported.
bool IRac::sendAc(void) {
  bool success = this->sendAc(next, &_prev);
  if (success) this->markAsSent();
  return success;
}

/// Compare two AirCon states.
/// @note The comparison excludes the clock.
/// @param a A state_t to be compared.
/// @param b A state_t to be compared.
/// @return True if they differ, False if they don't.
bool IRac::cmpStates(const stdAc::state_t a, const stdAc::state_t b) {
  return a.protocol != b.protocol || a.model != b.model || a.power != b.power ||
      a.mode != b.mode || a.degrees != b.degrees || a.celsius != b.celsius ||
      a.fanspeed != b.fanspeed || a.swingv != b.swingv ||
      a.swingh != b.swingh || a.quiet != b.quiet || a.turbo != b.turbo ||
      a.econo != b.econo || a.light != b.light || a.filter != b.filter ||
      a.clean != b.clean || a.beep != b.beep || a.sleep != b.sleep ||
      a.command != b.command || a.sensorTemperature != b.sensorTemperature ||
      a.iFeel != b.iFeel;
}

/// Check if the internal state has changed from what was previously sent.
/// @note The comparison excludes the clock.
/// @return True if it has changed, False if not.
bool IRac::hasStateChanged(void) { return cmpStates(next, _prev); }

/// Convert the supplied str into the appropriate enum.
/// @param[in] str A Ptr to a C-style string to be converted.
/// @param[in] def The enum to return if no conversion was possible.
/// @return The equivalent enum.
stdAc::ac_command_t IRac::strToCommandType(const char *str,
                                           const stdAc::ac_command_t def) {
  if (!STRCASECMP(str, kControlCommandStr))
    return stdAc::ac_command_t::kControlCommand;
  else if (!STRCASECMP(str, kIFeelReportStr) ||
           !STRCASECMP(str, kIFeelStr))
    return stdAc::ac_command_t::kSensorTempReport;
  else if (!STRCASECMP(str, kSetTimerCommandStr) ||
           !STRCASECMP(str, kTimerStr))
    return stdAc::ac_command_t::kTimerCommand;
  else if (!STRCASECMP(str, kConfigCommandStr))
    return stdAc::ac_command_t::kConfigCommand;
  else
    return def;
}

/// Convert the supplied str into the appropriate enum.
/// @param[in] str A Ptr to a C-style string to be converted.
/// @param[in] def The enum to return if no conversion was possible.
/// @return The equivalent enum.
stdAc::opmode_t IRac::strToOpmode(const char *str,
                                  const stdAc::opmode_t def) {
  if (!STRCASECMP(str, kAutoStr) ||
      !STRCASECMP(str, kAutomaticStr))
    return stdAc::opmode_t::kAuto;
  else if (!STRCASECMP(str, kOffStr) ||
           !STRCASECMP(str, kStopStr))
    return stdAc::opmode_t::kOff;
  else if (!STRCASECMP(str, kCoolStr) ||
           !STRCASECMP(str, kCoolingStr))
    return stdAc::opmode_t::kCool;
  else if (!STRCASECMP(str, kHeatStr) ||
           !STRCASECMP(str, kHeatingStr))
    return stdAc::opmode_t::kHeat;
  else if (!STRCASECMP(str, kDryStr) ||
           !STRCASECMP(str, kDryingStr) ||
           !STRCASECMP(str, kDehumidifyStr))
    return stdAc::opmode_t::kDry;
  else if (!STRCASECMP(str, kFanStr) ||
          // The following Fans strings with "only" are required to help with
          // HomeAssistant & Google Home Climate integration.
          // For compatibility only.
          // Ref: https://www.home-assistant.io/integrations/google_assistant/#climate-operation-modes
           !STRCASECMP(str, kFanOnlyStr) ||
           !STRCASECMP(str, kFan_OnlyStr) ||
           !STRCASECMP(str, kFanOnlyWithSpaceStr) ||
           !STRCASECMP(str, kFanOnlyNoSpaceStr))
    return stdAc::opmode_t::kFan;
  else
    return def;
}

/// Convert the supplied str into the appropriate enum.
/// @param[in] str A Ptr to a C-style string to be converted.
/// @param[in] def The enum to return if no conversion was possible.
/// @return The equivalent enum.
stdAc::fanspeed_t IRac::strToFanspeed(const char *str,
                                      const stdAc::fanspeed_t def) {
  if (!STRCASECMP(str, kAutoStr) ||
      !STRCASECMP(str, kAutomaticStr))
    return stdAc::fanspeed_t::kAuto;
  else if (!STRCASECMP(str, kMinStr) ||
           !STRCASECMP(str, kMinimumStr) ||
           !STRCASECMP(str, kLowestStr))
    return stdAc::fanspeed_t::kMin;
  else if (!STRCASECMP(str, kLowStr) ||
           !STRCASECMP(str, kLoStr))
    return stdAc::fanspeed_t::kLow;
  else if (!STRCASECMP(str, kMedStr) ||
           !STRCASECMP(str, kMediumStr) ||
           !STRCASECMP(str, kMidStr))
    return stdAc::fanspeed_t::kMedium;
  else if (!STRCASECMP(str, kHighStr) ||
           !STRCASECMP(str, kHiStr))
    return stdAc::fanspeed_t::kHigh;
  else if (!STRCASECMP(str, kMaxStr) ||
           !STRCASECMP(str, kMaximumStr) ||
           !STRCASECMP(str, kHighestStr))
    return stdAc::fanspeed_t::kMax;
  else if (!STRCASECMP(str, kMedHighStr))
    return stdAc::fanspeed_t::kMediumHigh;
  else
    return def;
}

/// Convert the supplied str into the appropriate enum.
/// @param[in] str A Ptr to a C-style string to be converted.
/// @param[in] def The enum to return if no conversion was possible.
/// @return The equivalent enum.
stdAc::swingv_t IRac::strToSwingV(const char *str,
                                  const stdAc::swingv_t def) {
  if (!STRCASECMP(str, kAutoStr) ||
      !STRCASECMP(str, kAutomaticStr) ||
      !STRCASECMP(str, kOnStr) ||
      !STRCASECMP(str, kSwingStr))
    return stdAc::swingv_t::kAuto;
  else if (!STRCASECMP(str, kOffStr) ||
           !STRCASECMP(str, kStopStr))
    return stdAc::swingv_t::kOff;
  else if (!STRCASECMP(str, kMinStr) ||
           !STRCASECMP(str, kMinimumStr) ||
           !STRCASECMP(str, kLowestStr) ||
           !STRCASECMP(str, kBottomStr) ||
           !STRCASECMP(str, kDownStr))
    return stdAc::swingv_t::kLowest;
  else if (!STRCASECMP(str, kLowStr))
    return stdAc::swingv_t::kLow;
  else if (!STRCASECMP(str, kMidStr) ||
           !STRCASECMP(str, kMiddleStr) ||
           !STRCASECMP(str, kMedStr) ||
           !STRCASECMP(str, kMediumStr) ||
           !STRCASECMP(str, kCentreStr))
    return stdAc::swingv_t::kMiddle;
  else if (!STRCASECMP(str, kUpperMiddleStr))
    return stdAc::swingv_t::kUpperMiddle;
  else if (!STRCASECMP(str, kHighStr) ||
           !STRCASECMP(str, kHiStr))
    return stdAc::swingv_t::kHigh;
  else if (!STRCASECMP(str, kHighestStr) ||
           !STRCASECMP(str, kMaxStr) ||
           !STRCASECMP(str, kMaximumStr) ||
           !STRCASECMP(str, kTopStr) ||
           !STRCASECMP(str, kUpStr))
    return stdAc::swingv_t::kHighest;
  else
    return def;
}

/// Convert the supplied str into the appropriate enum.
/// @param[in] str A Ptr to a C-style string to be converted.
/// @param[in] def The enum to return if no conversion was possible.
/// @return The equivalent enum.
stdAc::swingh_t IRac::strToSwingH(const char *str,
                                  const stdAc::swingh_t def) {
  if (!STRCASECMP(str, kAutoStr) ||
      !STRCASECMP(str, kAutomaticStr) ||
      !STRCASECMP(str, kOnStr) || !STRCASECMP(str, kSwingStr))
    return stdAc::swingh_t::kAuto;
  else if (!STRCASECMP(str, kOffStr) ||
           !STRCASECMP(str, kStopStr))
    return stdAc::swingh_t::kOff;
  else if (!STRCASECMP(str, kLeftMaxNoSpaceStr) ||              // "LeftMax"
           !STRCASECMP(str, kLeftMaxStr) ||                     // "Left Max"
           !STRCASECMP(str, kMaxLeftNoSpaceStr) ||              // "MaxLeft"
           !STRCASECMP(str, kMaxLeftStr))                       // "Max Left"
    return stdAc::swingh_t::kLeftMax;
  else if (!STRCASECMP(str, kLeftStr))
    return stdAc::swingh_t::kLeft;
  else if (!STRCASECMP(str, kMidStr) ||
           !STRCASECMP(str, kMiddleStr) ||
           !STRCASECMP(str, kMedStr) ||
           !STRCASECMP(str, kMediumStr) ||
           !STRCASECMP(str, kCentreStr))
    return stdAc::swingh_t::kMiddle;
  else if (!STRCASECMP(str, kRightStr))
    return stdAc::swingh_t::kRight;
  else if (!STRCASECMP(str, kRightMaxNoSpaceStr) ||              // "RightMax"
           !STRCASECMP(str, kRightMaxStr) ||                     // "Right Max"
           !STRCASECMP(str, kMaxRightNoSpaceStr) ||              // "MaxRight"
           !STRCASECMP(str, kMaxRightStr))                       // "Max Right"
    return stdAc::swingh_t::kRightMax;
  else if (!STRCASECMP(str, kWideStr))
    return stdAc::swingh_t::kWide;
  else
    return def;
}

/// Convert the supplied str into the appropriate enum.
/// @note Assumes str is the model code or an integer >= 1.
/// @param[in] str A Ptr to a C-style string to be converted.
/// @param[in] def The enum to return if no conversion was possible.
/// @return The equivalent enum.
/// @note After adding a new model you should update modelToStr() too.
int16_t IRac::strToModel(const char *str, const int16_t def) {
  // Panasonic A/C families
  if (!STRCASECMP(str, kLkeStr) ||
             !STRCASECMP(str, kPanasonicLkeStr)) {
    return panasonic_ac_remote_model_t::kPanasonicLke;
  } else if (!STRCASECMP(str, kNkeStr) ||
             !STRCASECMP(str, kPanasonicNkeStr)) {
    return panasonic_ac_remote_model_t::kPanasonicNke;
  } else if (!STRCASECMP(str, kDkeStr) ||
             !STRCASECMP(str, kPanasonicDkeStr) ||
             !STRCASECMP(str, kPkrStr) ||
             !STRCASECMP(str, kPanasonicPkrStr)) {
    return panasonic_ac_remote_model_t::kPanasonicDke;
  } else if (!STRCASECMP(str, kJkeStr) ||
             !STRCASECMP(str, kPanasonicJkeStr)) {
    return panasonic_ac_remote_model_t::kPanasonicJke;
  } else if (!STRCASECMP(str, kCkpStr) ||
             !STRCASECMP(str, kPanasonicCkpStr)) {
    return panasonic_ac_remote_model_t::kPanasonicCkp;
  } else if (!STRCASECMP(str, kRkrStr) ||
             !STRCASECMP(str, kPanasonicRkrStr)) {
    return panasonic_ac_remote_model_t::kPanasonicRkr;
  // Sharp A/C Models
  } else if (!STRCASECMP(str, kA907Str)) {
    return sharp_ac_remote_model_t::A907;
  } else if (!STRCASECMP(str, kA705Str)) {
    return sharp_ac_remote_model_t::A705;
  } else if (!STRCASECMP(str, kA903Str)) {
    return sharp_ac_remote_model_t::A903;
  // TCL A/C Models
  } else if (!STRCASECMP(str, kTac09chsdStr)) {
    return tcl_ac_remote_model_t::TAC09CHSD;
  } else if (!STRCASECMP(str, kGz055be1Str)) {
    return tcl_ac_remote_model_t::GZ055BE1;
  } else {
    int16_t number = atoi(str);
    if (number > 0)
      return number;
    else
      return def;
  }
}

/// Convert the supplied str into the appropriate boolean value.
/// @param[in] str A Ptr to a C-style string to be converted.
/// @param[in] def The boolean value to return if no conversion was possible.
/// @return The equivalent boolean value.
bool IRac::strToBool(const char *str, const bool def) {
  if (!STRCASECMP(str, kOnStr) ||
      !STRCASECMP(str, k1Str) ||
      !STRCASECMP(str, kYesStr) ||
      !STRCASECMP(str, kTrueStr))
    return true;
  else if (!STRCASECMP(str, kOffStr) ||
           !STRCASECMP(str, k0Str) ||
           !STRCASECMP(str, kNoStr) ||
           !STRCASECMP(str, kFalseStr))
    return false;
  else
    return def;
}

/// Convert the supplied boolean into the appropriate String.
/// @param[in] value The boolean value to be converted.
/// @return The equivalent String for the locale.
String IRac::boolToString(const bool value) {
  return value ? kOnStr : kOffStr;
}

/// Convert the supplied operation mode into the appropriate String.
/// @param[in] cmdType The enum to be converted.
/// @return The equivalent String for the locale.
String IRac::commandTypeToString(const stdAc::ac_command_t cmdType) {
  switch (cmdType) {
    case stdAc::ac_command_t::kControlCommand:    return kControlCommandStr;
    case stdAc::ac_command_t::kSensorTempReport: return kIFeelReportStr;
    case stdAc::ac_command_t::kTimerCommand:      return kSetTimerCommandStr;
    case stdAc::ac_command_t::kConfigCommand:     return kConfigCommandStr;
    default:                                      return kUnknownStr;
  }
}

/// Convert the supplied operation mode into the appropriate String.
/// @param[in] mode The enum to be converted.
/// @param[in] ha A flag to indicate we want GoogleHome/HomeAssistant output.
/// @return The equivalent String for the locale.
String IRac::opmodeToString(const stdAc::opmode_t mode, const bool ha) {
  switch (mode) {
    case stdAc::opmode_t::kOff:  return kOffStr;
    case stdAc::opmode_t::kAuto: return kAutoStr;
    case stdAc::opmode_t::kCool: return kCoolStr;
    case stdAc::opmode_t::kHeat: return kHeatStr;
    case stdAc::opmode_t::kDry:  return kDryStr;
    case stdAc::opmode_t::kFan:  return ha ? kFan_OnlyStr : kFanStr;
    default:                     return kUnknownStr;
  }
}

/// Convert the supplied fan speed enum into the appropriate String.
/// @param[in] speed The enum to be converted.
/// @return The equivalent String for the locale.
String IRac::fanspeedToString(const stdAc::fanspeed_t speed) {
  switch (speed) {
    case stdAc::fanspeed_t::kAuto:       return kAutoStr;
    case stdAc::fanspeed_t::kMax:        return kMaxStr;
    case stdAc::fanspeed_t::kHigh:       return kHighStr;
    case stdAc::fanspeed_t::kMedium:     return kMediumStr;
    case stdAc::fanspeed_t::kMediumHigh: return kMedHighStr;
    case stdAc::fanspeed_t::kLow:        return kLowStr;
    case stdAc::fanspeed_t::kMin:        return kMinStr;
    default:                             return kUnknownStr;
  }
}

/// Convert the supplied enum into the appropriate String.
/// @param[in] swingv The enum to be converted.
/// @return The equivalent String for the locale.
String IRac::swingvToString(const stdAc::swingv_t swingv) {
  switch (swingv) {
    case stdAc::swingv_t::kOff:          return kOffStr;
    case stdAc::swingv_t::kAuto:         return kAutoStr;
    case stdAc::swingv_t::kHighest:      return kHighestStr;
    case stdAc::swingv_t::kHigh:         return kHighStr;
    case stdAc::swingv_t::kMiddle:       return kMiddleStr;
    case stdAc::swingv_t::kUpperMiddle:  return kUpperMiddleStr;
    case stdAc::swingv_t::kLow:          return kLowStr;
    case stdAc::swingv_t::kLowest:       return kLowestStr;
    default:                             return kUnknownStr;
  }
}

/// Convert the supplied enum into the appropriate String.
/// @param[in] swingh The enum to be converted.
/// @return The equivalent String for the locale.
String IRac::swinghToString(const stdAc::swingh_t swingh) {
  switch (swingh) {
    case stdAc::swingh_t::kOff:      return kOffStr;
    case stdAc::swingh_t::kAuto:     return kAutoStr;
    case stdAc::swingh_t::kLeftMax:  return kLeftMaxStr;
    case stdAc::swingh_t::kLeft:     return kLeftStr;
    case stdAc::swingh_t::kMiddle:   return kMiddleStr;
    case stdAc::swingh_t::kRight:    return kRightStr;
    case stdAc::swingh_t::kRightMax: return kRightMaxStr;
    case stdAc::swingh_t::kWide:     return kWideStr;
    default:                         return kUnknownStr;
  }
}

namespace IRAcUtils {
  /// Display the human readable state of an A/C message if we can.
  /// @param[in] result A Ptr to the captured `decode_results` that contains an
  ///   A/C mesg.
  /// @return A string with the human description of the A/C message.
  ///   An empty string if we can't.
  String resultAcToString(const decode_results * const result) {
    switch (result->decode_type) {
#if DECODE_PANASONIC_AC32
      case decode_type_t::PANASONIC_AC32: {
        if (result->bits >= kPanasonicAc32Bits) {
          IRPanasonicAc32 ac(kGpioUnused);
          ac.setRaw(result->value);  // Uses value instead of state.
          return ac.toString();
        }
        return "";
      }
#endif  // DECODE_PANASONIC_AC
#if DECODE_SAMSUNG_AC
      case decode_type_t::SAMSUNG_AC: {
        IRSamsungAc ac(kGpioUnused);
        ac.setRaw(result->state, result->bits / 8);
        return ac.toString();
      }
#endif  // DECODE_SAMSUNG_AC
#if DECODE_SANYO_AC
      case decode_type_t::SANYO_AC: {
        IRSanyoAc ac(kGpioUnused);
        ac.setRaw(result->state);
        return ac.toString();
      }
#endif  // DECODE_SANYO_AC
#if DECODE_SANYO_AC88
      case decode_type_t::SANYO_AC88: {
        IRSanyoAc88 ac(kGpioUnused);
        ac.setRaw(result->state);
        return ac.toString();
      }
#endif  // DECODE_SANYO_AC88
#if DECODE_SHARP_AC
      case decode_type_t::SHARP_AC: {
        IRSharpAc ac(kGpioUnused);
        ac.setRaw(result->state);
        return ac.toString();
      }
#endif  // DECODE_SHARP_AC
#if (DECODE_TCL112AC || DECODE_TEKNOPOINT)
      case decode_type_t::TCL112AC:
      case decode_type_t::TEKNOPOINT: {
        IRTcl112Ac ac(kGpioUnused);
        ac.setRaw(result->state);
        return ac.toString();
      }
#endif  // (DECODE_TCL112AC || DECODE_TEKNOPOINT)
#if DECODE_TOSHIBA_AC
      case decode_type_t::TOSHIBA_AC: {
        IRToshibaAC ac(kGpioUnused);
        ac.setRaw(result->state, result->bits / 8);
        return ac.toString();
      }
#endif  // DECODE_TOSHIBA_AC
      default:
        return "";
    }
  }

  /// Convert a valid IR A/C remote message that we understand enough into a
  /// Common A/C state.
  /// @param[in] decode A PTR to a successful raw IR decode object.
  /// @param[in] result A PTR to a state structure to store the result in.
  /// @param[in] prev A PTR to a state structure which has the prev. state.
  /// @return A boolean indicating success or failure.
  bool decodeToState(const decode_results *decode, stdAc::state_t *result,
                     const stdAc::state_t *prev
/// @cond IGNORE
// *prev flagged as "unused" due to potential compiler warning when some
// protocols that use it are disabled. It really is used.
                                                __attribute__((unused))
/// @endcond
                    ) {
    if (decode == NULL || result == NULL) return false;  // Safety check.
    switch (decode->decode_type) {
#if DECODE_PANASONIC_AC
      case decode_type_t::PANASONIC_AC: {
        IRPanasonicAc ac(kGpioUnused);
        ac.setRaw(decode->state);
        *result = ac.toCommon();
        break;
      }
#endif  // DECODE_PANASONIC_AC
#if DECODE_PANASONIC_AC32
      case decode_type_t::PANASONIC_AC32: {
        IRPanasonicAc32 ac(kGpioUnused);
        if (decode->bits >= kPanasonicAc32Bits) {
          ac.setRaw(decode->value);  // Uses value instead of state.
          *result = ac.toCommon(prev);
        } else {
          return false;
        }
        break;
      }
#endif  // DECODE_PANASONIC_AC32
#if DECODE_SAMSUNG_AC
      case decode_type_t::SAMSUNG_AC: {
        IRSamsungAc ac(kGpioUnused);
        ac.setRaw(decode->state, decode->bits / 8);
        *result = ac.toCommon();
        break;
      }
#endif  // DECODE_SAMSUNG_AC
#if DECODE_SANYO_AC
      case decode_type_t::SANYO_AC: {
        IRSanyoAc ac(kGpioUnused);
        ac.setRaw(decode->state);
        *result = ac.toCommon();
        break;
      }
#endif  // DECODE_SANYO_AC
#if DECODE_SANYO_AC88
      case decode_type_t::SANYO_AC88: {
        IRSanyoAc88 ac(kGpioUnused);
        ac.setRaw(decode->state);
        *result = ac.toCommon();
        break;
      }
#endif  // DECODE_SANYO_AC88
#if DECODE_SHARP_AC
      case decode_type_t::SHARP_AC: {
        IRSharpAc ac(kGpioUnused);
        ac.setRaw(decode->state);
        *result = ac.toCommon(prev);
        break;
      }
#endif  // DECODE_SHARP_AC
#if (DECODE_TCL112AC || DECODE_TEKNOPOINT)
      case decode_type_t::TCL112AC:
      case decode_type_t::TEKNOPOINT: {
        IRTcl112Ac ac(kGpioUnused);
        ac.setRaw(decode->state);
        *result = ac.toCommon(prev);
        // Teknopoint uses the TCL protocol, but with a different model number.
        // Just keep the original protocol type ... for now.
        result->protocol = decode->decode_type;
        break;
      }
#endif  // (DECODE_TCL112AC || DECODE_TEKNOPOINT)
#if DECODE_TOSHIBA_AC
      case decode_type_t::TOSHIBA_AC: {
        IRToshibaAC ac(kGpioUnused);
        ac.setRaw(decode->state, decode->bits / 8);
        *result = ac.toCommon(prev);
        break;
      }
#endif  // DECODE_TOSHIBA_AC
      default:
        return false;
    }
    return true;
  }
}  // namespace IRAcUtils
