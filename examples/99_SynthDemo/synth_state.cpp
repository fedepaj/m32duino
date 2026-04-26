#include "synth_state.h"

SynthParams synth;

void synth_init_defaults() {
  synth.wave1 = 0;        // sin
  synth.wave2 = 2;        // saw
  synth.mix = 32768;      // 50/50
  synth.detune = 0;
  synth.cutoff = 50000;
  synth.reso = 0;
  synth.glide = 0;        // no glide
  synth.volume = 32768;   // 50%
  
  synth.aAtt = 50;        // ms
  synth.aDec = 200;
  synth.aSus = 40000;     // 0..FFFF
  synth.aRel = 300;
  
  synth.fAtt = 50;
  synth.fDec = 200;
  synth.fSus = 40000;
  synth.fRel = 300;
  synth.fEnvAmount = 0;
  
  synth.lfoWave = 0;
  synth.lfoRate = 100;    // [PH4] da definire encoding
  synth.lfoDepth = 0;
  synth.lfoTarget = 0;
  
  synth.arpOn = false;
  synth.arpBpm = 120;
  
  synth.pitchBend = 0;
  synth.modWheel = 0;
  synth.octaveShift = 0;
}
