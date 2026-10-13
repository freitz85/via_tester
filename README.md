# MOS 6522 functional tester

Arduino Mega **2560, 16 MHz, 5 V** firmware for a standalone, original
**NMOS MOS 6522 / 6522A in a 40-pin DIP**. No 6502 CPU is needed. The Mega
provides the bus, a continuous 100 kHz PHI2 clock, and peripheral stimuli.

This is a **direct-wired bench prototype**, not a guaranteed-level production
tester. Firmware compilation alone does not validate a physical IC.

## Electrical requirements - read before assembly

- Original NMOS VIA outputs are TTL-level: a guaranteed HIGH can be only 2.4 V.
  The ATmega2560 normally requires at least 0.6 * VCC (3.0 V at 5 V) for a
  guaranteed digital HIGH. Direct connection therefore is **not guaranteed**.
- Fit external **10k pull-ups to +5 V** on D0-D7, PA0-PA7, PB0-PB7,
  CA2, CB1 and CB2. These help weak NMOS outputs rise but are not a substitute
  for verified logic levels or proper level buffering.
- Fit a **330 ohm series resistor in each data, peripheral-port and control
  connection** between the Mega and VIA (D0-D7, PA0-PA7, PB0-PB7, CA1, CA2,
  CB1 and CB2). Place pull-ups on the VIA side. Resistors mitigate accidental
  contention; they do not make contention safe or guarantee logic levels.
- Fit an external **4.7k pull-up** on IRQ to +5 V. NMOS IRQ is open-drain.
  Connect IRQ only to Mega D2; do not tie it directly to +5 V.
- Fit a **10k pull-up on /RESET** and a **10k pull-up on /CS2** so that the VIA
  is deselected while the Mega boots. Do not connect the VIA reset to the
  Mega's own reset pin.
- Put a **100 nF ceramic capacitor** directly between VIA pins 20 and 1;
  add 10 uF across the supply near the socket. Use short wires and a common
  ground. Do not connect LEDs or other loads to the tested signals.
- Use one regulated 5 V supply for both devices. USB power through the Mega
  can supply a single VIA if the total current stays within the board and
  USB limits. Do not connect competing supplies or apply signals to an
  unpowered IC. Disconnect power before inserting or removing the chip.
- Before trusting results, scope HIGH and LOW voltages **at the receiving
  pins**, with the actual pull-ups, resistors and clock. Mega inputs need
  HIGH >= 3.0 V and LOW <= 1.5 V at VCC = 5 V; check the VIA's own input
  limits too (normally TTL HIGH >= 2.0 V, LOW <= 0.8 V).
  Verify D0-D7 have settled at the read sampling point, about 2 us after
  PHI2 rises. If these levels/timings are not met, stop and use HCT-compatible
  buffering rather than declaring the chip faulty.

The tester and chip must be isolated from any computer motherboard or other
bus master. This pin map and firmware are not intended for in-circuit tests,
a 3.3 V board, the smaller ATmega1280 Mega, or CMOS VIA variants.

## Wiring

Numbers in the VIA column are **physical DIP pins, viewed from above**.
With the notch at the top, pin 1 is top-left; pins count counterclockwise.
Verify the package against the original datasheet before applying power.

### Supply and chip selection

| VIA signal | VIA pin | Connection |
|---|---:|---|
| VSS | 1 | Mega GND |
| VCC | 20 | +5 V |
| CS1 (active HIGH) | 24 | +5 V |
| /CS2 (active LOW) | 23 | Mega D44, 10k pull-up |
| /RESET | 34 | Mega D43, 10k pull-up |
| PHI2 | 25 | Mega D11 / OC1A |
| R/W | 22 | Mega D45 |
| /IRQ | 21 | Mega D2, 4.7k pull-up |

**Do not confuse pin 23 (/CS2) with pin 19 (CB2).**

### Register address and data bus

| VIA signal | VIA pin | Mega pin |
|---|---:|---:|
| RS0 | 38 | D49 |
| RS1 | 37 | D48 |
| RS2 | 36 | D47 |
| RS3 | 35 | D46 |
| D0 | 33 | D22 |
| D1 | 32 | D23 |
| D2 | 31 | D24 |
| D3 | 30 | D25 |
| D4 | 29 | D26 |
| D5 | 28 | D27 |
| D6 | 27 | D28 |
| D7 | 26 | D29 |

### Peripheral test connections

The Mega directly drives and samples these pins; no PA-to-PB loopback jumpers
are needed. A8-A15 are used as digital pins, not analog measurements.

| VIA signal | VIA pin | Mega pin |
|---|---:|---|
| PA0 | 2 | A8 / D62 |
| PA1 | 3 | A9 / D63 |
| PA2 | 4 | A10 / D64 |
| PA3 | 5 | A11 / D65 |
| PA4 | 6 | A12 / D66 |
| PA5 | 7 | A13 / D67 |
| PA6 | 8 | A14 / D68 |
| PA7 | 9 | A15 / D69 |
| PB0 | 10 | D37 |
| PB1 | 11 | D36 |
| PB2 | 12 | D35 |
| PB3 | 13 | D34 |
| PB4 | 14 | D33 |
| PB5 | 15 | D32 |
| PB6 | 16 | D31 |
| PB7 | 17 | D30 |
| CA1 | 40 | D38 |
| CA2 | 39 | D39 |
| CB1 | 18 | D40 |
| CB2 | 19 | D41 |

PB wiring intentionally runs **D37 down to D30**, matching bits 0-7 of
ATmega PORTC. D22-D29 use PORTA and A8-A15 use PORTK. Register/address/control
signals use PORTL. These entire port banks are reserved: do not attach shields
or reuse their pins. Timer1 and its PWM pins D11/D12/D13 are also reserved;
do not add Servo or other Timer1 users. Serial uses the normal USB connection.

## Upload and run

1. Open `ViaTester/ViaTester.ino` in Arduino IDE.
2. Select **Arduino AVR Boards > Arduino Mega or Mega 2560**, processor
   **ATmega2560**, and your board's serial port. No external libraries needed.
3. Upload, then open Serial Monitor at **115200 baud**.
4. With power disconnected, assemble and verify the wiring and IC orientation.
   Power up, verify the clock and logic levels, then send **`r`**.
5. Read the per-group PASS/FAIL messages and final summary. Send `r` to repeat;
   `h` or `?` prints help.

There is no automatic test on boot. The VIA remains in reset until a test
starts and is held in reset afterwards, including after a failed preflight.
PHI2 continues running even while Serial is printing. Test execution is
synchronous; commands typed during a run are handled afterwards.

With Arduino CLI installed:

```powershell
arduino-cli core install arduino:avr
arduino-cli compile --fqbn arduino:avr:mega:cpu=atmega2560 --warnings all .\ViaTester
# Replace COM5 with the board's actual port:
arduino-cli upload --fqbn arduino:avr:mega:cpu=atmega2560 --port COM5 .\ViaTester
arduino-cli monitor --port COM5 --config baudrate=115200
```

## Test coverage

Every group begins with a hardware reset; results do not depend on the
previous group's configuration. Reset/register preflight failure aborts the
suite before the Mega drives peripheral inputs. There are 25 test groups.
Port stimulus groups also verify DDR readback before enabling Mega drivers.
This catches incorrect direction-register settings, but cannot guarantee
protection against a damaged VIA output stage that ignores its DDR.

| Group | Checks |
|---|---|
| Reset / register access | Defined reset values, DDR/ACR/PCR patterns including walking bits, distinct register values to detect address aliasing, T1 latch readback, disabled SR readback |
| Interrupt enable | IER bitwise set, accumulated enable, selective clear; bit 7 ignored on read because historical revisions differ |
| PA and PB output | All 256 bytes at physical pins and register reads |
| PA and PB input | All 256 externally driven bytes |
| Mixed directions | Six DDR masks and twelve input patterns per port; physical and register results |
| Input latches | CA1/CB1 capture and retention while physical inputs change |
| Control interrupts | Both edge polarities on CA1/CA2/CB1/CB2, masked pending flags, enabling an already pending IRQ, disabling without clearing, port-access flag clearing |
| Independent interrupts | CA2/CB2 flags survive port access; selective IFR write-one-to-clear |
| ORA alias | Register 15 accesses data without clearing CA1 or asserting the CA2 handshake |
| Control outputs | CA2/CB2 forced LOW/HIGH and handshake acknowledgement through CA1/CB1 |
| T1 | Counter readback/decrement, one-shot, coarse timeout window, IRQ/clear/rearm, free-running repeated timeout and PB7 toggling |
| T2 | Interval/IRQ/clear/rearm, stationary PB6 pulse counter, three exact counted edges, eventual pulse timeout |
| Shift register | Mode 3 external input and mode 7 external output, twelve patterns each, MSB-first bytes and 7-versus-8-bit completion, flag clearing; mode 6 PHI2-clocked completion/IRQ/rearm |

The clock is 100 kHz (5 us per half-cycle) from the Mega's hardware Timer1.
Bus accesses set address/R/W/data while PHI2 is LOW, sample reads during HIGH,
and retain writes through the falling edge. Interrupts are masked only around
individual bus transactions and synchronized external control transitions.
The timer interval check permits 1.5-3.5 ms for a nominal approximately 2 ms
timeout; it detects gross failures, not datasheet-level accuracy.

### Deliberate limits

- This is not a 1 MHz or 2 MHz speed certification or a supply/temperature sweep.
- No maximum-load, output-drive-strength, analog threshold, leakage or
  rise/fall-time qualification.
- CA2/CB2 one-cycle pulse-output mode and pulse-width measurements are not
  covered. Handshake outputs are covered.
- SR Timer2-clocked modes 1/4/5 and internally clocked input mode 2 are not
  covered. Mode 6 checks completion, not its entire serial waveform.
- The T2 pulse test verifies exact decrement for three edges and eventual
  timeout, not the precise timeout-edge boundary or 16-bit wraparound.
- One-shot tests check lack of immediate repetition, not behavior after a full
  65536-cycle wraparound.
- Original NMOS external SR clocking has a known PHI2/CB1 edge-coincidence
  defect. Stimulus edges are synchronized away from PHI2 falling edges;
  the suite does not attempt to classify or reproduce that defect.

A PASS means the implemented functional checks passed with this fixture.
A FAIL may mean a bad IC, incorrect wiring, poor supply/decoupling or
incompatible electrical levels. It is not automatically evidence of a bad VIA.
Initial readback of all `FF` usually warrants checking chip select, reset,
data-bus connections and the IRQ/other pull-ups before changing software.

## Sources and validation status

- [Original MOS MCS6522 preliminary datasheet, November 1977](https://6502.org/documents/datasheets/mos/mos_6522_preliminary_nov_1977.pdf)
- [Searchable original datasheet mirror](https://archive.org/download/ensoniq-mirage-files-collection/ensoniq%20mirage/ewans%20mirage%20archive/Mirage%20Schematics/mos_6522_text.pdf)
- [Additional scanned datasheet](https://www.princeton.edu/~mae412/HANDOUTS/Datasheets/6522.pdf)
- [ATmega2560 documentation](https://www.microchip.com/en-us/product/ATmega2560)

Build verified for `arduino:avr:mega:cpu=atmega2560` with Arduino AVR core
1.8.8: 11,790 bytes of flash and 215 bytes of static RAM. A separate
sketch-only `-Wall -Wextra -Werror` check passes. The full `--warnings all`
build reports four unused-parameter warnings in the Arduino core's
`new.cpp`, not in this sketch. Generated AVR instructions were inspected
for bus phase setup and the fixed 2 us read/stimulus and 1 us write-hold
delays.

No physical VIA run has been performed. Hardware tests and oscilloscope
verification are still required before treating this fixture as validated.
