#include "sys_clock.h"
#include "events.h"
#include "synth_state.h"
#include "mappings.h"
#include "audio.h"
#include "scan.h"
#include "encoder.h"
#include "touch.h"
#include "led.h"
#include "oled.h"

// Stack delle note tenute fisicamente (mono legato priority)
#define HELD_MAX 16
static uint8_t heldNotes[HELD_MAX];
static uint8_t nHeld = 0;

static void heldPush(uint8_t n) {
  for (int i = 0; i < nHeld; i++) if (heldNotes[i] == n) return;
  if (nHeld < HELD_MAX) heldNotes[nHeld++] = n;
}
static void heldRemove(uint8_t n) {
  for (int i = 0; i < nHeld; i++) {
    if (heldNotes[i] == n) {
      for (int j = i; j < nHeld - 1; j++) heldNotes[j] = heldNotes[j+1];
      nHeld--;
      return;
    }
  }
}

// Aggiorna parametro in base a delta encoder
static void applyEncoder(uint8_t enc, int16_t delta);

// Cambia pagina
static uint8_t currentPage = 0;

void setup() {
  // Clock 72MHz prima di tutto
  clock_init_72mhz();
  
  // USB pullup HIGH
  
  // Default params
  synth_init_defaults();
  mappings_init();
  
  // Periferiche
  audio_init();
  scan_init();
  encoder_init();
  touch_init();
  led_init();
  oled_init();
}

void loop() {
  uint32_t now = millis();
  
  // Polling I2C
  touch_poll(now);
  
  // Eventi
  Event e;
  while (popEvent(&e)) {
    switch (e.type) {
      case EVT_NOTE_ON: {
        uint8_t midi = MIDI_NOTE_START + e.idx + synth.octaveShift * 12;
        if (synth.arpOn) {
          heldPush(midi);
          // arp lo gestiamo a parte
        } else {
          heldPush(midi);
          audio_noteOn(midi, e.value);
        }
        break;
      }
      case EVT_NOTE_OFF: {
        uint8_t midi = MIDI_NOTE_START + e.idx + synth.octaveShift * 12;
        heldRemove(midi);
        if (synth.arpOn) break;
        if (nHeld == 0) {
          audio_noteOff();
        } else {
          // legato: suona la più recente delle altre tenute
          audio_noteOn(heldNotes[nHeld - 1], 100);
        }
        break;
      }
      case EVT_BTN_DOWN:
        switch (e.idx) {
          case B_PLAY_RESTART:
            synth.arpOn = true;
            led_set(B_PLAY_RESTART, 0xFF);
            break;
          case B_STOP_CLEAR:
            synth.arpOn = false;
            nHeld = 0;
            audio_noteOff();
            led_set(B_PLAY_RESTART, 0);
            led_set(B_STOP_CLEAR, 0xFF);
            break;
          case B_OCT_DOWN:
            if (synth.octaveShift > -2) synth.octaveShift--;
            led_set(B_OCT_DOWN, synth.octaveShift < 0 ? 0xFF : 0);
            led_set(B_OCT_UP, synth.octaveShift > 0 ? 0xFF : 0);
            break;
          case B_OCT_UP:
            if (synth.octaveShift < 2) synth.octaveShift++;
            led_set(B_OCT_DOWN, synth.octaveShift < 0 ? 0xFF : 0);
            led_set(B_OCT_UP, synth.octaveShift > 0 ? 0xFF : 0);
            break;
          case B_BROWSER:
            currentPage = (currentPage + 1) % N_PAGES;
            oled_setPage(currentPage);
            oled_pulsePageBanner();
            led_set(B_BROWSER, 0xFF);
            break;
        }
        break;
      case EVT_BTN_UP:
        if (e.idx == B_STOP_CLEAR) led_set(B_STOP_CLEAR, 0);
        if (e.idx == B_BROWSER) led_set(B_BROWSER, 0);
        break;
      case EVT_ENC_DELTA:
        applyEncoder(e.idx, e.value);
        oled_setTouchedParam(e.idx);
        break;
      case EVT_TOUCH_ENC_ON:
        oled_setTouchedParam(e.idx);
        break;
      case EVT_TOUCH_ENC_OFF:
        // lascia il timer auto-spegnere la view
        break;
      case EVT_TOUCH_PITCH: {
        // pitch bend ±2 semi (value 0..127)
        int16_t bend = ((int16_t)e.value - 64) * 256;  // -16384..+16128
        synth.pitchBend = bend / 2;  // -8192..+8064
        break;
      }
      case EVT_TOUCH_PITCH_OFF:
        synth.pitchBend = 0;
        break;
      case EVT_TOUCH_MOD:
        synth.modWheel = e.value;
        break;
      case EVT_TOUCH_MOD_OFF:
        // mod resta dove era (come hardware veri)
        break;
    }
  }
  
  // Render OLED
  oled_render(now);
  
  // Flush LED
  led_flush_if_dirty();
}

static void applyEncoder(uint8_t enc, int16_t delta) {
  // Mapping cambia per pagina
  switch (currentPage) {
    case PAGE_OSC:
      switch (enc) {
        case 0: synth.wave1 = (synth.wave1 + (delta > 0 ? 1 : 3)) & 3; break;
        case 1: synth.wave2 = (synth.wave2 + (delta > 0 ? 1 : 3)) & 3; break;
        case 2: synth.mix = constrain(synth.mix + delta * 500, 0, 65535); break;
        case 3: synth.detune = constrain(synth.detune + delta, -100, 100); break;
        case 4: synth.cutoff = constrain(synth.cutoff + delta * 500, 1000, 65535); break;
        case 5: synth.reso = constrain(synth.reso + delta * 500, 0, 65535); break;
        case 6: synth.glide = constrain(synth.glide + delta * 500, 0, 65535); break;
        case 7: synth.volume = constrain(synth.volume + delta * 500, 0, 65535); break;
      }
      break;
    case PAGE_ENV:
      switch (enc) {
        case 0: synth.aAtt = constrain(synth.aAtt + delta * 5, 1, 5000); break;
        case 1: synth.aDec = constrain(synth.aDec + delta * 5, 1, 5000); break;
        case 2: synth.aSus = constrain(synth.aSus + delta * 500, 0, 65535); break;
        case 3: synth.aRel = constrain(synth.aRel + delta * 5, 1, 5000); break;
        case 4: synth.fAtt = constrain(synth.fAtt + delta * 5, 1, 5000); break;
        case 5: synth.fDec = constrain(synth.fDec + delta * 5, 1, 5000); break;
        case 6: synth.fSus = constrain(synth.fSus + delta * 500, 0, 65535); break;
        case 7: synth.fRel = constrain(synth.fRel + delta * 5, 1, 5000); break;
      }
      break;
    case PAGE_MOD:
      switch (enc) {
        case 0: synth.lfoRate = constrain(synth.lfoRate + delta * 10, 1, 65535); break;
        case 1: synth.lfoDepth = constrain(synth.lfoDepth + delta * 500, 0, 65535); break;
        case 2: synth.lfoWave = (synth.lfoWave + (delta > 0 ? 1 : 3)) & 3; break;
        case 3: synth.lfoTarget = (synth.lfoTarget + (delta > 0 ? 1 : 2)) % 3; break;
        case 4: synth.fEnvAmount = constrain(synth.fEnvAmount + delta * 500, 0, 32767); break;
      }
      break;
  }
}
