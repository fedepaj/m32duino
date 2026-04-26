#include "scan.h"
#include "events.h"
#include "mappings.h"

#define MASK_CTRL (0x4007)
#define COL_SHIFT 8
#define N_COLS 8
#define N_SLOTS 16

static volatile uint8_t currentStep = 0;
static volatile bool pressedState[N_COLS][N_SLOTS];
static volatile uint32_t keyTopTime[N_KEYS];
static volatile bool keyTopPressed[N_KEYS];

static HardwareTimer *scanTim;

static inline uint8_t velocityFromDelta(uint32_t d) {
  if (d < 500) return 127;
  if (d > 50000) return 1;
  uint32_t v = 127 - ((d - 500) * 126 / 49500);
  return (v < 1) ? 1 : (v > 127 ? 127 : v);
}

static void scan_isr() {
  uint8_t slot = currentStep;
  uint16_t idr = (GPIOC->IDR >> COL_SHIFT) & 0xFF;
  uint32_t now_us = micros();
  
  for (uint8_t c = 0; c < N_COLS; c++) {
    bool press = !((idr >> c) & 1);
    if (press == pressedState[c][slot]) continue;
    pressedState[c][slot] = press;
    
    uint8_t btn = lookupBtn(c, slot);
    if (btn != 0xFF) {
      pushEvent(press ? EVT_BTN_DOWN : EVT_BTN_UP, btn, 0);
      continue;
    }
    uint8_t k = lookupKey(c, slot);
    if (k != 0xFF) {
      bool isBot = (k & 0x80) != 0;
      uint8_t ki = k & 0x7F;
      if (!isBot) {
        if (press) { keyTopTime[ki] = now_us; keyTopPressed[ki] = true; }
        else { keyTopPressed[ki] = false; pushEvent(EVT_NOTE_OFF, ki, 0); }
      } else if (press && keyTopPressed[ki]) {
        pushEvent(EVT_NOTE_ON, ki, velocityFromDelta(now_us - keyTopTime[ki]));
      }
    }
  }
  
  uint8_t next = (slot + 1) & 0x0F;
  currentStep = next;
  uint32_t val = (next & 0x07) | ((next & 0x08) << 11);
  GPIOB->BSRR = (MASK_CTRL << 16) | val;
}

void scan_init() {
  // Pin output: PB0, PB1, PB2, PB14
  pinMode(PB0, OUTPUT);
  pinMode(PB1, OUTPUT);
  pinMode(PB2, OUTPUT);
  pinMode(PB14, OUTPUT);
  
  // Colonne input pullup
  const uint32_t colPins[N_COLS] = {PC8, PC9, PC10, PC11, PC12, PC13, PC14, PC15};
  for (int i = 0; i < N_COLS; i++) pinMode(colPins[i], INPUT_PULLUP);
  
  for (int c = 0; c < N_COLS; c++)
    for (int s = 0; s < N_SLOTS; s++) pressedState[c][s] = false;
  for (int k = 0; k < N_KEYS; k++) {
    keyTopTime[k] = 0; keyTopPressed[k] = false;
  }
  
  scanTim = new HardwareTimer(TIM2);
  scanTim->setOverflow(50, MICROSEC_FORMAT);  // 50us tick
  scanTim->attachInterrupt(scan_isr);
  scanTim->resume();
}
