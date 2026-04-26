#include <Arduino.h>

// ==================== PIN CONFIG ====================

#define SEL_A0   PB0
#define SEL_A1   PB1
#define SEL_A2   PB2
#define SEL_CMD  PB14
#define MASK_CTRL (0x4007)

#define COL_SHIFT 8
#define N_COLS 8
#define N_SLOTS 16

// ==================== BUTTON MAPPING (dalla mappa generata) ====================
// Indice 0-20: pulsanti con LED (isKey=false)
// Indice 21-52: tasti (isKey=true)
#define N_BUTTONS 21
#define N_KEYS 32

// LUT: (col, slot) -> (buttonIdx o keyIdx, isBot)
// Codifica: se -1 libero/rumore
// Altrimenti bit 7 = isBot, bit 6 = isKey, bit 5-0 = idx
// Semplifichiamo con due LUT separate
uint8_t  btnLUT[N_COLS][N_SLOTS];   // -1 o indice pulsante (0-20)
uint8_t  keyLUT[N_COLS][N_SLOTS];   // -1 o indice tasto (0-31) con bit7=isBot

const char* btnNames[N_BUTTONS] = {
  "shift", "scale_edit", "arp_edit", "undo_redo", "quantize_auto",
  "ideas", "loop", "metro", "tempo", "preset_up", "preset_down",
  "play_restart", "rec_countin", "stop_clear", "left_m", "right_s",
  "track_instance", "plugin_midi", "browser", "oct_down", "oct_up"
};

// MIDI note start: F2 = MIDI 41 (F2 secondo convention NI/standard)
// Se l'octave shift sul device diverso, aggiusta qui
#define MIDI_NOTE_START 41
const char* noteNames[] = {
  "C","Cs","D","Ds","E","F","Fs","G","Gs","A","As","B"
};

void buildLUT() {
  for (int c = 0; c < N_COLS; c++)
    for (int s = 0; s < N_SLOTS; s++) {
      btnLUT[c][s] = 0xFF;
      keyLUT[c][s] = 0xFF;
    }
  
  // Pulsanti (col, slot, idx)
  struct B { uint8_t c, s, i; };
  B btns[] = {
    {0,0,0},{1,0,1},{2,0,2},{3,0,3},{4,0,4},{5,0,5},
    {0,2,6},{1,2,7},{2,2,8},{3,1,9},{4,1,10},{3,2,11},
    {4,2,12},{5,2,13},{6,1,14},{7,1,15},{2,1,16},{1,1,17},
    {0,1,18},{7,2,19},{6,2,20}
  };
  for (auto& b : btns) btnLUT[b.c][b.s] = b.i;
  
  // Tasti: top = idx | 0x00, bot = idx | 0x80
  struct K { uint8_t c, s, i; bool isBot; };
  K keys[] = {
    {7,14,0,false},{7,15,0,true},   // F2
    {6,14,1,false},{6,15,1,true},   // Fs2
    {5,14,2,false},{5,15,2,true},   // G2
    {4,14,3,false},{4,15,3,true},   // Gs2
    {3,14,4,false},{3,15,4,true},   // A2
    {2,14,5,false},{2,15,5,true},   // As2
    {1,14,6,false},{1,15,6,true},   // B2
    {0,14,7,false},{0,15,7,true},   // C3
    {7,12,8,false},{7,13,8,true},   // Cs3
    {6,12,9,false},{6,13,9,true},   // D3
    {5,12,10,false},{5,13,10,true}, // Ds3
    {4,12,11,false},{4,13,11,true}, // E3
    {3,12,12,false},{3,13,12,true}, // F3
    {2,12,13,false},{2,13,13,true}, // Fs3
    {1,12,14,false},{1,13,14,true}, // G3
    {0,12,15,false},{0,13,15,true}, // Gs3
    {7,10,16,false},{7,11,16,true}, // A3
    {6,10,17,false},{6,11,17,true}, // As3
    {5,10,18,false},{5,11,18,true}, // B3
    {4,10,19,false},{4,11,19,true}, // C4
    {3,10,20,false},{3,11,20,true}, // Cs4
    {2,10,21,false},{2,11,21,true}, // D4
    {1,10,22,false},{1,11,22,true}, // Ds4
    {0,10,23,false},{0,11,23,true}, // E4
    {7,8,24,false},{7,9,24,true},   // F4
    {6,8,25,false},{6,9,25,true},   // Fs4
    {5,8,26,false},{5,9,26,true},   // G4
    {4,8,27,false},{4,9,27,true},   // Gs4
    {3,8,28,false},{3,9,28,true},   // A4
    {2,8,29,false},{2,9,29,true},   // As4
    {1,8,30,false},{1,9,30,true},   // B4
    {0,8,31,false},{0,9,31,true}    // C5
  };
  for (auto& k : keys) keyLUT[k.c][k.s] = k.i | (k.isBot ? 0x80 : 0);
}

// ==================== SCAN ENGINE (ISR) ====================
volatile uint8_t currentStep = 0;
volatile bool pressedState[N_COLS][N_SLOTS];

// Tempi TOP per il calcolo velocity
volatile uint32_t keyTopTime[N_KEYS];
volatile bool keyTopPressed[N_KEYS];  // true se TOP è attualmente premuto

// Ring buffer di eventi SEMANTICI (note_on, note_off, button_down, button_up)
enum EvtType { EVT_NOTE_ON, EVT_NOTE_OFF, EVT_BTN_DOWN, EVT_BTN_UP };
struct SemEvent { uint8_t type; uint8_t idx; uint8_t velocity; };
#define SEM_BUF_SIZE 128
volatile SemEvent semBuf[SEM_BUF_SIZE];
volatile uint16_t semHead = 0;
volatile uint16_t semTail = 0;

bool scanner_pop(SemEvent* out);

static inline void pushSemEvent(uint8_t type, uint8_t idx, uint8_t velocity) {
  uint16_t head = semHead;
  uint16_t nextHead = (head + 1) & (SEM_BUF_SIZE - 1);
  if (nextHead != semTail) {
    semBuf[head].type = type;
    semBuf[head].idx = idx;
    semBuf[head].velocity = velocity;
    semHead = nextHead;
  }
}

// Mappa delta_us → velocity 1..127
// Tempi rapidi (=suonato forte) → velocity alta
// Curva semplice: 500us=127, 50000us=1
static inline uint8_t velocityFromDelta(uint32_t delta_us) {
  if (delta_us < 500) return 127;
  if (delta_us > 50000) return 1;
  // Interpolazione logaritmica approssimata
  // linear inverse su [500, 50000] us -> [127, 1]
  uint32_t v = 127 - ((delta_us - 500) * 126 / 49500);
  if (v < 1) v = 1;
  if (v > 127) v = 127;
  return v;
}

void scanTimer_ISR() {
  uint8_t slot = currentStep;
  uint16_t idr = (GPIOC->IDR >> COL_SHIFT) & 0xFF;
  uint32_t now_us = micros();
  
  for (uint8_t c = 0; c < N_COLS; c++) {
    bool press = !((idr >> c) & 1);
    if (press == pressedState[c][slot]) continue;
    pressedState[c][slot] = press;

// Interpreta l'evento via LUT
    uint8_t btn = btnLUT[c][slot];
    if (btn != 0xFF) {
      pushSemEvent(press ? EVT_BTN_DOWN : EVT_BTN_UP, btn, 0);
      continue;
    }
    uint8_t k = keyLUT[c][slot];
    if (k != 0xFF) {
      bool isBot = (k & 0x80) != 0;
      uint8_t keyIdx = k & 0x7F;
      
      if (!isBot) {
        if (press) {
          keyTopTime[keyIdx] = now_us;
          keyTopPressed[keyIdx] = true;
        } else {
          keyTopPressed[keyIdx] = false;
          pushSemEvent(EVT_NOTE_OFF, keyIdx, 0);
        }
      } else {
        if (press && keyTopPressed[keyIdx]) {
          uint32_t delta = now_us - keyTopTime[keyIdx];
          uint8_t vel = velocityFromDelta(delta);
          pushSemEvent(EVT_NOTE_ON, keyIdx, vel);
        }
      }
    }
  }
  
  // Avanza slot
  uint8_t next = (slot + 1) & 0x0F;
  currentStep = next;
  uint32_t val = (next & 0x07) | ((next & 0x08) << 11);
  GPIOB->BSRR = (MASK_CTRL << 16) | val;
}

bool scanner_pop(SemEvent* out) {
  if (semTail == semHead) return false;
  noInterrupts();
  *out = const_cast<SemEvent&>(semBuf[semTail]);
  semTail = (semTail + 1) & (SEM_BUF_SIZE - 1);
  interrupts();
  return true;
}

HardwareTimer *scanTim;

void scanner_begin() {
  pinMode(SEL_A0, OUTPUT); pinMode(SEL_A1, OUTPUT);
  pinMode(SEL_A2, OUTPUT); pinMode(SEL_CMD, OUTPUT);
  
  const uint32_t colPins[] = {PC8, PC9, PC10, PC11, PC12, PC13, PC14, PC15};
  for (int i = 0; i < N_COLS; i++) pinMode(colPins[i], INPUT_PULLUP);
  
  for (int c = 0; c < N_COLS; c++)
    for (int s = 0; s < N_SLOTS; s++)
      pressedState[c][s] = false;
  
  for (int k = 0; k < N_KEYS; k++) {
    keyTopTime[k] = 0;
    keyTopPressed[k] = false;
  }
  
  scanTim = new HardwareTimer(TIM2);
  scanTim->setOverflow(10, MICROSEC_FORMAT);
  scanTim->attachInterrupt(scanTimer_ISR);
  scanTim->resume();
}

// ==================== SETUP / LOOP ====================
void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);
  delay(500);
  
  buildLUT();
  scanner_begin();
  
  Serial.println(">>> M32 RUNTIME <<<");
  Serial.println("Premi pulsanti/tasti, vedrai eventi qua.");
}

void loop() {
  SemEvent e;
  while (scanner_pop(&e)) {
    switch (e.type) {
      case EVT_NOTE_ON: {
        uint8_t note = MIDI_NOTE_START + e.idx;
        Serial.print("NOTE_ON  ");
        Serial.print(noteNames[note % 12]);
        Serial.print(note / 12 - 1);
        Serial.print(" (midi=");
        Serial.print(note);
        Serial.print(")  vel=");
        Serial.println(e.velocity);
        break;
      }
      case EVT_NOTE_OFF: {
        uint8_t note = MIDI_NOTE_START + e.idx;
        Serial.print("NOTE_OFF ");
        Serial.print(noteNames[note % 12]);
        Serial.print(note / 12 - 1);
        Serial.print(" (midi=");
        Serial.print(note);
        Serial.println(")");
        break;
      }
      case EVT_BTN_DOWN:
        Serial.print("BTN_DOWN  ");
        Serial.println(btnNames[e.idx]);
        break;
      case EVT_BTN_UP:
        Serial.print("BTN_UP    ");
        Serial.println(btnNames[e.idx]);
        break;
    }
  }
}
