#include "oled.h"
#include "synth_state.h"
#include "audio.h"
#include "mappings.h"
#include <U8g2lib.h>
#include <SPI.h>
#include <math.h>

#define OLED_MOSI PB5
#define OLED_CLK  PB3
#define OLED_CS   PB15
#define OLED_DC   PB4
#define OLED_RES  PD2

static U8G2_SSD1306_128X32_UNIVISION_F_4W_HW_SPI u8g2(U8G2_R2, OLED_CS, OLED_DC, OLED_RES);

static uint8_t currentPage = PAGE_OSC;
static int8_t touchedEnc = -1;
static uint32_t touchedExpire = 0;
static uint32_t pageBannerExpire = 0;
static uint32_t lastFrame = 0;
static uint32_t frameNum = 0;

const char* pageNames[N_PAGES] = {"OSC", "ENV", "MOD", "FX"};

// Mappa: pagina × encoder → nome parametro (o nullptr)
static const char* paramName(uint8_t page, uint8_t enc) {
  static const char* names[N_PAGES][8] = {
    {"WAVE 1", "WAVE 2", "MIX", "DETUNE", "CUTOFF", "RESO", "GLIDE", "VOLUME"},
    {"A.ATT", "A.DEC", "A.SUS", "A.REL", "F.ATT", "F.DEC", "F.SUS", "F.REL"},
    {"LFO RATE", "LFO DEPTH", "LFO WAVE", "LFO TARG", "FILT ENV", nullptr, nullptr, nullptr},
    {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}
  };
  return names[page][enc];
}

static const char* lfoTargetName(uint8_t t) {
  switch (t) { case 0: return "PITCH"; case 1: return "CUT"; default: return "AMP"; }
}

static const char* waveStr(uint8_t w) {
  switch (w) { case 0: return "SIN"; case 1: return "TRI"; case 2: return "SAW"; default: return "SQR"; }
}

void oled_init() {
  SPI.setMOSI(OLED_MOSI);
  SPI.setSCLK(OLED_CLK);
  u8g2.begin();
  u8g2.setFont(u8g2_font_6x10_tr);
}

void oled_setPage(uint8_t p) { if (p < N_PAGES) currentPage = p; }

void oled_setTouchedParam(int8_t enc) {
  touchedEnc = enc;
  if (enc >= 0) touchedExpire = millis() + 1500;
}

void oled_pulsePageBanner() { pageBannerExpire = millis() + 800; }

// =========== VISTE ===========

static void drawWaveformGrande(uint8_t wave, int x0, int y0, int w, int h) {
  // Disegna una wave del tipo 'wave' nel rettangolo
  const int16_t* tbl = getWaveTable(wave);
  int prev_y = y0 + h/2;
  for (int x = 0; x < w; x++) {
    int sampleIdx = (x * 256) / w;
    int v = tbl[sampleIdx];
    int y = y0 + h/2 - (v * (h/2 - 1)) / 32767;
    if (x > 0) u8g2.drawLine(x0 + x - 1, prev_y, x0 + x, y);
    prev_y = y;
  }
}

static void drawADSRCurve(int x0, int y0, int w, int h, uint16_t a, uint16_t d, uint16_t s, uint16_t r, int highlight) {
  // ADSR semplice: 4 segmenti su w pixel
  // Tempi attack/decay/release scalati per stare in 1/3 di w ognuno
  // Sustain è level
  int wA = (w * a) / (a + d + r + 1) / 2 + 5;
  int wD = (w * d) / (a + d + r + 1) / 2 + 5;
  int wR = (w * r) / (a + d + r + 1) / 2 + 5;
  int wS = w - wA - wD - wR;
  if (wS < 5) wS = 5;
  int sLevel = y0 + h - (s * h) / 65535;
  
  int x = x0;
  // Attack: linea da (x, y0+h) a (x+wA, y0)
  u8g2.drawLine(x, y0 + h - 1, x + wA, y0);
  if (highlight == 0) u8g2.drawBox(x, y0 + h - 2, wA, 2);
  x += wA;
  // Decay: linea da (x, y0) a (x+wD, sLevel)
  u8g2.drawLine(x, y0, x + wD, sLevel);
  if (highlight == 1) u8g2.drawBox(x, y0 + h - 2, wD, 2);
  x += wD;
  // Sustain: linea orizzontale
  u8g2.drawLine(x, sLevel, x + wS, sLevel);
  if (highlight == 2) u8g2.drawBox(x, y0 + h - 2, wS, 2);
  x += wS;
  // Release: linea da (x, sLevel) a (x+wR, y0+h)
  u8g2.drawLine(x, sLevel, x + wR, y0 + h - 1);
  if (highlight == 3) u8g2.drawBox(x, y0 + h - 2, wR, 2);
}

static void drawFilterCurve(int x0, int y0, int w, int h, uint16_t cutoff, uint16_t reso) {
  // LP curve: piatto fino al cutoff, poi roll-off
  int cutX = x0 + (cutoff * (w - 4)) / 65535;
  int prev_y = y0 + 4;
  for (int x = 0; x < w; x++) {
    int xa = x0 + x;
    int y;
    if (xa < cutX) y = y0 + 4;
    else {
      int dist = xa - cutX;
      y = y0 + 4 + (dist * dist) / 8;
    }
    // Resonance bump
    if (xa > cutX - 3 && xa < cutX + 3) y -= (reso >> 12);
    if (y < y0) y = y0;
    if (y >= y0 + h) y = y0 + h - 1;
    if (x > 0) u8g2.drawLine(xa - 1, prev_y, xa, y);
    prev_y = y;
  }
}

static void drawLFOCurve(int x0, int y0, int w, int h, uint8_t wave, uint16_t depth, uint32_t phaseOffset) {
  const int16_t* tbl = getWaveTable(wave);
  int prev_y = y0 + h/2;
  for (int x = 0; x < w; x++) {
    uint8_t idx = (uint8_t)((x * 8 + (phaseOffset >> 22)) & 0xFF);
    int v = tbl[idx];
    int y = y0 + h/2 - (v * (h/2 - 1) * depth) / (32767 * 65535);
    if (y < y0) y = y0;
    if (y >= y0 + h) y = y0 + h - 1;
    if (x > 0) u8g2.drawLine(x0 + x - 1, prev_y, x0 + x, y);
    prev_y = y;
  }
}

static void drawDefaultView() {
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.setCursor(0, 8);
  u8g2.print("M32 ");
  u8g2.print(pageNames[currentPage]);
  
  u8g2.setCursor(80, 8);
  if (audio_voiceActive()) {
    uint8_t n = audio_currentNote();
    u8g2.print(noteNames[n % 12]);
    u8g2.print(n / 12 - 1);
  } else {
    u8g2.print("---");
  }
  
  // Waveform animata in basso (osc1 mixed con envelope)
  uint16_t env = audio_currentAmpEnv();
  uint8_t basePhase = audio_currentPhase();
  const int16_t* tbl1 = getWaveTable(synth.wave1);
  const int16_t* tbl2 = getWaveTable(synth.wave2);
  
  int amp = (env >> 12);  // 0..15
  int prev_y = 22;
  for (int x = 0; x < 128; x++) {
    uint8_t idx = (uint8_t)(basePhase + (x * 4));
    int s1 = tbl1[idx];
    int s2 = tbl2[idx];
    int mixed = (s1 * (65535 - synth.mix) + s2 * synth.mix) >> 16;
    int y = 22 - (mixed * amp) / 32767;
    if (y < 12) y = 12;
    if (y > 31) y = 31;
    if (x > 0) u8g2.drawLine(x - 1, prev_y, x, y);
    prev_y = y;
  }
}

static void drawParamView(uint8_t enc) {
  const char* name = paramName(currentPage, enc);
  if (!name) { drawDefaultView(); return; }
  
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.setCursor(0, 9);
  u8g2.print(pageNames[currentPage]);
  u8g2.print(" / ");
  u8g2.print(name);
  
  // Vista specifica per parametro
  if (currentPage == PAGE_OSC) {
    if (enc == 0 || enc == 1) {
      // Wave selection: mostra grande la wave
      uint8_t w = (enc == 0) ? synth.wave1 : synth.wave2;
      drawWaveformGrande(w, 0, 14, 80, 18);
      u8g2.setFont(u8g2_font_logisoso16_tr);
      u8g2.setCursor(90, 30);
      u8g2.print(waveStr(w));
    } else if (enc == 4 || enc == 5) {
      // Cutoff / Reso: mostra filter curve
      drawFilterCurve(0, 14, 128, 18, synth.cutoff, synth.reso);
    } else {
      // Generic numeric
      u8g2.setFont(u8g2_font_logisoso16_tr);
      char buf[16];
      int v = 0;
      switch (enc) {
        case 2: v = (synth.mix * 100) >> 16; break;
        case 3: v = synth.detune; break;
        case 6: v = (synth.glide * 100) >> 16; break;
        case 7: v = (synth.volume * 100) >> 16; break;
      }
      snprintf(buf, sizeof(buf), "%d", v);
      int wpx = u8g2.getStrWidth(buf);
      u8g2.setCursor((128 - wpx) / 2, 30);
      u8g2.print(buf);
    }
  } else if (currentPage == PAGE_ENV) {
    // Disegna ADSR completo con highlight su segmento corrente
    bool isFilt = (enc >= 4);
    int seg = enc & 3;
    if (isFilt) {
      drawADSRCurve(0, 14, 128, 17, synth.fAtt, synth.fDec, synth.fSus, synth.fRel, seg);
    } else {
      drawADSRCurve(0, 14, 128, 17, synth.aAtt, synth.aDec, synth.aSus, synth.aRel, seg);
    }
  } else if (currentPage == PAGE_MOD) {
    if (enc == 0 || enc == 1 || enc == 2) {
      // LFO rate/depth/wave: anima la curva
      drawLFOCurve(0, 14, 128, 17, synth.lfoWave, synth.lfoDepth, frameNum * 200000);
    } else if (enc == 3) {
      // LFO target
      u8g2.setFont(u8g2_font_logisoso16_tr);
      const char* t = lfoTargetName(synth.lfoTarget);
      int wpx = u8g2.getStrWidth(t);
      u8g2.setCursor((128 - wpx) / 2, 30);
      u8g2.print(t);
    } else if (enc == 4) {
      // Filter env amount: mostra la curva con un valore
      u8g2.setFont(u8g2_font_logisoso16_tr);
      char buf[16];
      snprintf(buf, sizeof(buf), "%d%%", (synth.fEnvAmount * 100) / 32767);
      int wpx = u8g2.getStrWidth(buf);
      u8g2.setCursor((128 - wpx) / 2, 30);
      u8g2.print(buf);
    }
  }
}

static void drawPageBanner() {
  u8g2.drawBox(0, 0, 128, 32);
  u8g2.setDrawColor(0);
  u8g2.setFont(u8g2_font_logisoso16_tr);
  char buf[20];
  snprintf(buf, sizeof(buf), "PAGE %d %s", currentPage + 1, pageNames[currentPage]);
  int w = u8g2.getStrWidth(buf);
  u8g2.setCursor((128 - w) / 2, 22);
  u8g2.print(buf);
  u8g2.setDrawColor(1);
}

void oled_render(uint32_t now) {
  if (now - lastFrame < 66) return;  // 30fps
  lastFrame = now;
  frameNum++;
  
  u8g2.clearBuffer();
  
  if (now < pageBannerExpire) {
    drawPageBanner();
  } else if (touchedEnc >= 0 && now < touchedExpire) {
    drawParamView(touchedEnc);
  } else {
    drawDefaultView();
  }
  
  u8g2.sendBuffer();
}
