#include "events.h"

volatile Event evtBuf[EVT_BUF_SIZE];
volatile uint16_t evtHead = 0;
volatile uint16_t evtTail = 0;

bool popEvent(Event* out) {
  if (evtTail == evtHead) return false;
  noInterrupts();
  *out = const_cast<Event&>(evtBuf[evtTail]);
  evtTail = (evtTail + 1) & (EVT_BUF_SIZE - 1);
  interrupts();
  return true;
}
