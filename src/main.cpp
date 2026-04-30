// GenMDM Pico - RP2040-Zero port of GenMDM v1.02
// Board: Waveshare RP2040-Zero (Earle Philhower core)
// Tools -> USB Stack: TinyUSB
// Libraries: Adafruit TinyUSB, FortySevenEffects MIDI Library (v5.x)
//
// Hardware: All outputs via ULN2003A open-collector driver (7 channels).
// ULN2003A inverts: GPIO HIGH = Genesis pin LOW, GPIO LOW = Genesis pin HIGH.
//
// RP2040-Zero GPIO -> ULN2003A -> Genesis DE-9 Controller Port
// GP2  -> ch1 -> Pin 4 (Right) = Data bit 3, register bit 3
// GP3  -> ch2 -> Pin 3 (Left)  = Data bit 2, register bit 2
// GP4  -> ch3 -> Pin 2 (Down)  = Data bit 1, register bit 1
// GP5  -> ch4 -> Pin 1 (Up)    = Data bit 0, register bit 0
// GP6  -> ch5 -> Pin 6 (B/TL)  = NB (nibble select), register bit 4
// GP7  -> ch6 -> Pin 9 (C/TR)  = AD (addr/data phase), register bit 5
// GP8  -> ch7 -> Pin 7 (TH)    = WR (write strobe), register bit 6
// GND         -> Pin 8 (GND)
//
// TRS MIDI input: GP1 (Serial1 RX) via 6N137 optocoupler

#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <MIDI.h>
#include <math.h>
#include <Adafruit_NeoPixel.h>
#include "samples_pico.h"

// Onboard NeoPixel LED on GP16
Adafruit_NeoPixel pixel(1, 16, NEO_GRB + NEO_KHZ800);

// ==================== USB MIDI ====================
Adafruit_USBD_MIDI usb_midi;
MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usb_midi, usbMIDI);

// ==================== Serial MIDI (TRS input via 6N137) ====================
MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, serialMIDI);

// ==================== Pin Definitions ====================
static const uint8_t PIN_BIT3 = 2;  // GP2 -> Genesis Pin 4 -> reg bit 3
static const uint8_t PIN_BIT2 = 3;  // GP3 -> Genesis Pin 3 -> reg bit 2
static const uint8_t PIN_BIT1 = 4;  // GP4 -> Genesis Pin 2 -> reg bit 1
static const uint8_t PIN_BIT0 = 5;  // GP5 -> Genesis Pin 1 -> reg bit 0
static const uint8_t PIN_NB   = 6;  // GP6 -> Genesis Pin 6 -> reg bit 4
static const uint8_t PIN_AD   = 7;  // GP7 -> Genesis Pin 9 -> reg bit 5
static const uint8_t PIN_WR   = 8;  // GP8 -> Genesis Pin 7 -> reg bit 6

static const int dT = 20;

// ==================== GPIO Helpers ====================
// Direct connection (no ULN2003A) - uncomment the inverted line if using ULN
inline void setGenesisPin(uint8_t pin, bool value) {
  digitalWrite(pin, value ? HIGH : LOW);  // direct connection
  // digitalWrite(pin, value ? LOW : HIGH);  // use this line for ULN2003A
}
inline void outputNibble(uint8_t n) {
  setGenesisPin(PIN_BIT0, (n >> 0) & 1);
  setGenesisPin(PIN_BIT1, (n >> 1) & 1);
  setGenesisPin(PIN_BIT2, (n >> 2) & 1);
  setGenesisPin(PIN_BIT3, (n >> 3) & 1);
}
inline void setNB(bool high) { setGenesisPin(PIN_NB, high); }
inline void setAD(bool high) { setGenesisPin(PIN_AD, high); }
inline void setWR(bool high) { setGenesisPin(PIN_WR, high); }
inline void strobeWR() {
  setWR(true);  delayMicroseconds(dT);
  setWR(false); delayMicroseconds(dT);
}

// ==================== Variables ====================
int bendAmount = 2;
byte page_number = 0;
byte velocity; // Global last-played velocity - matches original GenMDM behavior
byte ccvalue2;
byte bendLSB, bendMSB;
byte voiceAge[6] = { 0,0,0,0,0,0 };
byte voiceAgeCounter = 0;

// Sample playback
int SPB_flag = 0, SPB_sound = 0, SPB_counter = 0, SPB_speed = 0;
int SPB_tick = 1, sample_on = 0, SPB_max = 400;
byte overSamp = 1, save_speed = 1;
byte noise_flag = 0, noise_data = 0, noise_velocity = 0, tri_flag = 1;

byte triangle[] = {
  0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80,
  0x40, 0x20, 0x10, 0x08, 0x04, 0x02,
};

// Polyphonic Handler
byte polyFlag = 0;
byte polyBusy[6];
byte pitchTracking[6];

// Pitch Values
byte octDiv = 12;
byte pitchOffset = 64;
uint16_t pitchInt;
double pitchDouble;
double constantDouble = 6.711;
byte reg22 = 0;
byte bend[] = { 64, 64, 64, 64, 64, 64 };

// Register Shadow Arrays
byte regB0[] = { 0,0,0,0,0,0 }; byte regB4[] = { 0,0,0,0,0,0 };
byte reg30[] = { 0,0,0,0,0,0 }; byte reg34[] = { 0,0,0,0,0,0 };
byte reg38[] = { 0,0,0,0,0,0 }; byte reg3c[] = { 0,0,0,0,0,0 };
byte reg50[] = { 0,0,0,0,0,0 }; byte reg54[] = { 0,0,0,0,0,0 };
byte reg58[] = { 0,0,0,0,0,0 }; byte reg5c[] = { 0,0,0,0,0,0 };
byte reg60[] = { 0,0,0,0,0,0 }; byte reg64[] = { 0,0,0,0,0,0 };
byte reg68[] = { 0,0,0,0,0,0 }; byte reg6c[] = { 0,0,0,0,0,0 };
byte reg80[] = { 0,0,0,0,0,0 }; byte reg84[] = { 0,0,0,0,0,0 };
byte reg88[] = { 0,0,0,0,0,0 }; byte reg8c[] = { 0,0,0,0,0,0 };

byte TL1[] = { 127,127,127,127,127,127 };
byte TL2[] = { 127,127,127,127,127,127 };
byte TL3[] = { 127,127,127,127,127,127 };
byte TL4[] = { 127,127,127,127,127,127 };

// Velocity curve LUT - maps raw MIDI velocity (0-127) to perceptually-scaled value.
// Uses sqrt curve: boosts low velocities for audibility while preserving max at 127.
// Toggle via CC 82 (value 0 = original linear, value > 0 = curve enabled, default ON).
byte velocityCurveEnabled = 1;
const uint8_t velocityCurve[128] = {
    0,  11,  16,  20,  23,  25,  28,  30,  32,  34,  36,  37,  39,  41,  42,  44,
   45,  46,  48,  49,  50,  52,  53,  54,  55,  56,  57,  59,  60,  61,  62,  63,
   64,  65,  66,  67,  68,  69,  69,  70,  71,  72,  73,  74,  75,  76,  76,  77,
   78,  79,  80,  80,  81,  82,  83,  84,  84,  85,  86,  87,  87,  88,  89,  89,
   90,  91,  92,  92,  93,  94,  94,  95,  96,  96,  97,  98,  98,  99, 100, 100,
  101, 101, 102, 103, 103, 104, 105, 105, 106, 106, 107, 108, 108, 109, 109, 110,
  110, 111, 112, 112, 113, 113, 114, 114, 115, 115, 116, 117, 117, 118, 118, 119,
  119, 120, 120, 121, 121, 122, 122, 123, 123, 124, 124, 125, 125, 126, 126, 127
};

inline byte applyCurve(byte v) {
  return velocityCurveEnabled ? velocityCurve[v & 0x7F] : v;
}

// FM Presets (16)
const byte ALGO[16] =      { 37,62,127,127,35,0,96,112,0,16,32,48,64,80,96,112 };
const byte FB[16] =        { 0,65,0,0,0,0,96,112,0,16,32,48,64,80,96,112 };
const byte TLOP1[16] =     { 127,100,100,120,127,127,96,112,0,16,32,48,64,80,96,112 };
const byte TLOP2[16] =     { 127,103,103,113,127,127,96,112,0,16,32,48,64,80,96,112 };
const byte TLOP3[16] =     { 127,108,108,107,127,127,96,112,0,16,32,48,64,80,96,112 };
const byte TLOP4[16] =     { 127,127,127,127,127,127,96,112,0,16,32,48,64,80,96,112 };
const byte MULOP1[16] =    { 0,0,25,29,0,0,96,112,0,16,32,48,64,80,96,112 };
const byte MULOP2[16] =    { 0,0,18,55,0,0,96,112,0,16,32,48,64,80,96,112 };
const byte MULOP3[16] =    { 30,10,8,36,0,0,96,112,0,16,32,48,64,80,96,112 };
const byte MULOP4[16] =    { 0,0,0,0,0,0,96,112,0,16,32,48,64,80,96,112 };
const byte DETUNEOP1[16] = { 0,0,0,0,0,27,96,112,0,16,32,48,64,80,96,112 };
const byte DETUNEOP2[16] = { 0,0,0,0,0,9,96,112,0,16,32,48,64,80,96,112 };
const byte DETUNEOP3[16] = { 0,0,0,0,0,18,96,112,0,16,32,48,64,80,96,112 };
const byte DETUNEOP4[16] = { 0,0,0,0,0,70,96,112,0,16,32,48,64,80,96,112 };
const byte ATTACKOP1[16] = { 127,127,127,127,122,54,96,112,0,16,32,48,64,80,96,112 };
const byte ATTACKOP2[16] = { 127,127,127,127,123,49,96,112,0,16,32,48,64,80,96,112 };
const byte ATTACKOP3[16] = { 127,127,127,127,120,41,96,112,0,16,32,48,64,80,96,112 };
const byte ATTACKOP4[16] = { 127,127,127,127,118,89,96,112,0,16,32,48,64,80,96,112 };
const byte DECAY1OP1[16] = { 0,52,0,60,58,30,96,112,0,16,32,48,64,80,96,112 };
const byte DECAY1OP2[16] = { 0,0,0,60,127,30,96,112,0,16,32,48,64,80,96,112 };
const byte DECAY1OP3[16] = { 0,0,0,60,122,30,96,112,0,16,32,48,64,80,96,112 };
const byte DECAY1OP4[16] = { 0,0,0,60,75,30,96,112,0,16,32,48,64,80,96,112 };
const byte DECAY2OP1[16] = { 127,127,127,127,87,73,96,112,0,16,32,48,64,80,96,112 };
const byte DECAY2OP2[16] = { 127,127,127,127,68,51,96,112,0,16,32,48,64,80,96,112 };
const byte DECAY2OP3[16] = { 127,127,127,127,88,43,96,112,0,16,32,48,64,80,96,112 };
const byte DECAY2OP4[16] = { 127,127,127,127,61,65,96,112,0,16,32,48,64,80,96,112 };
const byte AMP2OP1[16] =   { 127,127,127,127,127,127,96,112,0,16,32,48,64,80,96,112 };
const byte AMP2OP2[16] =   { 127,127,127,127,127,127,96,112,0,16,32,48,64,80,96,112 };
const byte AMP2OP3[16] =   { 127,127,127,127,127,127,96,112,0,16,32,48,64,80,96,112 };
const byte AMP2OP4[16] =   { 127,127,127,127,127,127,96,112,0,16,32,48,64,80,96,112 };
const byte RELOP1[16] =    { 127,127,60,127,127,70,96,112,0,16,32,48,64,80,96,112 };
const byte RELOP2[16] =    { 127,127,60,127,127,70,96,112,0,16,32,48,64,80,96,112 };
const byte RELOP3[16] =    { 127,127,60,127,127,70,96,112,0,16,32,48,64,80,96,112 };
const byte RELOP4[16] =    { 127,127,60,127,127,70,96,112,0,16,32,48,64,80,96,112 };

// SN76489 Data
byte working_byte = 0;
int pdatInt = 0;
byte pdat1 = 0, pdat2 = 0;

const int pitchTable[] = {
  1008,951,898,847,800,755,713,673,635,599,566,534,504,
  475,449,424,400,378,356,336,317,300,283,267,252,238,
  224,212,200,189,178,168,159,150,141,133,126,119,112,
  106,100,94,89,84,79,75,71,67,63,59,56,53,50,47,45,
  42,40,37,35,33,31,30,28,26,25,24,22,21,20,19,18,17,
  16,15,14,13,13,12,11,11,10,9,9,
  1017,960,906,855,807,762,719,679,641,605,571,539,508,480,
  453,428,404,381,360,339,320,302,285,269,254,240,226,214,
  202,190,180,170,160,151,143,135,127,120,113,107,101,95,
  90,85,80,76,71,67,64,60,57,53,50,48,45,42,40,38,36,
  34,32,30,28,27,25,24,22,21,20,19,18,17,16,15,14,13,
  13,12,11,11,10,9,9
};

int coarsePitch[] = { 64,64,64,64 };
int pitchTableOffset = 0;
byte bend_data[] = { 64,64,64,64 };
byte bend_MSB[] = { 64,64,64,64 };
byte pitchData[] = { 0,0,0,0 };
byte velocityData[] = { 0,0,0,0 };

const byte noiseLookup[] = {
  0xE0,0xE0,0xE1,0xE1,0xE2,0xE4,0xE5,0xE6,0xE6,0xE3,0xE3,0xE7,
};

// Forward declarations
void doNote(byte channel, byte pitch, byte velocity);
void doNoteOff(byte channel, byte pitch, byte velocity);
void doCC(byte channel, byte ccnumber, byte ccvalue);
void doBend(byte channel, int bend_usb);
void doSample();
void writeMD(byte page, byte address, byte data);
void writeSN76489(byte data);
void writeAmplitude(byte velocity, byte channel);
void writeFrequency(byte pitch, byte channel);

// ==================== YM2612 Write Protocol ====================
// Last-written value cache: skip duplicate register writes (~180us each).
// Exempt:
//   0x28           - key on/off events (same-value write means re-trigger)
//   0x2A, 0x2B     - DAC sample byte stream
//   0xA0-0xAF      - FM frequency registers. The high-byte writes (0xA4-0xA6,
//                    0xAC-0xAE) share ONE global latch register on the YM2612;
//                    skipping a "redundant" 0xA4 lets another channel's stale
//                    latch corrupt the next 0xA0 commit, producing wrong-octave
//                    notes when multiple channels are interleaving freq writes.
uint8_t writeMDCache[2][256];
bool writeMDCacheValid[2][256];

void writeMD(byte page, byte address, byte data) {
  bool exempt = (address == 0x28) || (address == 0x2A) || (address == 0x2B)
             || (address >= 0xA0 && address <= 0xAF);
  if (!exempt && writeMDCacheValid[page][address] && writeMDCache[page][address] == data) return;
  writeMDCache[page][address] = data;
  writeMDCacheValid[page][address] = true;

  if (page_number != page) {
    page_number = page;
    outputNibble(page == 0 ? 0x0D : 0x0E);
    setNB(true); setAD(true);
    strobeWR();
  }
  outputNibble(data & 0x0F);        setNB(false); setAD(false); strobeWR();
  outputNibble((data >> 4) & 0x0F); setNB(true);  setAD(false); strobeWR();
  outputNibble(address & 0x0F);     setNB(false); setAD(true);  strobeWR();
  outputNibble((address >> 4) & 0x0F); setNB(true); setAD(true); strobeWR();
}

// ==================== SN76489 Write Protocol ====================
void writeSN76489(byte data) {
  outputNibble(data & 0x0F);        setNB(false); setAD(false); strobeWR();
  outputNibble((data >> 4) & 0x0F); setNB(true);  setAD(false); strobeWR();
  outputNibble(0x0C);               setNB(true);  setAD(true);  strobeWR();
  delayMicroseconds(dT); delayMicroseconds(dT);
}

void writeAmplitude(byte velocity, byte channel) {
  channel = channel << 5;
  velocity = 15 - (velocity >> 3);
  writeSN76489(0b10010000 + channel + velocity);
}

void writeFrequency(byte pitch, byte channel) {
  int coarsePitchVal = -12 + (coarsePitch[channel] / 5);
  pdatInt = pitchTable[pitch - 45 + pitchTableOffset + coarsePitchVal] + (64 - bend_data[channel]);
  if (pdatInt < 0) pdatInt = 0;
  else if (pdatInt > 1023) pdatInt = 1023;
  pdat1 = pdatInt % 16;
  byte ch_shifted = channel << 5;
  writeSN76489(0b10000000 + ch_shifted + pdat1);
  writeSN76489(pdatInt >> 4);
}

// ==================== Note On ====================
void doNote(byte channel, byte pitch, byte velocity) {
  channel--;

  if (channel == 5 && sample_on == 1) {
    if (pitch >= 60) {
      SPB_sound = pitch % NUM_SAMPLES;
      if (velocity > 0) {
        SPB_speed = save_speed; noise_flag = 0;
        SPB_flag = 1; SPB_counter = 0;
        SPB_max = sample_length_list[SPB_sound];
      } else { SPB_flag = 0; }
    }
    if (pitch < 60) {
      if (velocity > 0) {
        SPB_flag = 1; SPB_counter = 0;
        noise_flag = tri_flag;
        noise_velocity = 7 - (velocity >> 4);
        SPB_speed = 59 - pitch;
      } else { SPB_flag = 0; }
    }
  }

  else if (channel <= 5) {
    if (velocity > 0) {
      if (polyFlag == 1) {
        int found = -1;
        for (int i = 0; i < 6; i++) {
          if (polyBusy[i] == 0) { found = i; break; }
        }
        if (found < 0) {
          byte oldest = 255;
          for (int i = 0; i < 6; i++) {
            if (voiceAge[i] < oldest) { oldest = voiceAge[i]; found = i; }
          }
          writeMD(0, 0x28, ((found / 3 << 2) | (found % 3)));
        }
        channel = found;
        polyBusy[channel] = 1;
        voiceAge[channel] = ++voiceAgeCounter;
      }
      pitch = pitch - 64 + pitchOffset;
      pitchTracking[channel] = pitch;
      pitchDouble = pow(2, ((pitch % octDiv) + (0.015625 * bendAmount * (bend[channel] - 64)) + constantDouble) / octDiv) * 440;
      pitchInt = (uint16_t)pitchDouble;
      // YM2612 block field is 3 bits (0-7); saturate to avoid overflow
      // into bit 6 of register 0xA4 (silent drop to block 0 on v1.02).
      uint16_t blk = pitch / octDiv; if (blk > 7) blk = 7;
      pitchInt = (blk << 11) | pitchInt;
      writeMD(channel / 3, 0xa4 + (channel % 3), pitchInt >> 8);
      writeMD(channel / 3, 0xa0 + (channel % 3), pitchInt % 256);
      byte v = applyCurve(velocity);
      writeMD(channel / 3, 0x40 + (channel % 3), 127 - ((v * TL1[channel] + 63) / 127));
      writeMD(channel / 3, 0x44 + (channel % 3), 127 - ((v * TL2[channel] + 63) / 127));
      writeMD(channel / 3, 0x48 + (channel % 3), 127 - ((v * TL3[channel] + 63) / 127));
      writeMD(channel / 3, 0x4C + (channel % 3), 127 - ((v * TL4[channel] + 63) / 127));
      writeMD(0, 0x28, 0xf0 | ((channel / 3 << 2) | (channel % 3)));
    } else {
      if (polyFlag == 1) {
        for (int i = 0; i < 6; i++) {
          if (pitchTracking[i] == pitch) { channel = i; polyBusy[i] = 0; i = 6; }
        }
      }
      if (pitchTracking[channel] == pitch)
        writeMD(0, 0x28, ((channel / 3 << 2) | (channel % 3)));
    }
  }

  else if (channel >= 6 && channel <= 8 && pitch >= 45) {
    byte psgCh = channel - 6;
    if (velocity > 0) {
      pitchData[psgCh] = pitch; velocityData[psgCh] = velocity;
      writeFrequency(pitch, psgCh); writeAmplitude(velocity, psgCh);
    } else { velocityData[psgCh] = 0; writeAmplitude(0, psgCh); }
  }

  else if (channel == 9) {
    if (velocity > 0) {
      velocityData[3] = velocity;
      writeSN76489(noiseLookup[pitch % 12]);
      writeAmplitude(velocity, 3);
    } else { velocityData[3] = 0; writeAmplitude(0, 3); }
  }
}

// ==================== Note Off ====================
void doNoteOff(byte channel, byte pitch, byte velocity) {
  channel--;
  if (channel == 5 && sample_on == 1) SPB_flag = 0;

  if (channel <= 5) {
    if (polyFlag == 1) {
      for (int i = 0; i < 6; i++) {
        if (pitchTracking[i] == pitch) { channel = i; polyBusy[i] = 0; i = 6; }
      }
    }
    if (pitchTracking[channel] == pitch)
      writeMD(0, 0x28, ((channel / 3 << 2) | (channel % 3)));
  }
  else if (channel >= 6 && channel <= 8 && pitch >= 45) {
    velocityData[channel - 6] = 0; writeAmplitude(0, channel - 6);
  }
  else if (channel == 9) { velocityData[3] = 0; writeAmplitude(0, 3); }
}

// ==================== Control Change ====================
void doCC(byte channel, byte ccnumber, byte ccvalue) {
  channel--;

  if (channel <= 5) {
    if (ccnumber >= 100 && ccnumber <= 113) triangle[ccnumber - 100] = ccvalue << 1;

    switch (ccnumber) {
    case 79: writeMD(0, 0x2a, ccvalue << 1); break;
    case 14:
      regB0[channel] = (regB0[channel] | 0x07) & ((ccvalue >> 4) | 0x38);
      writeMD(channel / 3, 0xB0 + (channel % 3), regB0[channel]); break;
    case 15:
      regB0[channel] = (regB0[channel] | 0x38) & (((ccvalue >> 4) << 3) | 0x07);
      writeMD(channel / 3, 0xB0 + (channel % 3), regB0[channel]); break;
    case 16: TL1[channel] = ccvalue; writeMD(channel/3, 0x40+(channel%3), 127-((velocity*TL1[channel])/127)); break;
    case 17: TL2[channel] = ccvalue; writeMD(channel/3, 0x44+(channel%3), 127-((velocity*TL2[channel])/127)); break;
    case 18: TL3[channel] = ccvalue; writeMD(channel/3, 0x48+(channel%3), 127-((velocity*TL3[channel])/127)); break;
    case 19: TL4[channel] = ccvalue; writeMD(channel/3, 0x4C+(channel%3), 127-((velocity*TL4[channel])/127)); break;
    case 20: reg30[channel]=(reg30[channel]|0x0F)&((ccvalue>>3)|0x70); writeMD(channel/3,0x30+(channel%3),reg30[channel]); break;
    case 21: reg34[channel]=(reg34[channel]|0x0F)&((ccvalue>>3)|0x70); writeMD(channel/3,0x34+(channel%3),reg34[channel]); break;
    case 22: reg38[channel]=(reg38[channel]|0x0F)&((ccvalue>>3)|0x70); writeMD(channel/3,0x38+(channel%3),reg38[channel]); break;
    case 23: reg3c[channel]=(reg3c[channel]|0x0F)&((ccvalue>>3)|0x70); writeMD(channel/3,0x3c+(channel%3),reg3c[channel]); break;
    case 24: reg30[channel]=(reg30[channel]|0x70)&(((ccvalue>>4)<<4)|0x0F); writeMD(channel/3,0x30+(channel%3),reg30[channel]); break;
    case 25: reg34[channel]=(reg34[channel]|0x70)&(((ccvalue>>4)<<4)|0x0F); writeMD(channel/3,0x34+(channel%3),reg34[channel]); break;
    case 26: reg38[channel]=(reg38[channel]|0x70)&(((ccvalue>>4)<<4)|0x0F); writeMD(channel/3,0x38+(channel%3),reg38[channel]); break;
    case 27: reg3c[channel]=(reg3c[channel]|0x70)&(((ccvalue>>4)<<4)|0x0F); writeMD(channel/3,0x3c+(channel%3),reg3c[channel]); break;
    case 39: reg50[channel]=(reg50[channel]|0xC0)&((ccvalue<<1)|0x1F); writeMD(channel/3,0x50+(channel%3),reg50[channel]); break;
    case 40: reg54[channel]=(reg54[channel]|0xC0)&((ccvalue<<1)|0x1F); writeMD(channel/3,0x54+(channel%3),reg54[channel]); break;
    case 41: reg58[channel]=(reg58[channel]|0xC0)&((ccvalue<<1)|0x1F); writeMD(channel/3,0x58+(channel%3),reg58[channel]); break;
    case 42: reg5c[channel]=(reg5c[channel]|0xC0)&((ccvalue<<1)|0x1F); writeMD(channel/3,0x5c+(channel%3),reg5c[channel]); break;
    case 43: reg50[channel]=(reg50[channel]|0x1F)&((ccvalue>>2)|0xC0); writeMD(channel/3,0x50+(channel%3),reg50[channel]); break;
    case 44: reg54[channel]=(reg54[channel]|0x1F)&((ccvalue>>2)|0xC0); writeMD(channel/3,0x54+(channel%3),reg54[channel]); break;
    case 45: reg58[channel]=(reg58[channel]|0x1F)&((ccvalue>>2)|0xC0); writeMD(channel/3,0x58+(channel%3),reg58[channel]); break;
    case 46: reg5c[channel]=(reg5c[channel]|0x1F)&((ccvalue>>2)|0xC0); writeMD(channel/3,0x5c+(channel%3),reg5c[channel]); break;
    case 70: reg60[channel]=(reg60[channel]|0x80)&((ccvalue<<1)|0x1F); writeMD(channel/3,0x60+(channel%3),reg60[channel]); break;
    case 71: reg64[channel]=(reg64[channel]|0x80)&((ccvalue<<1)|0x1F); writeMD(channel/3,0x64+(channel%3),reg64[channel]); break;
    case 72: reg68[channel]=(reg68[channel]|0x80)&((ccvalue<<1)|0x1F); writeMD(channel/3,0x68+(channel%3),reg68[channel]); break;
    case 73: reg6c[channel]=(reg6c[channel]|0x80)&((ccvalue<<1)|0x1F); writeMD(channel/3,0x6c+(channel%3),reg6c[channel]); break;
    case 47: reg60[channel]=(reg60[channel]|0x1F)&((ccvalue>>2)|0x80); writeMD(channel/3,0x60+(channel%3),reg60[channel]); break;
    case 48: reg64[channel]=(reg64[channel]|0x1F)&((ccvalue>>2)|0x80); writeMD(channel/3,0x64+(channel%3),reg64[channel]); break;
    case 49: reg68[channel]=(reg68[channel]|0x1F)&((ccvalue>>2)|0x80); writeMD(channel/3,0x68+(channel%3),reg68[channel]); break;
    case 50: reg6c[channel]=(reg6c[channel]|0x1F)&((ccvalue>>2)|0x80); writeMD(channel/3,0x6c+(channel%3),reg6c[channel]); break;
    case 51: writeMD(channel/3, 0x70+(channel%3), ccvalue>>3); break;
    case 52: writeMD(channel/3, 0x74+(channel%3), ccvalue>>3); break;
    case 53: writeMD(channel/3, 0x78+(channel%3), ccvalue>>3); break;
    case 54: writeMD(channel/3, 0x7c+(channel%3), ccvalue>>3); break;
    case 55: reg80[channel]=(reg80[channel]|0xF0)&((ccvalue<<1)|0x0F); writeMD(channel/3,0x80+(channel%3),reg80[channel]); break;
    case 56: reg84[channel]=(reg84[channel]|0xF0)&((ccvalue<<1)|0x0F); writeMD(channel/3,0x84+(channel%3),reg84[channel]); break;
    case 57: reg88[channel]=(reg88[channel]|0xF0)&((ccvalue<<1)|0x0F); writeMD(channel/3,0x88+(channel%3),reg88[channel]); break;
    case 58: reg8c[channel]=(reg8c[channel]|0xF0)&((ccvalue<<1)|0x0F); writeMD(channel/3,0x8c+(channel%3),reg8c[channel]); break;
    case 59: reg80[channel]=(reg80[channel]|0x0F)&((ccvalue>>3)|0xF0); writeMD(channel/3,0x80+(channel%3),reg80[channel]); break;
    case 60: reg84[channel]=(reg84[channel]|0x0F)&((ccvalue>>3)|0xF0); writeMD(channel/3,0x84+(channel%3),reg84[channel]); break;
    case 61: reg88[channel]=(reg88[channel]|0x0F)&((ccvalue>>3)|0xF0); writeMD(channel/3,0x88+(channel%3),reg88[channel]); break;
    case 62: reg8c[channel]=(reg8c[channel]|0x0F)&((ccvalue>>3)|0xF0); writeMD(channel/3,0x8c+(channel%3),reg8c[channel]); break;
    case 1: reg22=(reg22|0x07)&((ccvalue>>4)|0x08); writeMD(0,0x22,reg22); break;
    case 74: reg22=(reg22|0x08)&(((ccvalue>>6)<<3)|0x07); writeMD(0,0x22,reg22); break;
    case 75: regB4[channel]=(regB4[channel]|0x07)&((ccvalue>>4)|0xF8); writeMD(channel/3,0xB4+(channel%3),regB4[channel]); break;
    case 76: regB4[channel]=(regB4[channel]|0x30)&(((ccvalue>>4)<<3)|0xC7); writeMD(channel/3,0xB4+(channel%3),regB4[channel]); break;
    case 77: regB4[channel]=(regB4[channel]|0xC0)&(((ccvalue>>5)<<6)|0x3F); writeMD(channel/3,0xB4+(channel%3),regB4[channel]); break;
    case 78: writeMD(0, 0x2B, ccvalue << 1); sample_on = ccvalue >> 6; break;
    case 81: bendAmount = ccvalue / 18; break;
    case 82: velocityCurveEnabled = (ccvalue > 0) ? 1 : 0; break; // velocity curve on/off
    case 83: constantDouble = (ccvalue > 63) ? 6.41 : 6.711; break;
    case 84: octDiv = constrain(ccvalue + 1, 1, 24); break;
    case 85: pitchOffset = ccvalue; break;
    case 86: SPB_speed = ccvalue; save_speed = SPB_speed; break;
    case 87: polyFlag = ccvalue >> 6; break;
    case 88: overSamp = (ccvalue >> 3) + 1; break;
    case 89: tri_flag = (ccvalue >> 6) + 1; break;

    // SSG-EG (CC 90-93) - YM2612 registers 0x90-0x9C
    case 90: writeMD(channel/3, 0x90 + (channel%3), ccvalue >> 3); break;
    case 91: writeMD(channel/3, 0x94 + (channel%3), ccvalue >> 3); break;
    case 92: writeMD(channel/3, 0x98 + (channel%3), ccvalue >> 3); break;
    case 93: writeMD(channel/3, 0x9C + (channel%3), ccvalue >> 3); break;
    case 120: // All Sound Off
    case 123: // All Notes Off
      for (int i = 0; i < 6; i++) {
        writeMD(0, 0x28, ((i / 3 << 2) | (i % 3)));
        polyBusy[i] = 0;
      }
      for (int i = 0; i < 4; i++) { writeAmplitude(0, i); velocityData[i] = 0; }
      SPB_flag = 0;
      break;

    case 9: {
      byte idx = min((byte)(ccvalue / 8), (byte)15);
      doCC(channel+1,14,ALGO[idx]); doCC(channel+1,15,FB[idx]);
      doCC(channel+1,16,TLOP1[idx]); doCC(channel+1,17,TLOP2[idx]);
      doCC(channel+1,18,TLOP3[idx]); doCC(channel+1,19,TLOP4[idx]);
      doCC(channel+1,20,MULOP1[idx]); doCC(channel+1,21,MULOP2[idx]);
      doCC(channel+1,22,MULOP3[idx]); doCC(channel+1,23,MULOP4[idx]);
      doCC(channel+1,24,DETUNEOP1[idx]); doCC(channel+1,25,DETUNEOP2[idx]);
      doCC(channel+1,26,DETUNEOP3[idx]); doCC(channel+1,27,DETUNEOP4[idx]);
      doCC(channel+1,43,ATTACKOP1[idx]); doCC(channel+1,44,ATTACKOP2[idx]);
      doCC(channel+1,45,ATTACKOP3[idx]); doCC(channel+1,46,ATTACKOP4[idx]);
      doCC(channel+1,47,DECAY1OP1[idx]); doCC(channel+1,48,DECAY1OP2[idx]);
      doCC(channel+1,49,DECAY1OP3[idx]); doCC(channel+1,50,DECAY1OP4[idx]);
      doCC(channel+1,51,DECAY2OP1[idx]); doCC(channel+1,52,DECAY2OP2[idx]);
      doCC(channel+1,53,DECAY2OP3[idx]); doCC(channel+1,54,DECAY2OP4[idx]);
      doCC(channel+1,55,AMP2OP1[idx]); doCC(channel+1,56,AMP2OP2[idx]);
      doCC(channel+1,57,AMP2OP3[idx]); doCC(channel+1,58,AMP2OP4[idx]);
      doCC(channel+1,59,RELOP1[idx]); doCC(channel+1,60,RELOP2[idx]);
      doCC(channel+1,61,RELOP3[idx]); doCC(channel+1,62,RELOP4[idx]);
      break;
    }
    }
  }

  else if (channel >= 6 && channel <= 8) {
    byte psgCh = channel - 6;
    if (ccnumber == 83 && psgCh == 0) pitchTableOffset = (ccvalue > 63) ? 84 : 0;
    else if (ccnumber == 42) { coarsePitch[psgCh] = ccvalue; writeFrequency(pitchData[psgCh], psgCh); }
    else if (ccnumber == 11) { velocityData[psgCh] = ccvalue; writeAmplitude(ccvalue, psgCh); }
  }
}

// ==================== Pitch Bend ====================
void doBend(byte channel, int bend_usb) {
  channel--;
  // FortySevenEffects sends -8192..+8191, convert to 0..16383
  uint16_t bendVal = (uint16_t)(bend_usb + 8192);
  bendMSB = (bendVal >> 7) & 0x7F;
  bendLSB = bendVal & 0x7F;

  if (channel <= 5) {
    bend[channel] = bendMSB;
    pitchDouble = pow(2, ((pitchTracking[channel] % octDiv) + (0.015625 * bendAmount * (bend[channel] - 64)) + constantDouble) / octDiv) * 440;
    pitchInt = (uint16_t)pitchDouble;
    uint16_t blk = pitchTracking[channel] / octDiv; if (blk > 7) blk = 7;
    pitchInt = (blk << 11) | pitchInt;
    writeMD(channel / 3, 0xa4 + (channel % 3), pitchInt >> 8);
    writeMD(channel / 3, 0xa0 + (channel % 3), pitchInt % 256);
  }
  else if (channel >= 6 && channel <= 8) {
    byte psgCh = channel - 6;
    bend_MSB[psgCh] = bendMSB;
    bend_data[psgCh] = bendMSB;
    writeFrequency(pitchData[psgCh], psgCh);
  }
}

// ==================== Sample Playback ====================
void doSample() {
  if (SPB_flag == 1 && sample_on == 1) {
    if (SPB_counter >= SPB_max && noise_flag == 0) { SPB_flag = 0; SPB_counter = 0; }

    if (SPB_tick >= SPB_speed && noise_flag == 1) {
      SPB_counter += overSamp; SPB_tick = 0;
      writeMD(0, 0x2a, byte(random(256)) >> noise_velocity);
    }
    if (SPB_tick >= SPB_speed && noise_flag == 2) {
      SPB_counter += overSamp; SPB_tick = 0;
      writeMD(0, 0x2a, triangle[SPB_counter % 14] >> noise_velocity);
    }
    else if (SPB_tick >= SPB_speed && noise_flag == 0) {
      SPB_tick = 0;
      if (SPB_sound < NUM_SAMPLES)
        writeMD(0, 0x2a, sample_ptrs[SPB_sound][SPB_counter]);
      SPB_counter += overSamp;
    }
    if (SPB_tick < SPB_speed) SPB_tick++;
  }
}

// ==================== MIDI Callbacks ====================
// Bend coalescing: a flood of bend messages on one channel collapses to
// the most recent value, flushed once per main-loop iteration.
volatile bool bendPending[6] = {false, false, false, false, false, false};
volatile int  bendPendingValue[6];

void doProgramChange(byte channel, byte program) {
  doCC(channel, 9, program);
}

// Non-blocking LED flash - flash purple for note on, turn off when elapsed
volatile unsigned long ledFlashUntil = 0;
inline void flashLED() {
  pixel.setPixelColor(0, pixel.Color(30, 0, 40)); // purple (dim)
  pixel.show();
  ledFlashUntil = millis() + 50; // 50ms flash
}

void handleNoteOn(byte ch, byte note, byte vel) {
  if (vel == 0) { doNoteOff(ch, note, 0); }
  else { flashLED(); doNote(ch, note, vel); }
}
void handleNoteOff(byte ch, byte note, byte vel) { doNoteOff(ch, note, vel); }
void handleCC(byte ch, byte num, byte val) { doCC(ch, num, val); }
void handlePitchBend(byte ch, int val) {
  byte i = ch - 1;
  if (i < 6) { bendPendingValue[i] = val; bendPending[i] = true; }
  else doBend(ch, val);  // PSG channels: handle inline (cheap)
}
void handleProgramChange(byte ch, byte pgm) { doProgramChange(ch, pgm); }

// ==================== Setup ====================
void setup() {
  // Debug LED (NeoPixel on GP16)
  pixel.begin();
  pixel.setPixelColor(0, pixel.Color(0, 0, 50));  // dim blue
  pixel.show(); delay(300);
  pixel.setPixelColor(0, 0);
  pixel.show(); delay(200);
  pixel.setPixelColor(0, pixel.Color(0, 50, 0));  // dim green
  pixel.show(); delay(300);
  pixel.setPixelColor(0, 0);
  pixel.show();

  pinMode(PIN_BIT0, OUTPUT); pinMode(PIN_BIT1, OUTPUT);
  pinMode(PIN_BIT2, OUTPUT); pinMode(PIN_BIT3, OUTPUT);
  pinMode(PIN_NB, OUTPUT); pinMode(PIN_AD, OUTPUT); pinMode(PIN_WR, OUTPUT);

  setWR(false); setNB(false); setAD(false); outputNibble(0x00);

  // USB MIDI
  usb_midi.setStringDescriptor("GenMDM Pico v1.02");
  usbMIDI.begin(MIDI_CHANNEL_OMNI);
  usbMIDI.setHandleNoteOn(handleNoteOn);
  usbMIDI.setHandleNoteOff(handleNoteOff);
  usbMIDI.setHandleControlChange(handleCC);
  usbMIDI.setHandlePitchBend(handlePitchBend);
  usbMIDI.setHandleProgramChange(handleProgramChange);

  // Serial MIDI (TRS input on GP1/Serial1 RX via 6N137)
  Serial1.setFIFOSize(256);
  Serial1.setRX(1);
  serialMIDI.begin(MIDI_CHANNEL_OMNI);
  serialMIDI.setHandleNoteOn(handleNoteOn);
  serialMIDI.setHandleNoteOff(handleNoteOff);
  serialMIDI.setHandleControlChange(handleCC);
  serialMIDI.setHandlePitchBend(handlePitchBend);
  serialMIDI.setHandleProgramChange(handleProgramChange);

  delay(550); // Wait for cart ROM YM2612 init

  // Match original v1.02 firmware setup (verified against hex disassembly)
  doCC(1,77,127); delay(1); doCC(2,77,127); delay(1);
  doCC(3,77,127); delay(1); doCC(4,77,127); delay(1);
  doCC(5,77,127); delay(1); doCC(6,77,127); delay(1);
  // Original loads preset 1 (bass) on all 6 channels via CC 9 = 8
  doCC(1,9,8); delay(1); doCC(2,9,8); delay(1);
  doCC(3,9,8); delay(1); doCC(4,9,8); delay(1);
  doCC(5,9,8); delay(1); doCC(6,9,8); delay(1);

  // Startup melody (ascending arpeggio)
  delay(100);
  doNote(1,60,120); delay(120); doNote(1,60,0); delay(30);
  doNote(2,64,120); delay(120); doNote(2,64,0); delay(30);
  doNote(3,67,120); delay(120); doNote(3,67,0); delay(30);
  doNote(4,72,120); delay(120); doNote(4,72,0); delay(30);
  doNote(5,76,115); delay(120); doNote(5,76,0); delay(30);
  doNote(6,79,110); delay(300); doNote(6,79,0);

  randomSeed(rp2040.hwrand32());
}

// ==================== Main Loop ====================
void loop() {
  doSample();
  serialMIDI.read();
  usbMIDI.read();

  for (byte i = 0; i < 6; i++) {
    if (bendPending[i]) { bendPending[i] = false; doBend(i + 1, bendPendingValue[i]); }
  }

  // Turn off LED after flash duration
  if (ledFlashUntil != 0 && millis() > ledFlashUntil) {
    pixel.setPixelColor(0, 0);
    pixel.show();
    ledFlashUntil = 0;
  }
}
