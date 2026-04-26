#include <Arduino.h>
#include <U8g2lib.h>
#include <SPI.h>
#include <math.h>

// ==================== PINOUT ====================
#define OLED_MOSI   PB5
#define OLED_CLK    PB3
#define OLED_CS     PB15
#define OLED_DC     PB4
#define OLED_RES    PD2
#define LED_RESET   PA15

#define LED_I2C_ADDR 0x3C

#define SEL_A0   PB0
#define SEL_A1   PB1
#define SEL_A2   PB2
#define SEL_CMD  PB14
#define MASK_CTRL (0x4007)
#define COL_SHIFT 8
#define N_COLS 8
#define N_SLOTS 16
#define N_BUTTONS 21
#define N_KEYS 32
#define MIDI_NOTE_START 41  // F2

// ==================== I2C BITBANG (LED driver) ====================
#define SCL_PIN 6
#define SDA_PIN 7
#define SCL_BIT (1 << SCL_PIN)
#define SDA_BIT (1 << SDA_PIN)

static inline void scl_set_output_od() {
  GPIOB->CRL = (GPIOB->CRL & ~(0xF << (SCL_PIN*4))) | (0x6 << (SCL_PIN*4));
}
static inline void sda_set_output_od() {
  GPIOB->CRL = (GPIOB->CRL & ~(0xF << (SDA_PIN*4))) | (0x6 << (SDA_PIN*4));
}
static inline void scl_high() { GPIOB->BSRR = SCL_BIT; }
static inline void scl_low()  { GPIOB->BSRR = SCL_BIT << 16; }
static inline void sda_high() { GPIOB->BSRR = SDA_BIT; }
static inline void sda_low()  { GPIOB->BSRR = SDA_BIT << 16; }
static inline bool sda_read() { return (GPIOB->IDR & SDA_BIT) != 0; }
static inline void i2c_delay() { for (volatile int i = 0; i < 10; i++) __NOP(); }

void i2c_init() {
  scl_high(); sda_high();
  scl_set_output_od(); sda_set_output_od();
}
void i2c_start() {
  sda_high(); scl_high(); i2c_delay();
  sda_low();  i2c_delay();
  scl_low();  i2c_delay();
}
void i2c_stop() {
  sda_low();  i2c_delay();
  scl_high(); i2c_delay();
  sda_high(); i2c_delay();
}
bool i2c_writeByte(uint8_t b) {
  for (int i = 7; i >= 0; i--) {
    if (b & (1 << i)) sda_high(); else sda_low();
    i2c_delay();
    scl_high(); i2c_delay();
    scl_low();  i2c_delay();
  }
  sda_high(); i2c_delay();
  scl_high(); i2c_delay();
  bool ack = !sda_read();
  scl_low();  i2c_delay();
  return ack;
}
void i2c_writeBuf(uint8_t addr, const uint8_t* data, size_t len) {
  i2c_start();
  i2c_writeByte(addr << 1);
  for (size_t i = 0; i < len; i++) i2c_writeByte(data[i]);
  i2c_stop();
}

// ==================== LED ====================
// LED index = stesso indice button (layout coincidente da design NI)
enum LedIdx {
  LED_SHIFT=0, LED_SCALE_EDIT, LED_ARP_EDIT, LED_UNDO_REDO,
  LED_QUANTIZE_AUTO, LED_IDEAS, LED_LOOP, LED_METRO, LED_TEMPO,
  LED_PRESET_UP, LED_PRESET_DOWN, LED_PLAY_RESTART, LED_REC_COUNTIN,
  LED_STOP_CLEAR, LED_LEFT_M, LED_RIGHT_S, LED_TRACK_INSTANCE,
  LED_PLUGIN_MIDI, LED_BROWSER, LED_OCT_DOWN, LED_OCT_UP
};

// Mappa button -> led: in questo caso 1:1 perché gli indici sono gli stessi.
// Se un domani cambiano, modifichi solo questa tabella.
const uint8_t btnToLed[N_BUTTONS] = {
  LED_SHIFT, LED_SCALE_EDIT, LED_ARP_EDIT, LED_UNDO_REDO,
  LED_QUANTIZE_AUTO, LED_IDEAS, LED_LOOP, LED_METRO, LED_TEMPO,
  LED_PRESET_UP, LED_PRESET_DOWN, LED_PLAY_RESTART, LED_REC_COUNTIN,
  LED_STOP_CLEAR, LED_LEFT_M, LED_RIGHT_S, LED_TRACK_INSTANCE,
  LED_PLUGIN_MIDI, LED_BROWSER, LED_OCT_DOWN, LED_OCT_UP
};

uint8_t ledState[21] = {0};
volatile bool ledDirty = false;

void ledInit() {
  digitalWrite(LED_RESET, LOW); delay(10);
  digitalWrite(LED_RESET, HIGH); delay(10);
  
  uint8_t cmd1[] = {0x4F, 0x00};
  i2c_writeBuf(LED_I2C_ADDR, cmd1, 2);
  uint8_t cmd2[] = {0x00, 0x01};
  i2c_writeBuf(LED_I2C_ADDR, cmd2, 2);
  uint8_t brightness[38];
  brightness[0] = 0x26;
  for (int i = 0; i < 21; i++) brightness[1 + i] = 0x07;
  for (int i = 21; i < 37; i++) brightness[1 + i] = 0x00;
  i2c_writeBuf(LED_I2C_ADDR, brightness, 38);
  uint8_t cmd3[] = {0x25, 0x01};
  i2c_writeBuf(LED_I2C_ADDR, cmd3, 2);
}

void ledFlush() {
  uint8_t buf[22];
  buf[0] = 0x01;
  memcpy(buf + 1, ledState, 21);
  i2c_writeBuf(LED_I2C_ADDR, buf, 22);
  uint8_t upd[] = {0x25, 0x01};
  i2c_writeBuf(LED_I2C_ADDR, upd, 2);
}

void ledSet(uint8_t idx, uint8_t val) {
  if (idx < 21 && ledState[idx] != val) {
    ledState[idx] = val;
    ledDirty = true;
  }
}

// ==================== OLED ====================
U8G2_SSD1306_128X32_UNIVISION_F_4W_HW_SPI u8g2(U8G2_R2, OLED_CS, OLED_DC, OLED_RES);

float cutoff = 64.0, resonance = 10.0;
float c_dir = 0.8, r_dir = 0.3;

void drawFilterGraph(int cut, int res) {
  int prev_y = 31;
  for (int x = 0; x < 128; x++) {
    float y_func = (x <= cut) ? 18.0 : 18.0 + pow((x - cut) * 0.4, 2);
    y_func -= res * exp(-pow(x - cut, 2) / 30.0);
    int y_final = constrain((int)y_func, 2, 31);
    if (x > 0) u8g2.drawLine(x - 1, prev_y, x, y_final);
    prev_y = y_final;
  }
}

// ==================== SCAN ENGINE ====================
const char* btnNames[N_BUTTONS] = {
  "shift", "scale_edit", "arp_edit", "undo_redo", "quantize_auto",
  "ideas", "loop", "metro", "tempo", "preset_up", "preset_down",
  "play_restart", "rec_countin", "stop_clear", "left_m", "right_s",
  "track_instance", "plugin_midi", "browser", "oct_down", "oct_up"
};
const char* noteNames[] = {"C","Cs","D","Ds","E","F","Fs","G","Gs","A","As","B"};

uint8_t btnLUT[N_COLS][N_SLOTS];
uint8_t keyLUT[N_COLS][N_SLOTS];

void buildLUT() {
  for (int c = 0; c < N_COLS; c++)
    for (int s = 0; s < N_SLOTS; s++) {
      btnLUT[c][s] = 0xFF;
      keyLUT[c][s] = 0xFF;
    }
  struct B { uint8_t c, s, i; };
  B btns[] = {
    {0,0,0},{1,0,1},{2,0,2},{3,0,3},{4,0,4},{5,0,5},
    {0,2,6},{1,2,7},{2,2,8},{3,1,9},{4,1,10},{3,2,11},
    {4,2,12},{5,2,13},{6,1,14},{7,1,15},{2,1,16},{1,1,17},
    {0,1,18},{7,2,19},{6,2,20}
  };
  for (auto& b : btns) btnLUT[b.c][b.s] = b.i;
  
  struct K { uint8_t c, s, i; bool isBot; };
  K keys[] = {
    {7,14,0,false},{7,15,0,true},{6,14,1,false},{6,15,1,true},
    {5,14,2,false},{5,15,2,true},{4,14,3,false},{4,15,3,true},
    {3,14,4,false},{3,15,4,true},{2,14,5,false},{2,15,5,true},
    {1,14,6,false},{1,15,6,true},{0,14,7,false},{0,15,7,true},
    {7,12,8,false},{7,13,8,true},{6,12,9,false},{6,13,9,true},
    {5,12,10,false},{5,13,10,true},{4,12,11,false},{4,13,11,true},
    {3,12,12,false},{3,13,12,true},{2,12,13,false},{2,13,13,true},
    {1,12,14,false},{1,13,14,true},{0,12,15,false},{0,13,15,true},
    {7,10,16,false},{7,11,16,true},{6,10,17,false},{6,11,17,true},
    {5,10,18,false},{5,11,18,true},{4,10,19,false},{4,11,19,true},
    {3,10,20,false},{3,11,20,true},{2,10,21,false},{2,11,21,true},
    {1,10,22,false},{1,11,22,true},{0,10,23,false},{0,11,23,true},
    {7,8,24,false},{7,9,24,true},{6,8,25,false},{6,9,25,true},
    {5,8,26,false},{5,9,26,true},{4,8,27,false},{4,9,27,true},
    {3,8,28,false},{3,9,28,true},{2,8,29,false},{2,9,29,true},
    {1,8,30,false},{1,9,30,true},{0,8,31,false},{0,9,31,true}
  };
  for (auto& k : keys) keyLUT[k.c][k.s] = k.i | (k.isBot ? 0x80 : 0);
}

volatile uint8_t currentStep = 0;
volatile bool pressedState[N_COLS][N_SLOTS];
volatile uint32_t keyTopTime[N_KEYS];
volatile bool keyTopPressed[N_KEYS];

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

static inline uint8_t velocityFromDelta(uint32_t delta_us) {
  if (delta_us < 500) return 127;
  if (delta_us > 50000) return 1;
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
          pushSemEvent(EVT_NOTE_ON, keyIdx, velocityFromDelta(delta));
        }
      }
    }
  }
  
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
  scanTim->setOverflow(30, MICROSEC_FORMAT);
  scanTim->attachInterrupt(scanTimer_ISR);
  scanTim->resume();
}

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  
  SPI.setMOSI(OLED_MOSI);
  SPI.setSCLK(OLED_CLK);
  u8g2.begin();
  
  pinMode(LED_RESET, OUTPUT);
  i2c_init();
  ledInit();
  
  buildLUT();
  scanner_begin();
  
  Serial.println(">>> M32 FIRMWARE READY <<<");
}

// ==================== LOOP ====================
void loop() {
  uint32_t now = millis();
  
  // --- OLED animation 60fps ---
  static uint32_t lastOled = 0;
  if (now - lastOled >= 16) {
    lastOled = now;
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_04b_03_tr);
    u8g2.setCursor(2, 6); u8g2.print("CUT:"); u8g2.print((int)cutoff);
    u8g2.setCursor(90, 6); u8g2.print("RES:"); u8g2.print((int)resonance);
    drawFilterGraph((int)cutoff, (int)resonance);
    u8g2.drawHLine(0, 31, 128);
    u8g2.sendBuffer();
    cutoff += c_dir;
    if (cutoff > 110 || cutoff < 15) c_dir *= -1;
    resonance += r_dir;
    if (resonance > 22 || resonance < 2) r_dir *= -1;
  }
  
  // --- Scan events: button -> LED + serial, keys -> serial ---
  SemEvent e;
  while (scanner_pop(&e)) {
    switch (e.type) {
      case EVT_NOTE_ON: {
        uint8_t note = MIDI_NOTE_START + e.idx;
        Serial.print("NOTE_ON  ");
        Serial.print(noteNames[note % 12]);
        Serial.print(note / 12 - 1);
        Serial.print("  vel=");
        Serial.println(e.velocity);
        break;
      }
      case EVT_NOTE_OFF: {
        uint8_t note = MIDI_NOTE_START + e.idx;
        Serial.print("NOTE_OFF ");
        Serial.print(noteNames[note % 12]);
        Serial.println(note / 12 - 1);
        break;
      }
      case EVT_BTN_DOWN:
        Serial.print("BTN_DOWN  ");
        Serial.println(btnNames[e.idx]);
        ledSet(btnToLed[e.idx], 0xFF);
        break;
      case EVT_BTN_UP:
        Serial.print("BTN_UP    ");
        Serial.println(btnNames[e.idx]);
        ledSet(btnToLed[e.idx], 0x00);
        break;
    }
  }
  
  // --- Flush LED solo se cambiato ---
  if (ledDirty) {
    ledDirty = false;
    ledFlush();
  }
}
