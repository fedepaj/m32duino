#pragma once
#include <Arduino.h>

enum EvtType {
  EVT_NOTE_ON,
  EVT_NOTE_OFF,
  EVT_BTN_DOWN,
  EVT_BTN_UP,
  EVT_ENC_DELTA,
  EVT_TOUCH_ENC_ON,
  EVT_TOUCH_ENC_OFF,
  EVT_TOUCH_PITCH,    // value = position 0..127
  EVT_TOUCH_PITCH_OFF,
  EVT_TOUCH_MOD,
  EVT_TOUCH_MOD_OFF,
  EVT_JOY_DELTA,
  EVT_JOY_BTN_DOWN,
  EVT_JOY_BTN_UP
};

struct Event {
  uint8_t type;
  uint8_t idx;
  int16_t value;
};

#define EVT_BUF_SIZE 128
extern volatile Event evtBuf[EVT_BUF_SIZE];
extern volatile uint16_t evtHead, evtTail;

static inline void pushEvent(uint8_t type, uint8_t idx, int16_t val) {
  uint16_t h = evtHead;
  uint16_t nh = (h + 1) & (EVT_BUF_SIZE - 1);
  if (nh != evtTail) {
    evtBuf[h].type = type;
    evtBuf[h].idx = idx;
    evtBuf[h].value = val;
    evtHead = nh;
  }
}

bool popEvent(Event* out);
