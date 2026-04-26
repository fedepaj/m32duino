#include <Arduino.h>

// ==================== PIN CONFIG ====================

#define SEL_A0   PB0
#define SEL_A1   PB1
#define SEL_A2   PB2
#define SEL_CMD  PB14
#define MASK_CTRL (0x4007)

// PC8..PC15 come colonne (bit 8-15 di GPIOC->IDR)
#define COL_SHIFT 8
#define N_COLS 8
#define N_SLOTS 16

// ==================== SCAN ENGINE (ISR) ====================
// Tutto lo stato sotto è toccato dall'ISR del timer.
// Il polling avviene a ogni tick del timer: prima cambiamo i SEL pin per
// selezionare il prossimo slot, poi leggiamo lo slot corrente.

volatile uint8_t currentStep = 0;

// Stato logico stabile per ogni (col, slot): true = premuto
volatile bool pressedState[N_COLS][N_SLOTS];

// Ring buffer di eventi raw (col, slot, press, t_us)
struct RawEvent { uint8_t col; uint8_t slot; bool press; uint32_t t_us; };
#define RAW_EVT_BUF_SIZE 256
volatile RawEvent rawBuf[RAW_EVT_BUF_SIZE];
volatile uint16_t rawHead = 0;
volatile uint16_t rawTail = 0;

// Timer ISR. Strategia: 
// 1. leggiamo lo stato delle colonne relativo allo slot CORRENTE (appena finito di settle)
// 2. avanziamo al prossimo slot programmando i SEL pin
// 3. il prossimo tick leggerà quello slot
static inline void pushRawEvent(uint8_t col, uint8_t slot, bool press) {
  uint16_t head = rawHead;
  uint16_t nextHead = (head + 1) & (RAW_EVT_BUF_SIZE - 1);
  if (nextHead != rawTail) {
    rawBuf[head].col = col;
    rawBuf[head].slot = slot;
    rawBuf[head].press = press;
    rawBuf[head].t_us = micros();
    rawHead = nextHead;
  }
}

void scanTimer_ISR() {
  // Lo slot corrente è quello selezionato al tick precedente
  uint8_t slot = currentStep;
  
  // Leggi tutte le 8 colonne in un colpo solo
  uint16_t idr = (GPIOC->IDR >> COL_SHIFT) & 0xFF;
  
  // Per ogni colonna, determina se è attiva (bit basso = premuto con pull-up)
  for (uint8_t c = 0; c < N_COLS; c++) {
    bool press = !((idr >> c) & 1);
    if (press != pressedState[c][slot]) {
      pressedState[c][slot] = press;
      pushRawEvent(c, slot, press);
    }
  }
  
  // Avanza e programma il prossimo slot
  uint8_t next = (slot + 1) & 0x0F;
  currentStep = next;
  uint32_t val = (next & 0x07) | ((next & 0x08) << 11);
  GPIOB->BSRR = (MASK_CTRL << 16) | val;
}

bool scanner_pop(RawEvent* out) {
  if (rawTail == rawHead) return false;
  noInterrupts();
  *out = const_cast<RawEvent&>(rawBuf[rawTail]);
  rawTail = (rawTail + 1) & (RAW_EVT_BUF_SIZE - 1);
  interrupts();
  return true;
}

HardwareTimer *scanTim;

void scanner_begin() {
  pinMode(SEL_A0, OUTPUT); pinMode(SEL_A1, OUTPUT);
  pinMode(SEL_A2, OUTPUT); pinMode(SEL_CMD, OUTPUT);
  
  const uint32_t colPins[] = {PC8, PC9, PC10, PC11, PC12, PC13, PC14, PC15};
  for (int i = 0; i < N_COLS; i++) pinMode(colPins[i], INPUT_PULLUP);
  
  // init memoria
  for (int c = 0; c < N_COLS; c++)
    for (int s = 0; s < N_SLOTS; s++)
      pressedState[c][s] = false;
  
  // Timer TIM2: 10us/tick. Full cycle = 160us = 6.25kHz, abbastanza per tutto
  scanTim = new HardwareTimer(TIM2);
  scanTim->setOverflow(10, MICROSEC_FORMAT);
  scanTim->attachInterrupt(scanTimer_ISR);
  scanTim->resume();
}

// ==================== MAPPER CONSUMER ====================
// Gira nel main loop, legge eventi raw dal ring e guida mapping interattivo.

struct MapItem { const char* name; bool isKey; };

MapItem toMap[] = {
  {"btn_shift", false}, {"btn_scale_edit", false}, {"btn_arp_edit", false},
  {"btn_undo_redo", false}, {"btn_quantize_auto", false}, {"btn_ideas", false},
  {"btn_loop", false}, {"btn_metro", false}, {"btn_tempo", false},
  {"btn_preset_up", false}, {"btn_preset_down", false}, {"btn_play_restart", false},
  {"btn_rec_countin", false}, {"btn_stop_clear", false}, {"btn_left_m", false},
  {"btn_right_s", false}, {"btn_track_instance", false}, {"btn_plugin_midi", false},
  {"btn_browser", false}, {"btn_oct_down", false}, {"btn_oct_up", false},
  {"key_F2", true}, {"key_Fs2", true}, {"key_G2", true}, {"key_Gs2", true},
  {"key_A2", true}, {"key_As2", true}, {"key_B2", true},
  {"key_C3", true}, {"key_Cs3", true}, {"key_D3", true}, {"key_Ds3", true},
  {"key_E3", true}, {"key_F3", true}, {"key_Fs3", true}, {"key_G3", true},
  {"key_Gs3", true}, {"key_A3", true}, {"key_As3", true}, {"key_B3", true},
  {"key_C4", true}, {"key_Cs4", true}, {"key_D4", true}, {"key_Ds4", true},
  {"key_E4", true}, {"key_F4", true}, {"key_Fs4", true}, {"key_G4", true},
  {"key_Gs4", true}, {"key_A4", true}, {"key_As4", true}, {"key_B4", true},
  {"key_C5", true}
};
const int N_TO_MAP = sizeof(toMap) / sizeof(toMap[0]);

// Per ogni item salviamo 1 o 2 coord (top e bot per i tasti)
struct Coord { uint8_t col; uint8_t slot; };
Coord itemCoords[64][2];
uint8_t itemCount[64] = {0};

// Lookup inversa: coords[col][slot] → indice item (-1 se libero)
int16_t coordsLUT[N_COLS][N_SLOTS];

int currentIdx = 0;
enum MapState { MAP_IDLE, MAP_WAIT_FIRST, MAP_WAIT_SECOND };
MapState mapState = MAP_IDLE;

void printFinalMap() {
  Serial.println();
  Serial.println("// === MAPPA FINALE ===");
  Serial.println("struct ButtonMapping { uint8_t col; uint8_t slot; const char* name; bool isKeyBot; };");
  Serial.println("const ButtonMapping buttonMap[] = {");
  int total = 0;
  for (int i = 0; i < N_TO_MAP; i++) {
    if (itemCount[i] >= 1) {
      Serial.print("  { ");
      Serial.print(itemCoords[i][0].col); Serial.print(", ");
      Serial.print(itemCoords[i][0].slot); Serial.print(", \"");
      Serial.print(toMap[i].name);
      if (toMap[i].isKey) Serial.print("_top");
      Serial.println("\", false },");
      total++;
    }
    if (itemCount[i] >= 2) {
      Serial.print("  { ");
      Serial.print(itemCoords[i][1].col); Serial.print(", ");
      Serial.print(itemCoords[i][1].slot); Serial.print(", \"");
      Serial.print(toMap[i].name); Serial.println("_bot\", true },");
      total++;
    }
  }
  Serial.println("};");
  Serial.print("const int N_BUTTONS = "); Serial.print(total); Serial.println(";");
}

void askNext() {
  if (currentIdx >= N_TO_MAP) {
    Serial.println("\n>>> TUTTI MAPPATI <<<");
    printFinalMap();
    mapState = MAP_IDLE;
    return;
  }
  Serial.print("[");
  Serial.print(currentIdx + 1); Serial.print("/"); Serial.print(N_TO_MAP);
  Serial.print("] ");
  Serial.print(toMap[currentIdx].isKey ? "PREMI LENTO FINO IN FONDO: " : "PREMI: ");
  Serial.println(toMap[currentIdx].name);
  mapState = MAP_WAIT_FIRST;
}

void mapper_handle(const RawEvent& e) {
  if (mapState == MAP_IDLE) return;
  if (!e.press) return;  // ignoro i release in mapping
  
  if (mapState == MAP_WAIT_FIRST) {
    if (coordsLUT[e.col][e.slot] != -1) return;
    itemCoords[currentIdx][0].col = e.col;
    itemCoords[currentIdx][0].slot = e.slot;
    itemCount[currentIdx] = 1;
    coordsLUT[e.col][e.slot] = currentIdx;
    
    if (toMap[currentIdx].isKey) {
      Serial.print("  TOP col "); Serial.print(e.col);
      Serial.print(" slot "); Serial.println(e.slot);
      mapState = MAP_WAIT_SECOND;
    } else {
      Serial.print("  OK col "); Serial.print(e.col);
      Serial.print(" slot "); Serial.println(e.slot);
      currentIdx++;
      askNext();
    }
  }
  else if (mapState == MAP_WAIT_SECOND) {
    if (coordsLUT[e.col][e.slot] != -1) return;  // già mappato
    itemCoords[currentIdx][1].col = e.col;
    itemCoords[currentIdx][1].slot = e.slot;
    itemCount[currentIdx] = 2;
    coordsLUT[e.col][e.slot] = currentIdx;
    Serial.print("  BOT col "); Serial.print(e.col);
    Serial.print(" slot "); Serial.println(e.slot);
    currentIdx++;
    askNext();
  }
}

// ==================== SETUP / LOOP ====================
void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);
  delay(500);
  
  for (int c = 0; c < N_COLS; c++)
    for (int s = 0; s < N_SLOTS; s++)
      coordsLUT[c][s] = -1;
  
  scanner_begin();
  
  Serial.println(">>> M32 SCANNER + MAPPER <<<");
  Serial.println("  s = start mapping  |  k = skip  |  b = back  |  p = print");
}

void loop() {
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line == "s") { currentIdx = 0; askNext(); }
    else if (line == "k") { Serial.println("SKIP"); currentIdx++; askNext(); }
    else if (line == "b" && currentIdx > 0) {
      currentIdx--;
      for (int j = 0; j < itemCount[currentIdx]; j++)
        coordsLUT[itemCoords[currentIdx][j].col][itemCoords[currentIdx][j].slot] = -1;
      itemCount[currentIdx] = 0;
      askNext();
    }
    else if (line == "p") printFinalMap();
  }
  
  // Consumer: legge eventi raw e li passa al mapper
  RawEvent e;
  while (scanner_pop(&e)) {
    mapper_handle(e);
  }
}
