#pragma once
#include <Arduino.h>

void oled_init();
void oled_render(uint32_t now_ms);

// API per impostare cosa mostrare
void oled_setPage(uint8_t page);
void oled_setTouchedParam(int8_t encIdx);   // -1 = nessuno
void oled_pulsePageBanner();                  // mostra banner cambio pagina

// Pagine
#define PAGE_OSC 0
#define PAGE_ENV 1
#define PAGE_MOD 2
#define PAGE_FX  3
#define N_PAGES 4

extern const char* pageNames[N_PAGES];
