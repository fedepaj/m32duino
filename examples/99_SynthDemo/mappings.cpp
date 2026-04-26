#include "mappings.h"
#include <math.h>

#define SAMPLE_RATE 22050

const char* btnNames[N_BUTTONS] = {
  "shift", "scale_edit", "arp_edit", "undo_redo", "quantize_auto",
  "ideas", "loop", "metro", "tempo", "preset_up", "preset_down",
  "play_restart", "rec_countin", "stop_clear", "left_m", "right_s",
  "track_instance", "plugin_midi", "browser", "oct_down", "oct_up"
};

const char* noteNames[12] = {
  "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
};

const uint8_t btnToLed[N_BUTTONS] = {
  0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20
};

int16_t waveSin[256];
int16_t waveTri[256];
int16_t waveSaw[256];
int16_t waveSqr[256];

uint32_t noteToInc[128];

// LUT scan: btnLUT[col][slot] e keyLUT[col][slot]
static uint8_t btnLUT[8][16];
static uint8_t keyLUT[8][16];

void mappings_init() {
  // Wavetables
  for (int i = 0; i < 256; i++) {
    waveSin[i] = (int16_t)(sinf(2.0f * M_PI * i / 256.0f) * 32767);
    int v = (i < 128) ? (i * 2 - 128) : (383 - i * 2);
    waveTri[i] = (int16_t)(v * 256);
    waveSaw[i] = (int16_t)((i - 128) * 256);
    waveSqr[i] = (i < 128) ? -32767 : 32767;
  }
  
  // Note → phase increment
  // freq = 440 * 2^((n-69)/12)
  // inc = freq / SR * 2^32
  for (int n = 0; n < 128; n++) {
    float freq = 440.0f * powf(2.0f, (n - 69) / 12.0f);
    noteToInc[n] = (uint32_t)(freq / (float)SAMPLE_RATE * 4294967296.0f);
  }
  
  // Init LUT a 0xFF
  for (int c = 0; c < 8; c++)
    for (int s = 0; s < 16; s++) {
      btnLUT[c][s] = 0xFF;
      keyLUT[c][s] = 0xFF;
    }
  
  // Pulsanti
  struct B { uint8_t c, s, i; };
  B btns[] = {
    {0,0,0},{1,0,1},{2,0,2},{3,0,3},{4,0,4},{5,0,5},
    {0,2,6},{1,2,7},{2,2,8},{3,1,9},{4,1,10},{3,2,11},
    {4,2,12},{5,2,13},{6,1,14},{7,1,15},{2,1,16},{1,1,17},
    {0,1,18},{7,2,19},{6,2,20}
  };
  for (auto& b : btns) btnLUT[b.c][b.s] = b.i;
  
  // Tasti
  struct K { uint8_t c, s, i; bool isBot; };
  K keys[] = {
    {7,14,0,false},{7,15,0,true},{6,14,1,false},{6,15,1,true},
    {5,14,2,false},{5,15,2,true},{4,14,3,false},{4,15,3,true},
    {3,14,4,false},{3,15,4,true},{2,14,5,false},{2,15,5,true},
    {1,14,6,false},{1,15,6,true},{0,14,7,false},{0,15,7,true},
    {7,12,8,false},{7,13,8,true},{6,12,9,false},{6,13,9,true},
    {5,12,10,false},{5,13,10,true},{4,12,11,false},{4,13,11,true},
    {3,12,12,false},{3,13,12,true},{2,12,13,false},{2,13,13,true},
    {1,12,14,false},{1,13,14,true},{0,12,15,false},{0,13,15,true},
    {7,10,16,false},{7,11,16,true},{6,10,17,false},{6,11,17,true},
    {5,10,18,false},{5,11,18,true},{4,10,19,false},{4,11,19,true},
    {3,10,20,false},{3,11,20,true},{2,10,21,false},{2,11,21,true},
    {1,10,22,false},{1,11,22,true},{0,10,23,false},{0,11,23,true},
    {7,8,24,false},{7,9,24,true},{6,8,25,false},{6,9,25,true},
    {5,8,26,false},{5,9,26,true},{4,8,27,false},{4,9,27,true},
    {3,8,28,false},{3,9,28,true},{2,8,29,false},{2,9,29,true},
    {1,8,30,false},{1,9,30,true},{0,8,31,false},{0,9,31,true}
  };
  for (auto& k : keys) keyLUT[k.c][k.s] = k.i | (k.isBot ? 0x80 : 0);
}

const int16_t* getWaveTable(uint8_t w) {
  switch (w) {
    case 0: return waveSin;
    case 1: return waveTri;
    case 2: return waveSaw;
    default: return waveSqr;
  }
}

uint8_t lookupBtn(uint8_t col, uint8_t slot) {
  return btnLUT[col][slot];
}

uint8_t lookupKey(uint8_t col, uint8_t slot) {
  return keyLUT[col][slot];
}
