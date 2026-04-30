# GenMDM Pico

A port of [GenMDM v1.02](https://catskullelectronics.com/genmdm) to the Raspberry Pi Pico / RP2040, with a few small fixes and quality-of-life improvements over the original AVR firmware.

The original GenMDM converts MIDI into Sega Genesis YM2612 + SN76489 register writes via a Mega Drive controller-port cartridge. This port is bit-for-bit compatible with v1.02 for normal MIDI data, same CCs, same sound, but ships on cheaper, more available hardware and addresses a handful of issues in the original.

For wiring/build instructions, see [WIRING.md](WIRING.md).

## Differences from v1.02

### Bug fixes

- **High notes (MIDI 96+) play in the correct octave.** The original firmware computes `(pitch / octDiv) << 11` with no clamp. The YM2612's block field is only 3 bits (0–7), so when `pitch / octDiv ≥ 8` the high bit lands in an unused bit of register 0xA4 and the block silently drops to 0 — meaning C7 and above play roughly five octaves below where you expect. This port saturates the block at 7 instead, so high notes hit the top octave cleanly. Confirmed identical bug in the disassembled v1.02 hex.

### Feature additions

- **Perceptual velocity curve (CC 82, default ON).** Maps incoming MIDI velocity through a sqrt curve so quiet notes are actually audible without crushing dynamics. Set CC 82 = 0 to fall back to the original linear v1.02 mapping.
- **Expanded DAC sample bank: 71 samples vs. 7 in v1.02.** The 7 original v1.02 samples are preserved at indices 0–6 ([src/samples_pico.h](src/samples_pico.h)) plus 64 new ones layered on top:
  - 24 Bitkits drum samples ([src/samples_drums.h](src/samples_drums.h))  indices 7–30
  - 24 Bitkits one-shots ([src/samples_oneshots.h](src/samples_oneshots.h))  indices 31–54
  - 16 ST-Sound DigiDrum samples ([src/samples_legacy.h](src/samples_legacy.h))  indices 55–70

  All triggered the same way as the original GenMDM samples on channel 6 (Note On ≥ MIDI 60 selects sample `pitch % 71`).

### MIDI throughput improvements

These reduce dropped MIDI bytes when the device is under sustained heavy load (multi-channel notes + CC automation), especially via the 3.5mm TRS input.

- **Enlarged Serial1 RX FIFO (256 bytes).** The original RP2040 default UART buffer can overflow during a 4-5 ms preset load (CC 9 / Program Change) at full MIDI rate. 256 bytes absorbs the worst-case burst with margin to spare.
- **Last-value write cache for non-freq YM2612 registers.** Repeated identical CC writes (e.g., LFO sweeps, mod wheel, slow TL automation) are deduplicated so the cart bus isn't bit-banged for nothing — saves ~180 µs per skipped write. The cache deliberately bypasses the FM frequency registers (0xA0–0xAF), key-on/off (0x28), and DAC stream (0x2A/2B). See "YM2612 quirks" below for why.
- **Pitch-bend coalescing.** A storm of bend messages on a channel collapses to its most recent value, applied once per main-loop pass. Eliminates the multi-millisecond pile-up that used to happen during fast bend automation.

## YM2612 quirks worth knowing if you fork this

- **Shared frequency-high latch.** The YM2612 has *one* global latch for FM frequency-high writes (registers 0xA4–0xA6 and 0xAC–0xAE) not one per channel. You must always write the high byte immediately before the low byte (0xA0–0xA2 / 0xA8–0xAA) for that channel, otherwise the next commit picks up another channel's latched block bits and the note plays in the wrong octave. This is why the write cache exempts the entire 0xA0–0xAF range.
- **3-bit block field.** Block is bits 3–5 of register 0xA4 values 0–7 only. Never write a value where bit 6 is set.
- **0x28, 0x2A, 0x2B are events, not state.** Same-value writes to key on/off and the DAC stream are semantically meaningful (re-trigger, sample byte) — never deduplicate them.

## Building

```
pio run -e rp2040zero
```

PlatformIO + Earle Philhower's [arduino-pico](https://github.com/earlephilhower/arduino-pico) core, with Adafruit TinyUSB and FortySevenEffects MIDI Library. Targets the Waveshare RP2040-Zero by default; any RP2040 board works with minor pin remapping.
