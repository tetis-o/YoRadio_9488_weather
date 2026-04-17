// Copyright 2009 Ken Shirriff
// Copyright 2015 Mark Szabo
// Copyright 2017,2019 David Conran

#include "IRsend.h"
#ifndef UNIT_TEST
#include <Arduino.h>
#else
#define __STDC_LIMIT_MACROS
#include <stdint.h>
#endif
#include <algorithm>
#ifdef UNIT_TEST
#include <cmath>
#endif
#include "IRtimer.h"

/// Constructor for an IRsend object.
/// @param[in] IRsendPin Which GPIO pin to use when sending an IR command.
/// @param[in] inverted Optional flag to invert the output. (default = false)
///  e.g. LED is illuminated when GPIO is LOW rather than HIGH.
/// @warning Setting `inverted` to something other than the default could
///  easily destroy your IR LED if you are overdriving it.
///  Unless you *REALLY* know what you are doing, don't change this.
/// @param[in] use_modulation Do we do frequency modulation during transmission?
///  i.e. If not, assume a 100% duty cycle. Ignore attempts to change the
///  duty cycle etc.
IRsend::IRsend(uint16_t IRsendPin, bool inverted, bool use_modulation)
    : IRpin(IRsendPin), periodOffset(kPeriodOffset) {
  if (inverted) {
    outputOn = LOW;
    outputOff = HIGH;
  } else {
    outputOn = HIGH;
    outputOff = LOW;
  }
  modulation = use_modulation;
  if (modulation)
    _dutycycle = kDutyDefault;
  else
    _dutycycle = kDutyMax;
}

/// Enable the pin for output.
void IRsend::begin() {
#ifndef UNIT_TEST
  pinMode(IRpin, OUTPUT);
#endif
  ledOff();  // Ensure the LED is in a known safe state when we start.
}

/// Turn off the IR LED.
void IRsend::ledOff() {
#ifndef UNIT_TEST
  digitalWrite(IRpin, outputOff);
#endif
}

/// Turn on the IR LED.
void IRsend::ledOn() {
#ifndef UNIT_TEST
  digitalWrite(IRpin, outputOn);
#endif
}

/// Calculate the period for a given frequency.
/// @param[in] hz Frequency in Hz.
/// @param[in] use_offset Should we use the calculated offset or not?
/// @return nr. of uSeconds.
/// @note (T = 1/f)
uint32_t IRsend::calcUSecPeriod(uint32_t hz, bool use_offset) {
  if (hz == 0) hz = 1;  // Avoid Zero hz. Divide by Zero is nasty.
  uint32_t period =
      (1000000UL + hz / 2) / hz;  // The equiv of round(1000000/hz).
  // Apply the offset and ensure we don't result in a <= 0 value.
  if (use_offset)
    return std::max((uint32_t)1, period + periodOffset);
  else
    return std::max((uint32_t)1, period);
}

/// Set the output frequency modulation and duty cycle.
/// @param[in] freq The freq we want to modulate at.
///  Assumes < 1000 means kHz else Hz.
/// @param[in] duty Percentage duty cycle of the LED.
///   e.g. 25 = 25% = 1/4 on, 3/4 off.
///   If you are not sure, try 50 percent.
///   This is ignored if modulation is disabled at object instantiation.
/// @note Integer timing functions & math mean we can't do fractions of
///  microseconds timing. Thus minor changes to the freq & duty values may have
///  limited effect. You've been warned.
void IRsend::enableIROut(uint32_t freq, uint8_t duty) {
  // Set the duty cycle to use if we want freq. modulation.
  if (modulation) {
    _dutycycle = std::min(duty, kDutyMax);
  } else {
    _dutycycle = kDutyMax;
  }
  if (freq < 1000)  // Were we given kHz? Supports the old call usage.
    freq *= 1000;
#ifdef UNIT_TEST
  _freq_unittest = freq;
#endif  // UNIT_TEST
  uint32_t period = calcUSecPeriod(freq);
  // Nr. of uSeconds the LED will be on per pulse.
  onTimePeriod = (period * _dutycycle) / kDutyMax;
  // Nr. of uSeconds the LED will be off per pulse.
  offTimePeriod = period - onTimePeriod;
}

#if ALLOW_DELAY_CALLS
/// An ESP8266 RTOS watch-dog timer friendly version of delayMicroseconds().
/// @param[in] usec Nr. of uSeconds to delay for.
void IRsend::_delayMicroseconds(uint32_t usec) {
  // delayMicroseconds() is only accurate to 16383us.
  // Ref: https://www.arduino.cc/en/Reference/delayMicroseconds
  if (usec <= kMaxAccurateUsecDelay) {
#ifndef UNIT_TEST
    delayMicroseconds(usec);
#endif
  } else {
#ifndef UNIT_TEST
    // Invoke a delay(), where possible, to avoid triggering the WDT.
    delay(usec / 1000UL);  // Delay for as many whole milliseconds as we can.
    // Delay the remaining sub-millisecond.
    delayMicroseconds(static_cast<uint16_t>(usec % 1000UL));
#endif
  }
}
#else  // ALLOW_DELAY_CALLS
/// A version of delayMicroseconds() that handles large values and does NOT use
/// the watch-dog friendly delay() calls where appropriate.
/// @note Use this only if you know what you are doing as it may cause the WDT
///  to reset the ESP8266.
void IRsend::_delayMicroseconds(uint32_t usec) {
  for (; usec > kMaxAccurateUsecDelay; usec -= kMaxAccurateUsecDelay)
#ifndef UNIT_TEST
    delayMicroseconds(kMaxAccurateUsecDelay);
  delayMicroseconds(static_cast<uint16_t>(usec));
#endif  // UNIT_TEST
}
#endif  // ALLOW_DELAY_CALLS

/// Modulate the IR LED for the given period (usec) and at the duty cycle set.
/// @param[in] usec The period of time to modulate the IR LED for, in
///  microseconds.
/// @return Nr. of pulses actually sent.
/// @note
///   The ESP8266 has no good way to do hardware PWM, so we have to do it all
///   in software. There is a horrible kludge/brilliant hack to use the second
///   serial TX line to do fairly accurate hardware PWM, but it is only
///   available on a single specific GPIO and only available on some modules.
///   e.g. It's not available on the ESP-01 module.
///   Hence, for greater compatibility & choice, we don't use that method.
/// Ref:
///   https://www.analysir.com/blog/2017/01/29/updated-esp8266-nodemcu-backdoor-upwm-hack-for-ir-signals/
uint16_t IRsend::mark(uint16_t usec) {
  // Handle the simple case of no required frequency modulation.
  if (!modulation || _dutycycle >= 100) {
    ledOn();
    _delayMicroseconds(usec);
    ledOff();
    return 1;
  }

  // Not simple, so do it assuming frequency modulation.
  uint16_t counter = 0;
  IRtimer usecTimer = IRtimer();
  // Cache the time taken so far. This saves us calling time, and we can be
  // assured that we can't have odd math problems. i.e. unsigned under/overflow.
  uint32_t elapsed = usecTimer.elapsed();

  while (elapsed < usec) {  // Loop until we've met/exceeded our required time.
    ledOn();
    // Calculate how long we should pulse on for.
    // e.g. Are we to close to the end of our requested mark time (usec)?
    _delayMicroseconds(std::min((uint32_t)onTimePeriod, usec - elapsed));
    ledOff();
    counter++;
    if (elapsed + onTimePeriod >= usec)
      return counter;  // LED is now off & we've passed our allotted time.
    // Wait for the lesser of the rest of the duty cycle, or the time remaining.
    _delayMicroseconds(
        std::min(usec - elapsed - onTimePeriod, (uint32_t)offTimePeriod));
    elapsed = usecTimer.elapsed();  // Update & recache the actual elapsed time.
  }
  return counter;
}

/// Turn the pin (LED) off for a given time.
/// Sends an IR space for the specified number of microseconds.
/// A space is no output, so the PWM output is disabled.
/// @param[in] time Time in microseconds (us).
void IRsend::space(uint32_t time) {
  ledOff();
  if (time == 0) return;
  _delayMicroseconds(time);
}

/// Calculate & set any offsets to account for execution times during sending.
///
/// @param[in] hz The frequency to calibrate at >= 1000Hz. Default is 38000Hz.
/// @return The calculated period offset (in uSeconds) which is now in use.
///  e.g. -5.
/// @note This will generate an 65535us mark() IR LED signal.
///  This only needs to be called once, if at all.
int8_t IRsend::calibrate(uint16_t hz) {
  if (hz < 1000)  // Were we given kHz? Supports the old call usage.
    hz *= 1000;
  periodOffset = 0;  // Turn off any existing offset while we calibrate.
  enableIROut(hz);
  IRtimer usecTimer = IRtimer();  // Start a timer *just* before we do the call.
  uint16_t pulses = mark(UINT16_MAX);  // Generate a PWM of 65,535 us. (Max.)
  uint32_t timeTaken = usecTimer.elapsed();  // Record the time it took.
  // While it shouldn't be necessary, assume at least 1 pulse, to avoid a
  // divide by 0 situation.
  pulses = std::max(pulses, (uint16_t)1U);
  uint32_t calcPeriod = calcUSecPeriod(hz);  // e.g. @38kHz it should be 26us.
  // Assuming 38kHz for the example calculations:
  // In a 65535us pulse, we should have 2520.5769 pulses @ 26us periods.
  // e.g. 65535.0us / 26us = 2520.5769
  // This should have caused approx 2520 loops through the main loop in mark().
  // The average over that many interations should give us a reasonable
  // approximation at what offset we need to use to account for instruction
  // execution times.
  //
  // Calculate the actual period from the actual time & the actual pulses
  // generated.
  double_t actualPeriod = (double_t)timeTaken / (double_t)pulses;
  // Store the difference between the actual time per period vs. calculated.
  periodOffset = (int8_t)((double_t)calcPeriod - actualPeriod);
  return periodOffset;
}

/// Generic method for sending data that is common to most protocols.
/// Will send leading or trailing 0's if the nbits is larger than the number
/// of bits in data.
/// @param[in] onemark Nr. of usecs for the led to be pulsed for a '1' bit.
/// @param[in] onespace Nr. of usecs for the led to be fully off for a '1' bit.
/// @param[in] zeromark Nr. of usecs for the led to be pulsed for a '0' bit.
/// @param[in] zerospace Nr. of usecs for the led to be fully off for a '0' bit.
/// @param[in] data The data to be transmitted.
/// @param[in] nbits Nr. of bits of data to be sent.
/// @param[in] MSBfirst Flag for bit transmission order.
///   Defaults to MSB->LSB order.
void IRsend::sendData(uint16_t onemark, uint32_t onespace, uint16_t zeromark,
                      uint32_t zerospace, uint64_t data, uint16_t nbits,
                      bool MSBfirst) {
  if (nbits == 0)  // If we are asked to send nothing, just return.
    return;
  if (MSBfirst) {  // Send the MSB first.
    // Send 0's until we get down to a bit size we can actually manage.
    while (nbits > sizeof(data) * 8) {
      mark(zeromark);
      space(zerospace);
      nbits--;
    }
    // Send the supplied data.
    for (uint64_t mask = 1ULL << (nbits - 1); mask; mask >>= 1)
      if (data & mask) {  // Send a 1
        mark(onemark);
        space(onespace);
      } else {  // Send a 0
        mark(zeromark);
        space(zerospace);
      }
  } else {  // Send the Least Significant Bit (LSB) first / MSB last.
    for (uint16_t bit = 0; bit < nbits; bit++, data >>= 1)
      if (data & 1) {  // Send a 1
        mark(onemark);
        space(onespace);
      } else {  // Send a 0
        mark(zeromark);
        space(zerospace);
      }
  }
}

/// Generic method for sending simple protocol messages.
/// Will send leading or trailing 0's if the nbits is larger than the number
/// of bits in data.
/// @param[in] headermark Nr. of usecs for the led to be pulsed for the header
///   mark. A value of 0 means no header mark.
/// @param[in] headerspace Nr. of usecs for the led to be off after the header
///   mark. A value of 0 means no header space.
/// @param[in] onemark Nr. of usecs for the led to be pulsed for a '1' bit.
/// @param[in] onespace Nr. of usecs for the led to be fully off for a '1' bit.
/// @param[in] zeromark Nr. of usecs for the led to be pulsed for a '0' bit.
/// @param[in] zerospace Nr. of usecs for the led to be fully off for a '0' bit.
/// @param[in] footermark Nr. of usecs for the led to be pulsed for the footer
///   mark. A value of 0 means no footer mark.
/// @param[in] gap Nr. of usecs for the led to be off after the footer mark.
///   This is effectively the gap between messages.
///   A value of 0 means no gap space.
/// @param[in] data The data to be transmitted.
/// @param[in] nbits Nr. of bits of data to be sent.
/// @param[in] frequency The frequency we want to modulate at. (Hz/kHz)
/// @param[in] MSBfirst Flag for bit transmission order.
///   Defaults to MSB->LSB order.
/// @param[in] repeat Nr. of extra times the message will be sent.
///   e.g. 0 = 1 message sent, 1 = 1 initial + 1 repeat = 2 messages
/// @param[in] dutycycle Percentage duty cycle of the LED.
///   e.g. 25 = 25% = 1/4 on, 3/4 off.
///   If you are not sure, try 50 percent.
/// @note Assumes a frequency < 1000 means kHz otherwise it is in Hz.
///   Most common value is 38000 or 38, for 38kHz.
void IRsend::sendGeneric(const uint16_t headermark, const uint32_t headerspace,
                         const uint16_t onemark, const uint32_t onespace,
                         const uint16_t zeromark, const uint32_t zerospace,
                         const uint16_t footermark, const uint32_t gap,
                         const uint64_t data, const uint16_t nbits,
                         const uint16_t frequency, const bool MSBfirst,
                         const uint16_t repeat, const uint8_t dutycycle) {
  sendGeneric(headermark, headerspace, onemark, onespace, zeromark, zerospace,
              footermark, gap, 0U, data, nbits, frequency, MSBfirst, repeat,
              dutycycle);
}

/// Generic method for sending simple protocol messages.
/// Will send leading or trailing 0's if the nbits is larger than the number
/// of bits in data.
/// @param[in] headermark Nr. of usecs for the led to be pulsed for the header
///   mark. A value of 0 means no header mark.
/// @param[in] headerspace Nr. of usecs for the led to be off after the header
///   mark. A value of 0 means no header space.
/// @param[in] onemark Nr. of usecs for the led to be pulsed for a '1' bit.
/// @param[in] onespace Nr. of usecs for the led to be fully off for a '1' bit.
/// @param[in] zeromark Nr. of usecs for the led to be pulsed for a '0' bit.
/// @param[in] zerospace Nr. of usecs for the led to be fully off for a '0' bit.
/// @param[in] footermark Nr. of usecs for the led to be pulsed for the footer
///   mark. A value of 0 means no footer mark.
/// @param[in] gap Nr. of usecs for the led to be off after the footer mark.
///   This is effectively the gap between messages.
///   A value of 0 means no gap space.
/// @param[in] mesgtime Min. nr. of usecs a single message needs to be.
///   This is effectively the min. total length of a single message.
/// @param[in] data The data to be transmitted.
/// @param[in] nbits Nr. of bits of data to be sent.
/// @param[in] frequency The frequency we want to modulate at. (Hz/kHz)
/// @param[in] MSBfirst Flag for bit transmission order.
///   Defaults to MSB->LSB order.
/// @param[in] repeat Nr. of extra times the message will be sent.
///   e.g. 0 = 1 message sent, 1 = 1 initial + 1 repeat = 2 messages
/// @param[in] dutycycle Percentage duty cycle of the LED.
///   e.g. 25 = 25% = 1/4 on, 3/4 off.
///   If you are not sure, try 50 percent.
/// @note Assumes a frequency < 1000 means kHz otherwise it is in Hz.
///   Most common value is 38000 or 38, for 38kHz.
void IRsend::sendGeneric(const uint16_t headermark, const uint32_t headerspace,
                         const uint16_t onemark, const uint32_t onespace,
                         const uint16_t zeromark, const uint32_t zerospace,
                         const uint16_t footermark, const uint32_t gap,
                         const uint32_t mesgtime, const uint64_t data,
                         const uint16_t nbits, const uint16_t frequency,
                         const bool MSBfirst, const uint16_t repeat,
                         const uint8_t dutycycle) {
  // Setup
  enableIROut(frequency, dutycycle);
  IRtimer usecs = IRtimer();

  // We always send a message, even for repeat=0, hence '<= repeat'.
  for (uint16_t r = 0; r <= repeat; r++) {
    usecs.reset();

    // Header
    if (headermark) mark(headermark);
    if (headerspace) space(headerspace);

    // Data
    sendData(onemark, onespace, zeromark, zerospace, data, nbits, MSBfirst);

    // Footer
    if (footermark) mark(footermark);
    uint32_t elapsed = usecs.elapsed();
    // Avoid potential unsigned integer underflow. e.g. when mesgtime is 0.
    if (elapsed >= mesgtime)
      space(gap);
    else
      space(std::max(gap, mesgtime - elapsed));
  }
}

/// Generic method for sending simple protocol messages.
/// @param[in] headermark Nr. of usecs for the led to be pulsed for the header
///   mark. A value of 0 means no header mark.
/// @param[in] headerspace Nr. of usecs for the led to be off after the header
///   mark. A value of 0 means no header space.
/// @param[in] onemark Nr. of usecs for the led to be pulsed for a '1' bit.
/// @param[in] onespace Nr. of usecs for the led to be fully off for a '1' bit.
/// @param[in] zeromark Nr. of usecs for the led to be pulsed for a '0' bit.
/// @param[in] zerospace Nr. of usecs for the led to be fully off for a '0' bit.
/// @param[in] footermark Nr. of usecs for the led to be pulsed for the footer
///   mark. A value of 0 means no footer mark.
/// @param[in] gap Nr. of usecs for the led to be off after the footer mark.
///   This is effectively the gap between messages.
///   A value of 0 means no gap space.
/// @param[in] dataptr Pointer to the data to be transmitted.
/// @param[in] nbytes Nr. of bytes of data to be sent.
/// @param[in] frequency The frequency we want to modulate at. (Hz/kHz)
/// @param[in] MSBfirst Flag for bit transmission order.
///   Defaults to MSB->LSB order.
/// @param[in] repeat Nr. of extra times the message will be sent.
///   e.g. 0 = 1 message sent, 1 = 1 initial + 1 repeat = 2 messages
/// @param[in] dutycycle Percentage duty cycle of the LED.
///   e.g. 25 = 25% = 1/4 on, 3/4 off.
///   If you are not sure, try 50 percent.
/// @note Assumes a frequency < 1000 means kHz otherwise it is in Hz.
///   Most common value is 38000 or 38, for 38kHz.
void IRsend::sendGeneric(const uint16_t headermark, const uint32_t headerspace,
                         const uint16_t onemark, const uint32_t onespace,
                         const uint16_t zeromark, const uint32_t zerospace,
                         const uint16_t footermark, const uint32_t gap,
                         const uint8_t *dataptr, const uint16_t nbytes,
                         const uint16_t frequency, const bool MSBfirst,
                         const uint16_t repeat, const uint8_t dutycycle) {
  // Setup
  enableIROut(frequency, dutycycle);
  // We always send a message, even for repeat=0, hence '<= repeat'.
  for (uint16_t r = 0; r <= repeat; r++) {
    // Header
    if (headermark) mark(headermark);
    if (headerspace) space(headerspace);

    // Data
    for (uint16_t i = 0; i < nbytes; i++)
      sendData(onemark, onespace, zeromark, zerospace, *(dataptr + i), 8,
               MSBfirst);

    // Footer
    if (footermark) mark(footermark);
    space(gap);
  }
}

/// Generic method for sending Manchester code data.
/// Will send leading or trailing 0's if the nbits is larger than the number
/// of bits in data.
/// @param[in] half_period Nr. of uSeconds for half the clock's period.
///   (1/2 wavelength)
/// @param[in] data The data to be transmitted.
/// @param[in] nbits Nr. of bits of data to be sent.
/// @param[in] MSBfirst Flag for bit transmission order.
///   Defaults to MSB->LSB order.
/// @param[in] GEThomas Use G.E. Thomas (true/default) or IEEE 802.3 (false).
void IRsend::sendManchesterData(const uint16_t half_period,
                                const uint64_t data,
                                const uint16_t nbits, const bool MSBfirst,
                                const bool GEThomas) {
  if (nbits == 0) return;  // Nothing to send.
  uint16_t bits = nbits;
  uint64_t copy = (GEThomas) ? data : ~data;

  if (MSBfirst) {  // Send the MSB first.
    // Send 0's until we get down to a bit size we can actually manage.
    if (bits > (sizeof(data) * 8)) {
      sendManchesterData(half_period, 0ULL, bits - sizeof(data) * 8, MSBfirst,
                         GEThomas);
      bits = sizeof(data) * 8;
    }
    // Send the supplied data.
    for (uint64_t mask = 1ULL << (bits - 1); mask; mask >>= 1)
      if (copy & mask) {
        mark(half_period);
        space(half_period);
      } else {
        space(half_period);
        mark(half_period);
      }
  } else {  // Send the Least Significant Bit (LSB) first / MSB last.
    for (bits = 0; bits < nbits; bits++, copy >>= 1)
      if (copy & 1) {
        mark(half_period);
        space(half_period);
      } else {
        space(half_period);
        mark(half_period);
      }
  }
}

/// Generic method for sending Manchester code messages.
/// Will send leading or trailing 0's if the nbits is larger than the number
/// @param[in] headermark Nr. of usecs for the led to be pulsed for the header
///   mark. A value of 0 means no header mark.
/// @param[in] headerspace Nr. of usecs for the led to be off after the header
///   mark. A value of 0 means no header space.
/// @param[in] half_period Nr. of uSeconds for half the clock's period.
///   (1/2 wavelength)
/// @param[in] footermark Nr. of usecs for the led to be pulsed for the footer
///   mark. A value of 0 means no footer mark.
/// @param[in] gap Min. nr. of usecs for the led to be off after the footer
///   mark. This is effectively the absolute minimum gap between messages.
/// @param[in] data The data to be transmitted.
/// @param[in] nbits Nr. of bits of data to be sent.
/// @param[in] frequency The frequency we want to modulate at. (Hz/kHz)
/// @param[in] MSBfirst Flag for bit transmission order.
///   Defaults to MSB->LSB order.
/// @param[in] repeat Nr. of extra times the message will be sent.
///   e.g. 0 = 1 message sent, 1 = 1 initial + 1 repeat = 2 messages
/// @param[in] dutycycle Percentage duty cycle of the LED.
///   e.g. 25 = 25% = 1/4 on, 3/4 off.
///   If you are not sure, try 50 percent.
/// @param[in] GEThomas Use G.E. Thomas (true/default) or IEEE 802.3 (false).
/// @note Assumes a frequency < 1000 means kHz otherwise it is in Hz.
///   Most common value is 38000 or 38, for 38kHz.
void IRsend::sendManchester(const uint16_t headermark,
                            const uint32_t headerspace,
                            const uint16_t half_period,
                            const uint16_t footermark, const uint32_t gap,
                            const uint64_t data, const uint16_t nbits,
                            const uint16_t frequency, const bool MSBfirst,
                            const uint16_t repeat, const uint8_t dutycycle,
                            const bool GEThomas) {
  // Setup
  enableIROut(frequency, dutycycle);

  // We always send a message, even for repeat=0, hence '<= repeat'.
  for (uint16_t r = 0; r <= repeat; r++) {
    // Header
    if (headermark) mark(headermark);
    if (headerspace) space(headerspace);
    // Data
    sendManchesterData(half_period, data, nbits, MSBfirst, GEThomas);
    // Footer
    if (footermark) mark(footermark);
    if (gap) space(gap);
  }
}

#if SEND_RAW
/// Send a raw IRremote message.
///
/// @param[in] buf An array of uint16_t's that has microseconds elements.
/// @param[in] len Nr. of elements in the buf[] array.
/// @param[in] hz Frequency to send the message at. (kHz < 1000; Hz >= 1000)
/// @note Even elements are Mark times (On), Odd elements are Space times (Off).
/// Ref:
///   examples/IRrecvDumpV2/IRrecvDumpV2.ino (or later)
void IRsend::sendRaw(const uint16_t buf[], const uint16_t len,
                     const uint16_t hz) {
  // Set IR carrier frequency
  enableIROut(hz);
  for (uint16_t i = 0; i < len; i++) {
    if (i & 1) {  // Odd bit.
      space(buf[i]);
    } else {  // Even bit.
      mark(buf[i]);
    }
  }
  ledOff();  // We potentially have ended with a mark(), so turn of the LED.
}
#endif  // SEND_RAW

/// Get the minimum number of repeats for a given protocol.
/// @param[in] protocol Protocol number/type of the message you want to send.
/// @return The number of repeats required.
uint16_t IRsend::minRepeats(const decode_type_t protocol) {
  switch (protocol) {
    // Single repeats
    case TOSHIBA_AC:
    // Special
    case SANYO_AC88:
      return kSanyoAc88MinRepeat;
    case SONY:
      return kSonyMinRepeat;
    case SONY_38K:
      return kSonyMinRepeat + 1;
    default:
      return kNoRepeat;
  }
}

/// Get the default number of bits for a given protocol.
/// @param[in] protocol Protocol number/type you want the default bit size for.
/// @return The number of bits.
uint16_t IRsend::defaultBits(const decode_type_t protocol) {
  switch (protocol) {
    case MULTIBRACKETS:
    case SHARP:
      return 15;
    case SONY:
    case SONY_38K:
      return 20;
    case NEC:
    case NEC_LIKE:
    case PANASONIC_AC32:
    case SAMSUNG:
    case SAMSUNG36:
      return 36;
    case SANYO_LC7461:
      return kSanyoLC7461Bits;  // 42
    case PANASONIC:
      return 48;
    case PANASONIC_AC:
      return kPanasonicAcBits;
    case SAMSUNG_AC:
      return kSamsungAcBits;
    case SANYO_AC:
      return kSanyoAcBits;
    case SANYO_AC88:
      return kSanyoAc88Bits;
    case SANYO_AC152:
      return kSanyoAc152Bits;
    case SHARP_AC:
      return kSharpAcBits;
    case TCL96AC:
      return kTcl96AcBits;
    case TCL112AC:
      return kTcl112AcBits;
    case TOSHIBA_AC:
      return kToshibaACBits;
    // No default amount of bits.
    default:
      return 0;
  }
}

/// Send a simple (up to 64 bits) IR message of a given type.
/// An unknown/unsupported type will send nothing.
/// @param[in] type Protocol number/type of the message you want to send.
/// @param[in] data The data you want to send (up to 64 bits).
/// @param[in] nbits How many bits long the message is to be.
/// @param[in] repeat How many repeats to do?
/// @return True if it is a type we can attempt to send, false if not.
bool IRsend::send(const decode_type_t type, const uint64_t data,
                  const uint16_t nbits, const uint16_t repeat) {
  uint16_t min_repeat __attribute__((unused)) =
      std::max(IRsend::minRepeats(type), repeat);
  switch (type) {
#if SEND_NEC
    case NEC:
    case NEC_LIKE:
      sendNEC(data, nbits, min_repeat);
      break;
#endif
#if SEND_PANASONIC
    case PANASONIC:
      sendPanasonic64(data, nbits, min_repeat);
      break;
#endif  // SEND_PANASONIC
#if SEND_PANASONIC_AC32
    case PANASONIC_AC32:
      sendPanasonicAC32(data, nbits, min_repeat);
      break;
#endif  // SEND_PANASONIC_AC32
#if SEND_SAMSUNG
    case SAMSUNG:
      sendSAMSUNG(data, nbits, min_repeat);
      break;
#endif
#if SEND_SAMSUNG36
    case SAMSUNG36:
      sendSamsung36(data, nbits, min_repeat);
      break;
#endif
#if SEND_SANYO
    case SANYO_LC7461:
      sendSanyoLC7461(data, nbits, min_repeat);
      break;
#endif
#if SEND_SHARP
    case SHARP:
      sendSharpRaw(data, nbits, min_repeat);
      break;
#endif
#if SEND_SONY
    case SONY:
      sendSony(data, nbits, min_repeat);
      break;
    case SONY_38K:
      sendSony38(data, nbits, min_repeat);
      break;
#endif
    default:
      return false;
  }
  return true;
}

/// Send a complex (>= 64 bits) IR message of a given type.
/// An unknown/unsupported type will send nothing.
/// @param[in] type Protocol number/type of the message you want to send.
/// @param[in] state A pointer to the array of bytes that make up the state[].
/// @param[in] nbytes How many bytes are in the state.
/// @return True if it is a type we can attempt to send, false if not.
bool IRsend::send(const decode_type_t type, const uint8_t *state,
                  const uint16_t nbytes) {
  switch (type) {
#if SEND_PANASONIC_AC
    case PANASONIC_AC:
      sendPanasonicAC(state, nbytes);
      break;
#endif  // SEND_PANASONIC_AC
#if SEND_SAMSUNG_AC
    case SAMSUNG_AC:
      sendSamsungAC(state, nbytes);
      break;
#endif  // SEND_SAMSUNG_AC
#if SEND_SANYO_AC
    case SANYO_AC:
      sendSanyoAc(state, nbytes);
      break;
#endif  // SEND_SANYO_AC
#if SEND_SANYO_AC88
    case SANYO_AC88:
      sendSanyoAc88(state, nbytes);
      break;
#endif  // SEND_SANYO_AC88
#if SEND_SANYO_AC152
    case SANYO_AC152:
      sendSanyoAc152(state, nbytes);
      break;
#endif  // SEND_SANYO_AC152
#if SEND_SHARP_AC
    case SHARP_AC:
      sendSharpAc(state, nbytes);
      break;
#endif  // SEND_SHARP_AC
#if SEND_TCL96AC
    case TCL96AC:
      sendTcl96Ac(state, nbytes);
      break;
#endif  // SEND_TCL96AC
#if SEND_TCL112AC
    case TCL112AC:
      sendTcl112Ac(state, nbytes);
      break;
#endif  // SEND_TCL112AC
#if SEND_TOSHIBA_AC
    case TOSHIBA_AC:
      sendToshibaAC(state, nbytes);
      break;
#endif  // SEND_TOSHIBA_AC
    default:
      return false;
  }
  return true;
}
