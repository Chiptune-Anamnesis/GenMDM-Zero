# GenMDM Pico - Complete Build Guide

## Parts Needed
- 1x Waveshare RP2040-Zero
- 1x ULN2003AN DIP-16
- 1x Male DE-9 connector (solder type, for Genesis controller port)
- Hookup wire
- (Optional) 6N137 optocoupler + TRS jack for MIDI input

## ULN2003AN DIP-16 Pinout

Inputs on the left, outputs MIRRORED on the right.
Each input directly drives the output across from it.

```
            +----v----+
 IN1 (1B)  1|         |16  OUT1 (1C)
 IN2 (2B)  2|         |15  OUT2 (2C)
 IN3 (3B)  3|         |14  OUT3 (3C)
 IN4 (4B)  4| ULN     |13  OUT4 (4C)
 IN5 (5B)  5| 2003AN  |12  OUT5 (5C)
 IN6 (6B)  6|         |11  OUT6 (6C)
 IN7 (7B)  7|         |10  OUT7 (7C)
      GND  8|         |9   COM
            +---------+

IN1 -> OUT1, IN2 -> OUT2, ... IN7 -> OUT7
Pin 9 (COM): leave unconnected
```

## Genesis DE-9 Connector Pinout

Looking INTO the console's controller port (female):

```
    5   4   3   2   1
     \  |   |   |  /
      9   8   7   6
```

Your male DE-9 plug (mirror image, looking at solder side):

```
    1   2   3   4   5
     /  |   |   |  \
      6   7   8   9
```

## Complete Wiring

### Power (Genesis -> RP2040-Zero)

| From | To | Notes |
|------|----|-------|
| Genesis Pin 5 (+5V) | RP2040-Zero **VBUS** pin | Powers the board via onboard 3.3V regulator |
| Genesis Pin 8 (GND) | RP2040-Zero **GND** pin | Common ground |
| Genesis Pin 8 (GND) | ULN2003AN **pin 8** (GND) | Common ground |

**All three GND connections must be tied together.**

If you have power issues, disconnect Genesis Pin 5 and power via USB instead.

### Signal (RP2040-Zero -> ULN2003AN -> Genesis)

| RP2040-Zero | ULN2003AN | ULN2003AN | Genesis Pin | Signal |
|-------------|-----------|-----------|-------------|--------|
| GP2 | IN1 (pin 1) | OUT1 (pin 16) | Pin 4 (Right) | Data bit 3 |
| GP3 | IN2 (pin 2) | OUT2 (pin 15) | Pin 3 (Left) | Data bit 2 |
| GP4 | IN3 (pin 3) | OUT3 (pin 14) | Pin 2 (Down) | Data bit 1 |
| GP5 | IN4 (pin 4) | OUT4 (pin 13) | Pin 1 (Up) | Data bit 0 |
| GP6 | IN5 (pin 5) | OUT5 (pin 12) | Pin 6 (B) | NB (nibble select) |
| GP7 | IN6 (pin 6) | OUT6 (pin 11) | Pin 9 (C) | AD (addr/data) |
| GP8 | IN7 (pin 7) | OUT7 (pin 10) | Pin 7 (TH) | WR (write strobe) |

### Summary: 10 wires total

```
RP2040-Zero          ULN2003AN              Genesis DE-9
-----------          ---------              -----------
GP2  ---------- 1  IN1  |  OUT1  16 ------- Pin 4 (Right)
GP3  ---------- 2  IN2  |  OUT2  15 ------- Pin 3 (Left)
GP4  ---------- 3  IN3  |  OUT3  14 ------- Pin 2 (Down)
GP5  ---------- 4  IN4  |  OUT4  13 ------- Pin 1 (Up)
GP6  ---------- 5  IN5  |  OUT5  12 ------- Pin 6 (B)
GP7  ---------- 6  IN6  |  OUT6  11 ------- Pin 9 (C)
GP8  ---------- 7  IN7  |  OUT7  10 ------- Pin 7 (TH)
GND  ---------- 8  GND  |  COM   9  (NC)
                         |
VBUS <---------------------------------------- Pin 5 (+5V)
GND  <---------------------------------------- Pin 8 (GND)
```

## Flashing the Firmware

1. Hold the BOOT button on the RP2040-Zero
2. Plug in USB cable (while holding BOOT)
3. Release BOOT - a USB drive named "RPI-RP2" appears
4. Copy `firmware.uf2` from `.pio/build/rp2040zero/` to the drive
5. The board reboots automatically with the firmware

Or use PlatformIO: `pio run -t upload`

## Testing

1. Flash the GenMDM cartridge ROM to your flash cart
2. Insert cart into Genesis and power on - wait for the init tone
3. Plug the DE-9 into Genesis controller port 2
4. You should hear a short ascending melody (confirms connection)
5. Send MIDI from your DAW via USB
6. Notes on MIDI channels 1-6 = FM synthesis
7. Notes on MIDI channels 7-9 = PSG (SN76489)
8. Notes on MIDI channel 10 = PSG noise

## TRS MIDI Input (Optional)

For serial MIDI via 3.5mm TRS jack, add a 6N137 optocoupler:

```
TRS Jack Tip ---[220R]--- 6N137 pin 2 (Anode)
TRS Jack Sleeve --------- 6N137 pin 3 (Cathode)
3.3V -------------------- 6N137 pin 8 (Vcc)
GND --------------------- 6N137 pin 5 (GND)
6N137 pin 6 (Output) ---- RP2040-Zero GP1 (Serial1 RX)
3.3V ---[4.7K]----------- 6N137 pin 6 (pullup)
```
