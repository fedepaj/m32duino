#pragma once
#include <Arduino.h>

#define SAMPLE_RATE 22050
#define OVERSAMP    16
#define CARRIER     (SAMPLE_RATE * OVERSAMP)
#define DMA_BUF_SIZE 512   // 256 carrier ticks per metà = 16 sample audio per metà

void audio_init();

// Triggera nuova nota (legato se voce già attiva)
void audio_noteOn(uint8_t midi, uint8_t velocity);

// Rilascia nota corrente (avvia release ADSR)
void audio_noteOff();

// True se voice attiva (ADSR non in idle)
bool audio_voiceActive();

// Nota attualmente suonata (valido solo se active)
uint8_t audio_currentNote();

// Stato envelope per visualizzazioni (0..65535)
uint16_t audio_currentAmpEnv();
uint16_t audio_currentFiltEnv();

// Phase corrente di osc1 (top 8 bit) per animazioni waveform
uint8_t audio_currentPhase();

void audio_updateEnvCoefs();

