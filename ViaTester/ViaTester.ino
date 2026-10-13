#include "ViaBus.h"

using namespace via;

static uint16_t checks = 0;
static uint16_t failures = 0;
static uint8_t failedGroups = 0;
static const uint8_t patterns[] = {
  0x00, 0xFF, 0x55, 0xAA, 0x01, 0x02, 0x04,
  0x08, 0x10, 0x20, 0x40, 0x80
};

static void expect(const __FlashStringHelper* label, uint8_t actual,
                   uint8_t expected, uint8_t mask = 0xFF) {
  ++checks;
  if ((actual & mask) == (expected & mask)) {
    return;
  }
  ++failures;
  Serial.print(F("  FAIL "));
  Serial.print(label);
  Serial.print(F(": got 0x"));
  Serial.print(actual, HEX);
  Serial.print(F(", expected 0x"));
  Serial.print(expected, HEX);
  Serial.print(F(", mask 0x"));
  Serial.println(mask, HEX);
}

static void require(const __FlashStringHelper* label, bool condition) {
  expect(label, condition ? 1 : 0, 1);
}

static bool setDirection(Register reg, uint8_t value) {
  via::write(reg, value);
  const uint8_t actual = via::read(reg);
  expect(F("DDR verified before peripheral stimulus"), actual, value);
  if (actual != value) {
    Serial.println(F("  Stopping group: unsafe to drive pins with incorrect DDR."));
    return false;
  }
  return true;
}

static bool waitFlag(uint8_t flag, uint32_t timeoutUs) {
  const uint32_t start = micros();
  do {
    if (via::read(IFR) & flag) {
      return true;
    }
  } while (static_cast<uint32_t>(micros() - start) < timeoutUs);
  return false;
}

static void loadTimer(bool timer1, uint16_t count) {
  via::write(timer1 ? T1CL : T2CL, static_cast<uint8_t>(count));
  via::write(timer1 ? T1CH : T2CH, static_cast<uint8_t>(count >> 8));
}

static uint16_t counter(bool timer1) {
  const Register low = timer1 ? T1CL : T2CL;
  const Register high = timer1 ? T1CH : T2CH;
  const uint8_t firstHigh = via::read(high);
  uint8_t lowByte = via::read(low);
  const uint8_t secondHigh = via::read(high);
  if (firstHigh != secondHigh) {
    lowByte = via::read(low);
  }
  return (static_cast<uint16_t>(secondHigh) << 8) | lowByte;
}

static void resetDefaults() {
  expect(F("DDRA reset"), via::read(DDR_A), 0);
  expect(F("DDRB reset"), via::read(DDR_B), 0);
  expect(F("ACR reset"), via::read(ACR), 0);
  expect(F("PCR reset"), via::read(PCR), 0);
  expect(F("IER reset"), via::read(IER), 0, 0x7F);
  expect(F("IFR reset"), via::read(IFR), 0);
  require(F("IRQ released after reset"), irqHigh());
}

static void registerAccess() {
  const Register registers[] = {DDR_A, DDR_B, ACR, PCR};
  for (Register reg : registers) {
    for (uint8_t value : patterns) {
      via::write(reg, value);
      expect(F("register readback"), via::read(reg), value);
    }
    via::write(reg, 0);
  }
  // Address-line aliasing is detected by retaining different values together.
  via::write(DDR_A, 0x12);
  via::write(DDR_B, 0x34);
  via::write(ACR, 0x00);
  via::write(PCR, 0x56);
  via::write(SR, 0x78);
  via::write(T1LL, 0x9A);
  via::write(T1LH, 0xBC);
  expect(F("DDRA distinct address"), via::read(DDR_A), 0x12);
  expect(F("DDRB distinct address"), via::read(DDR_B), 0x34);
  expect(F("ACR distinct address"), via::read(ACR), 0x00);
  expect(F("PCR distinct address"), via::read(PCR), 0x56);
  expect(F("SR disabled readback"), via::read(SR), 0x78);
  expect(F("T1 low latch"), via::read(T1LL), 0x9A);
  expect(F("T1 high latch"), via::read(T1LH), 0xBC);
}

static void enableRegister() {
  via::write(IER, 0x7F);
  for (uint8_t bit = 1; bit < 0x80; bit <<= 1) {
    via::write(IER, 0x80 | bit);
    expect(F("IER set one bit"), via::read(IER), bit, 0x7F);
    via::write(IER, bit);
    expect(F("IER clear one bit"), via::read(IER), 0, 0x7F);
  }
  via::write(IER, 0xD5);
  via::write(IER, 0xAA);
  expect(F("IER accumulate bits"), via::read(IER), 0x7F, 0x7F);
  via::write(IER, 0x2A);
  expect(F("IER selective clear"), via::read(IER), 0x55, 0x7F);
}

static void portOutputs(bool portA) {
  const Register direction = portA ? DDR_A : DDR_B;
  const Register data = portA ? ORA_NO_HANDSHAKE : ORB;
  if (!setDirection(direction, 0xFF)) {
    return;
  }
  for (uint16_t value = 0; value < 256; ++value) {
    via::write(data, static_cast<uint8_t>(value));
    delayMicroseconds(10);
    const uint8_t pins = portA ? PINK : PINC;
    const uint8_t readback = via::read(data);
    expect(F("output physical pins"), pins, static_cast<uint8_t>(value));
    expect(F("output register read"), readback, static_cast<uint8_t>(value));
  }
}

static void portInputs(bool portA) {
  const Register direction = portA ? DDR_A : DDR_B;
  const Register data = portA ? ORA_NO_HANDSHAKE : ORB;
  volatile uint8_t* output = portA ? &PORTK : &PORTC;
  volatile uint8_t* ddr = portA ? &DDRK : &DDRC;
  if (!setDirection(direction, 0)) {
    return;
  }
  *output = 0;
  *ddr = 0xFF;
  for (uint16_t value = 0; value < 256; ++value) {
    *output = static_cast<uint8_t>(value);
    delayMicroseconds(20);
    expect(F("input register"), via::read(data), static_cast<uint8_t>(value));
  }
  *ddr = 0;
  *output = 0;
}

static void mixedPorts(bool portA) {
  const Register direction = portA ? DDR_A : DDR_B;
  const Register data = portA ? ORA_NO_HANDSHAKE : ORB;
  volatile uint8_t* output = portA ? &PORTK : &PORTC;
  volatile uint8_t* ddr = portA ? &DDRK : &DDRC;
  const uint8_t masks[] = {0x55, 0xAA, 0x0F, 0xF0, 0x01, 0x80};
  for (uint8_t mask : masks) {
    *ddr = 0;
    *output = 0;
    if (!setDirection(direction, 0)) {
      return;
    }
    via::write(data, 0xA5);
    if (!setDirection(direction, mask)) {
      return;
    }
    for (uint8_t value : patterns) {
      *output = value & ~mask;
      *ddr = static_cast<uint8_t>(~mask);
      delayMicroseconds(20);
      const uint8_t expected = (0xA5 & mask) | (value & ~mask);
      const uint8_t pins = portA ? PINK : PINC;
      expect(F("mixed direction pins"), pins, expected);
      expect(F("mixed direction read"), via::read(data), expected);
    }
  }
  *ddr = 0;
  *output = 0;
}

static void latchedInput(bool portA) {
  const Line strobe = portA ? CA1 : CB1;
  const Register data = portA ? ORA_NO_HANDSHAKE : ORB;
  volatile uint8_t* output = portA ? &PORTK : &PORTC;
  volatile uint8_t* ddr = portA ? &DDRK : &DDRC;
  if (!setDirection(portA ? DDR_A : DDR_B, 0)) {
    return;
  }
  outputLine(strobe, true);
  via::write(ACR, portA ? 0x01 : 0x02);
  *output = 0x5A;
  *ddr = 0xFF;
  delayMicroseconds(40);
  driveLine(strobe, false);
  *output = 0xA5;
  delayMicroseconds(40);
  expect(F("input latch retains snapshot"), via::read(data), 0x5A);
  driveLine(strobe, true);
  driveLine(strobe, false);
  expect(F("input latch captures next edge"), via::read(data), 0xA5);
  *ddr = 0;
  *output = 0;
}

static void interruptInput(Line line, uint8_t flag, uint8_t positivePcr,
                           Register clearRegister) {
  for (uint8_t positive = 0; positive < 2; ++positive) {
    outputLine(line, positive == 0);
    via::write(PCR, positive ? positivePcr : 0);
    via::write(IER, 0x7F);
    via::write(IFR, 0x7F);
    driveLine(line, positive != 0);
    const uint8_t pending = via::read(IFR);
    const bool maskedIrq = irqHigh();
    via::write(IER, 0x80 | flag);
    const uint8_t enabled = via::read(IFR);
    const bool enabledIrq = irqHigh();
    via::write(IER, flag);
    const uint8_t disabled = via::read(IFR);
    const bool disabledIrq = irqHigh();
    via::read(clearRegister);
    const uint8_t cleared = via::read(IFR);

    expect(F("edge sets flag while masked"), pending, flag, flag | 0x80);
    require(F("masked IRQ high"), maskedIrq);
    expect(F("enabled IFR summary"), enabled, flag | 0x80, flag | 0x80);
    require(F("enabled IRQ low"), !enabledIrq);
    expect(F("disabled flag retained"), disabled, flag, flag | 0x80);
    require(F("disabled IRQ high"), disabledIrq);
    expect(F("port access clears flag"), cleared, 0, flag);
  }
}

static void ca1Interrupt() { interruptInput(CA1, 0x02, 0x01, ORA); }
static void ca2Interrupt() { interruptInput(CA2, 0x01, 0x04, ORA); }
static void cb1Interrupt() { interruptInput(CB1, 0x10, 0x10, ORB); }
static void cb2Interrupt() { interruptInput(CB2, 0x08, 0x40, ORB); }

static void independentInterrupts() {
  outputLine(CA2, true);
  outputLine(CB2, true);
  via::write(PCR, 0x22);
  via::write(IFR, 0x7F);
  driveLine(CA2, false);
  driveLine(CB2, false);
  via::read(ORA);
  via::read(ORB);
  expect(F("independent flags survive port access"), via::read(IFR), 0x09, 0x09);
  via::write(IFR, 0x01);
  expect(F("IFR selective write-one clear"), via::read(IFR), 0x08, 0x09);
  via::write(IFR, 0x08);
  expect(F("IFR second flag clear"), via::read(IFR), 0, 0x09);
}

static void noHandshakeAlias() {
  via::write(IFR, 0x7F);
  driveLine(CA1, false);
  via::read(ORA_NO_HANDSHAKE);
  via::write(ORA_NO_HANDSHAKE, 0xA5);
  expect(F("address 15 preserves CA1 flag"), via::read(IFR), 0x02, 0x02);
  via::read(ORA);
  expect(F("address 1 clears CA1 flag"), via::read(IFR), 0, 0x02);
  via::write(DDR_A, 0xFF);
  via::write(ORA, 0x5A);
  expect(F("address 15 aliases ORA data"), via::read(ORA_NO_HANDSHAKE), 0x5A);
}

static void controlOutputs() {
  via::write(PCR, 0xCC);
  require(F("CA2 manual low"), !lineHigh(CA2));
  require(F("CB2 manual low"), !lineHigh(CB2));
  via::write(PCR, 0xEE);
  require(F("CA2 manual high"), lineHigh(CA2));
  require(F("CB2 manual high"), lineHigh(CB2));

  outputLine(CB1, true);
  via::write(PCR, 0x88);
  require(F("CA2 handshake idle"), lineHigh(CA2));
  require(F("CB2 handshake idle"), lineHigh(CB2));
  via::write(ORA_NO_HANDSHAKE, 0x55);
  require(F("address 15 does not handshake"), lineHigh(CA2));
  via::write(ORA, 0x55);
  via::write(ORB, 0xAA);
  require(F("CA2 handshake asserted"), !lineHigh(CA2));
  require(F("CB2 handshake asserted"), !lineHigh(CB2));
  driveLine(CA1, false);
  driveLine(CB1, false);
  require(F("CA1 acknowledges CA2"), lineHigh(CA2));
  require(F("CB1 acknowledges CB2"), lineHigh(CB2));
}

static void timerOneShot(bool timer1) {
  const uint8_t flag = timer1 ? 0x40 : 0x20;
  const Register low = timer1 ? T1CL : T2CL;
  via::write(IER, 0x80 | flag);
  loadTimer(timer1, 10000);
  const uint16_t first = counter(timer1);
  delayMicroseconds(200);
  const uint16_t second = counter(timer1);
  require(F("timer load/readback range"), first <= 10000 && first >= 9950);
  require(F("timer decrements"), second < first && first - second < 100);

  loadTimer(timer1, 200);
  const uint32_t start = micros();
  delayMicroseconds(500);
  const bool early = (via::read(IFR) & flag) != 0;
  const bool expired = waitFlag(flag, 10000);
  const uint32_t elapsed = micros() - start;
  const uint8_t pending = via::read(IFR);
  const bool asserted = !irqHigh();
  via::read(low);
  const uint8_t cleared = via::read(IFR);
  const bool released = irqHigh();
  delayMicroseconds(5000);
  const uint8_t later = via::read(IFR);

  require(F("timer not expired early"), !early);
  require(F("timer bounded timeout"), expired);
  require(F("timer coarse 2ms interval"), elapsed >= 1500 && elapsed <= 3500);
  expect(F("timer IFR summary"), pending, flag | 0x80, flag | 0x80);
  require(F("timer IRQ asserted"), asserted);
  expect(F("counter low read clears flag"), cleared, 0, flag | 0x80);
  require(F("counter low read releases IRQ"), released);
  expect(F("one-shot does not immediately repeat"), later, 0, flag);

  loadTimer(timer1, 50);
  const bool rearmed = waitFlag(flag, 5000);
  loadTimer(timer1, 200);
  const uint8_t reloadFlags = via::read(IFR);
  require(F("timer can rearm"), rearmed);
  expect(F("counter high write clears flag"), reloadFlags, 0, flag);
}

static void timer1OneShot() { timerOneShot(true); }
static void timer2OneShot() { timerOneShot(false); }

static void timer1FreeRun() {
  via::write(DDR_B, 0x80);
  via::write(ORB, 0xFF);
  via::write(ACR, 0xC0);
  loadTimer(true, 100);
  const bool initialLow = (PINC & 0x80) == 0;
  const bool firstTimeout = waitFlag(0x40, 5000);
  const bool firstHigh = (PINC & 0x80) != 0;
  via::write(IFR, 0x40);
  const bool secondTimeout = waitFlag(0x40, 5000);
  const bool secondLow = (PINC & 0x80) == 0;
  require(F("PB7 timer overrides ORB initially"), initialLow);
  require(F("T1 first free-run timeout"), firstTimeout);
  require(F("PB7 toggles high"), firstHigh);
  require(F("T1 second free-run timeout"), secondTimeout);
  require(F("PB7 toggles low"), secondLow);
}

static void timer2PulseCount() {
  if (!setDirection(DDR_B, 0)) {
    return;
  }
  PORTC = 0x40;
  DDRC = 0x40;
  via::write(ACR, 0x20);
  loadTimer(false, 10);
  delayMicroseconds(500);
  const uint16_t before = counter(false);
  for (uint8_t pulse = 0; pulse < 3; ++pulse) {
    PORTC &= ~_BV(6);
    delayMicroseconds(40);
    PORTC |= _BV(6);
    delayMicroseconds(40);
  }
  const uint16_t after = counter(false);
  const uint8_t earlyFlags = via::read(IFR);
  for (uint8_t pulse = 0; pulse < 16; ++pulse) {
    PORTC &= ~_BV(6);
    delayMicroseconds(40);
    PORTC |= _BV(6);
    delayMicroseconds(40);
  }
  const uint8_t expiredFlags = via::read(IFR);
  via::read(T2CL);
  const uint8_t cleared = via::read(IFR);
  require(F("T2 pulse count initially stationary"), before == 10);
  require(F("T2 counts three falling PB6 edges"), after == 7);
  expect(F("T2 no early pulse timeout"), earlyFlags, 0, 0x20);
  expect(F("T2 pulse timeout"), expiredFlags, 0x20, 0x20);
  expect(F("T2 pulse flag clears"), cleared, 0, 0x20);
  DDRC = 0;
  PORTC = 0;
}

static void shiftExternalInput() {
  outputLine(CB1, true);
  outputLine(CB2, false);
  via::write(ACR, 0x0C);
  for (uint8_t pattern : patterns) {
    via::write(SR, 0);
    uint8_t sevenFlags = 0;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      driveLine(CB1, false);
      driveLine(CB2, (pattern & (0x80 >> bit)) != 0);
      driveLine(CB1, true);
      if (bit == 6) {
        sevenFlags = via::read(IFR);
      }
    }
    const uint8_t completeFlags = via::read(IFR);
    const uint8_t received = via::read(SR);
    const uint8_t cleared = via::read(IFR);
    expect(F("SR input not complete after seven bits"), sevenFlags, 0, 0x04);
    expect(F("SR input complete after eight bits"), completeFlags, 0x04, 0x04);
    expect(F("SR external input byte"), received, pattern);
    expect(F("SR read clears flag"), cleared, 0, 0x04);
  }
}

static void shiftExternalOutput() {
  outputLine(CB1, true);
  inputLine(CB2);
  via::write(ACR, 0x1C);
  for (uint8_t pattern : patterns) {
    via::write(SR, pattern);
    uint8_t received = 0;
    uint8_t sevenFlags = 0;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      received = (received << 1) | (lineHigh(CB2) ? 1 : 0);
      driveLine(CB1, false);
      driveLine(CB1, true);
      if (bit == 6) {
        sevenFlags = via::read(IFR);
      }
    }
    const uint8_t completeFlags = via::read(IFR);
    expect(F("SR external output byte MSB first"), received, pattern);
    expect(F("SR output not complete after seven bits"), sevenFlags, 0, 0x04);
    expect(F("SR output complete after eight bits"), completeFlags, 0x04, 0x04);
  }
}

static void shiftInternalCompletion() {
  via::write(ACR, 0x18);
  via::write(IER, 0x84);
  via::write(SR, 0xA5);
  const bool completed = waitFlag(0x04, 3000);
  const bool asserted = !irqHigh();
  via::read(SR);
  const uint8_t cleared = via::read(IFR);
  const bool released = irqHigh();
  require(F("SR PHI2-clocked completion"), completed);
  require(F("SR IRQ asserted"), asserted);
  expect(F("SR read clears IRQ summary"), cleared, 0, 0x84);
  require(F("SR IRQ released"), released);
  via::write(SR, 0x5A);
  require(F("SR PHI2-clocked rearm"), waitFlag(0x04, 3000));
}

static void paOutputs() { portOutputs(true); }
static void pbOutputs() { portOutputs(false); }
static void paInputs() { portInputs(true); }
static void pbInputs() { portInputs(false); }
static void paMixed() { mixedPorts(true); }
static void pbMixed() { mixedPorts(false); }
static void paLatch() { latchedInput(true); }
static void pbLatch() { latchedInput(false); }

static bool runGroup(const __FlashStringHelper* name, void (*test)()) {
  Serial.print(F("\nTEST "));
  Serial.println(name);
  Serial.flush();
  via::reset();
  const uint16_t previousFailures = failures;
  test();
  const bool passed = previousFailures == failures;
  if (!passed) {
    ++failedGroups;
  }
  Serial.println(passed ? F("  PASS") : F("  FAIL"));
  return passed;
}

static void runSuite() {
  checks = 0;
  failures = 0;
  failedGroups = 0;
  Serial.println(F("\nMOS 6522 NMOS functional suite; PHI2 = 100 kHz"));
  if (!runGroup(F("reset defaults / bus preflight"), resetDefaults) ||
      !runGroup(F("registers / data and address lines"), registerAccess)) {
    via::holdReset();
    Serial.println(F("ABORTED: fix power, levels, clock or bus wiring before peripheral tests."));
    return;
  }
  runGroup(F("IER set/clear semantics"), enableRegister);
  runGroup(F("PA output: all 256 bytes"), paOutputs);
  runGroup(F("PB output: all 256 bytes"), pbOutputs);
  runGroup(F("PA input: all 256 bytes"), paInputs);
  runGroup(F("PB input: all 256 bytes"), pbInputs);
  runGroup(F("PA mixed direction"), paMixed);
  runGroup(F("PB mixed direction"), pbMixed);
  runGroup(F("PA input latch"), paLatch);
  runGroup(F("PB input latch"), pbLatch);
  runGroup(F("CA1 edges / IRQ masking"), ca1Interrupt);
  runGroup(F("CA2 edges / IRQ masking"), ca2Interrupt);
  runGroup(F("CB1 edges / IRQ masking"), cb1Interrupt);
  runGroup(F("CB2 edges / IRQ masking"), cb2Interrupt);
  runGroup(F("independent CA2/CB2 flags / IFR clearing"), independentInterrupts);
  runGroup(F("ORA no-handshake alias"), noHandshakeAlias);
  runGroup(F("manual control / handshake outputs"), controlOutputs);
  runGroup(F("T1 one-shot / rearm / IRQ"), timer1OneShot);
  runGroup(F("T1 free-run / PB7"), timer1FreeRun);
  runGroup(F("T2 interval / rearm / IRQ"), timer2OneShot);
  runGroup(F("T2 PB6 pulse counting"), timer2PulseCount);
  runGroup(F("SR external input mode 3"), shiftExternalInput);
  runGroup(F("SR external output mode 7"), shiftExternalOutput);
  runGroup(F("SR internal PHI2 mode 6 completion"), shiftInternalCompletion);
  via::holdReset();
  Serial.print(F("\nSUMMARY: "));
  Serial.print(checks);
  Serial.print(F(" checks, "));
  Serial.print(failures);
  Serial.print(F(" failures, "));
  Serial.print(failedGroups);
  Serial.println(F(" failed groups."));
  Serial.println(failures ? F("RESULT: FAIL") : F("RESULT: PASS (functional coverage only)"));
  Serial.println(F("VIA held in reset. Send r to repeat."));
}

static void help() {
  Serial.println(F("\nMOS 6522 tester - Arduino Mega 2560, 16 MHz"));
  Serial.println(F("Read the wiring and voltage-check instructions before connecting the IC."));
  Serial.println(F("r = run suite; h or ? = help"));
  Serial.println(F("No automatic run. VIA held in reset; PHI2 runs continuously at 100 kHz."));
}

void setup() {
  via::begin();
  Serial.begin(115200);
  help();
}

void loop() {
  if (!Serial.available()) {
    return;
  }
  const char command = static_cast<char>(Serial.read());
  if (command == 'r' || command == 'R') {
    runSuite();
  } else if (command == 'h' || command == '?') {
    help();
  } else if (command != '\r' && command != '\n' && command != ' ') {
    Serial.println(F("Unknown command. Send h for help."));
  }
}
