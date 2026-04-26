#pragma once
#include <Arduino.h>

#define N_BUTTONS 21
#define N_KEYS 32
#define MIDI_NOTE_START 41   // F2

extern const char* btnNames[N_BUTTONS];
extern const char* noteNames[12];
extern const uint8_t btnToLed[N_BUTTONS];

// Indici simbolici dei pulsanti (per leggibilità)
enum BtnId {
  B_SHIFT=0, B_SCALE_EDIT, B_ARP_EDIT, B_UNDO_REDO, B_QUANTIZE_AUTO,
  B_IDEAS, B_LOOP, B_METRO, B_TEMPO, B_PRESET_UP, B_PRESET_DOWN,
  B_PLAY_RESTART, B_REC_COUNTIN, B_STOP_CLEAR, B_LEFT_M, B_RIGHT_S,
  B_TRACK_INSTANCE, B_PLUGIN_MIDI, B_BROWSER, B_OCT_DOWN, B_OCT_UP
};

// Wavetable
extern int16_t waveSin[256];
extern int16_t waveTri[256];
extern int16_t waveSaw[256];
extern int16_t waveSqr[256];

// MIDI note -> phase increment (Q24.8 fixed point)
// Pre-calcolato in init per evitare powf() runtime
extern uint32_t noteToInc[128];

void mappings_init();
const int16_t* getWaveTable(uint8_t w);

// Lookup scan matrix
// Restituisce: 0xFF = libero, 0..N_BUTTONS-1 = button index
uint8_t lookupBtn(uint8_t col, uint8_t slot);

// Restituisce: 0xFF = libero, oppure (idx 0..31) | (0x80 se è BOT)
uint8_t lookupKey(uint8_t col, uint8_t slot);
