#pragma once

#include <Arduino.h>
#include <avr/interrupt.h>
#include <util/delay.h>

#if !defined(__AVR_ATmega2560__) || F_CPU != 16000000UL
#error "This wiring and clock implementation requires a 16 MHz Arduino Mega 2560."
#endif

namespace via {

enum Register : uint8_t {
  ORB = 0, ORA = 1, DDR_B = 2, DDR_A = 3,
  T1CL = 4, T1CH = 5, T1LL = 6, T1LH = 7,
  T2CL = 8, T2CH = 9, SR = 10, ACR = 11,
  PCR = 12, IFR = 13, IER = 14, ORA_NO_HANDSHAKE = 15
};

enum Line : uint8_t { CA1 = 38, CA2 = 39, CB1 = 40, CB2 = 41 };

constexpr uint8_t RESET = _BV(6);
constexpr uint8_t CS2 = _BV(5);
constexpr uint8_t READ = _BV(4);
constexpr uint8_t IDLE = RESET | CS2 | READ;
constexpr uint32_t CLOCK_HZ = 100000UL;

inline __attribute__((always_inline)) bool clockHigh() {
  return (PINB & _BV(5)) != 0;
}

inline __attribute__((always_inline)) void nextLowPhase() {
  while (!clockHigh()) {}
  while (clockHigh()) {}
}

inline void releasePeripheralDrivers() {
  DDRK = 0;
  DDRC = 0;
  PORTK = 0;
  PORTC = 0;
  for (uint8_t pin = CA1; pin <= CB2; ++pin) {
    pinMode(pin, INPUT);
    digitalWrite(pin, LOW);
  }
}

inline void holdReset() {
  // Assert reset before releasing any test-side peripheral drivers.
  PORTL = CS2 | READ;
  DDRA = 0;
  PORTA = 0;
  releasePeripheralDrivers();
}

inline void begin() {
  PORTL = CS2 | READ;
  DDRL = 0x7F;
  holdReset();
  pinMode(2, INPUT);  // IRQ requires the external 4.7k pull-up.

  const uint8_t saved = SREG;
  cli();
  TCCR1A = 0;
  TCCR1B = 0;
  TIMSK1 = 0;
  TCNT1 = 0;
  OCR1A = F_CPU / (2UL * CLOCK_HZ) - 1;
  PORTB &= ~_BV(5);
  DDRB |= _BV(5);
  TCCR1A = _BV(COM1A0);
  TCCR1B = _BV(WGM12) | _BV(CS10);
  SREG = saved;
}

inline void reset() {
  holdReset();
  digitalWrite(CA1, HIGH);
  pinMode(CA1, OUTPUT);
  delayMicroseconds(500);
  PORTL = IDLE;
  delayMicroseconds(100);
}

inline uint8_t read(Register reg) {
  const uint8_t saved = SREG;
  cli();
  nextLowPhase();
  PORTL = RESET | READ | static_cast<uint8_t>(reg);
  while (!clockHigh()) {}
  _delay_us(2);
  const uint8_t value = PINA;
  while (clockHigh()) {}
  PORTL = IDLE;
  SREG = saved;
  return value;
}

inline void write(Register reg, uint8_t value) {
  const uint8_t saved = SREG;
  cli();
  nextLowPhase();
  PORTA = value;
  DDRA = 0xFF;
  PORTL = RESET | static_cast<uint8_t>(reg);
  while (!clockHigh()) {}
  while (clockHigh()) {}
  // Hold write data beyond the falling edge before removing chip select.
  _delay_us(1);
  PORTL = IDLE;
  DDRA = 0;
  PORTA = 0;
  SREG = saved;
}

inline void outputLine(Line line, bool high) {
  digitalWrite(line, high ? HIGH : LOW);
  pinMode(line, OUTPUT);
  delayMicroseconds(40);
}

inline void inputLine(Line line) {
  pinMode(line, INPUT);
  digitalWrite(line, LOW);
}

inline void driveLine(Line line, bool high) {
  volatile uint8_t* output;
  uint8_t mask;
  switch (line) {
    case CA1: output = &PORTD; mask = _BV(7); break;
    case CA2: output = &PORTG; mask = _BV(2); break;
    case CB1: output = &PORTG; mask = _BV(1); break;
    case CB2: output = &PORTG; mask = _BV(0); break;
    default:
      Serial.println(F("ERROR: invalid control line"));
      return;
  }
  const uint8_t value = high ? (*output | mask) : (*output & ~mask);
  const uint8_t saved = SREG;
  cli();
  nextLowPhase();
  // External SR edges must stay away from PHI2 falling edges on an NMOS VIA.
  _delay_us(2);
  *output = value;
  SREG = saved;
  delayMicroseconds(40);
}

inline bool lineHigh(Line line) {
  return digitalRead(line) == HIGH;
}

inline bool irqHigh() {
  return (PINE & _BV(4)) != 0;
}

}  // namespace via
