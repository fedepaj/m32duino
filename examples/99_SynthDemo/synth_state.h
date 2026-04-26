#pragma once
#include <Arduino.h>

// Parametri del synth, scritti dal main loop in risposta a eventi,
// letti dall'audio engine. Tipi 16-bit per atomicità su Cortex-M3.
struct SynthParams {
  // OSC
  volatile uint8_t wave1;       // 0..3 (sin/tri/saw/sqr)
  volatile uint8_t wave2;
  volatile uint16_t mix;        // 0..65535 (0=osc1 only, FFFF=osc2 only)
  volatile int16_t detune;      // ±100 cents
  
  // FILTER
  volatile uint16_t cutoff;     // 0..65535
  volatile uint16_t reso;       // 0..65535 (placeholder, 1-pole non usa reso)
  
  // GLIDE / VOL
  volatile uint16_t glide;      // 0..65535 (slew rate, 0=instant, FFFF=slowest)
  volatile uint16_t volume;     // 0..65535
  
  // AMP ADSR
  volatile uint16_t aAtt, aDec, aSus, aRel;   // ms ms 0..FFFF ms
  
  // FILTER ADSR
  volatile uint16_t fAtt, fDec, fSus, fRel;
  volatile int16_t fEnvAmount;  // -32768..+32767 (signed)
  
  // LFO
  volatile uint8_t lfoWave;     // 0..3
  volatile uint16_t lfoRate;    // freq (encoding da decidere, vedi PH4)
  volatile uint16_t lfoDepth;   // 0..65535
  volatile uint8_t lfoTarget;   // 0=pitch 1=cutoff 2=amp
  
  // ARP
  volatile bool arpOn;
  volatile uint16_t arpBpm;     // 30..300
  
  // Pitch bend & mod (da touch strip)
  volatile int16_t pitchBend;   // -8192..+8191 (semitoni × 4096)
  volatile uint8_t modWheel;    // 0..127
  
  // Octave shift (da pulsanti)
  volatile int8_t octaveShift;  // -2..+2
};

extern SynthParams synth;

void synth_init_defaults();
