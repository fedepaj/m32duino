#include <Arduino.h>

// === JOYCODER PINS ===
#define JOY_ENC_A   PC6   // TIM3_CH1
#define JOY_ENC_B   PC7   // TIM3_CH2
#define JOY_BTN     PB13  // encoder push
#define JOY_RIGHT   PA2
#define JOY_DOWN    PA3
#define JOY_LEFT    PA0
#define JOY_UP      PA1 

// ==================== EVENT BUFFER ====================
enum EvtType { EVT_ENC_DELTA, EVT_BTN_DOWN, EVT_BTN_UP };
enum BtnId { BTN_ENC=0, BTN_RIGHT, BTN_DOWN, BTN_LEFT, BTN_UP };
const char* btnNames[] = {"enc_click", "right", "down", "left", "up"};

struct Event { uint8_t type; uint8_t idx; int16_t value; };
#define EVT_BUF_SIZE 32
volatile Event evtBuf[EVT_BUF_SIZE];
volatile uint8_t evtHead = 0, evtTail = 0;

static inline void pushEvent(uint8_t type, uint8_t idx, int16_t val) {
  uint8_t head = evtHead;
  uint8_t nh = (head + 1) & (EVT_BUF_SIZE - 1);
  if (nh != evtTail) {
    evtBuf[head].type = type;
    evtBuf[head].idx = idx;
    evtBuf[head].value = val;
    evtHead = nh;
  }
}

// ==================== TIM3 HARDWARE ENCODER ====================
// Encoder mode: conta automaticamente su PC6 (TIM3_CH1) e PC7 (TIM3_CH2)
// Serve remap parziale di TIM3 per usare PC6/PC7: TIM3_REMAP = 10 (full remap)

void tim3_encoder_init() {
  // Abilita clock TIM3 e AFIO
  RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;
  RCC->APB2ENR |= RCC_APB2ENR_AFIOEN;
  
  // Remap TIM3: bits 11:10 di AFIO_MAPR = 10 (full remap -> PC6, PC7, PB0, PB1)
  AFIO->MAPR = (AFIO->MAPR & ~AFIO_MAPR_TIM3_REMAP) | AFIO_MAPR_TIM3_REMAP_FULLREMAP;
  
  // Configura PC6 e PC7 come input con pull-up
  // CRL bit (6*4=24) e (7*4=28)
  GPIOC->CRL = (GPIOC->CRL & ~((0xF << 24) | (0xF << 28))) | ((0x8 << 24) | (0x8 << 28));
  GPIOC->ODR |= (1 << 6) | (1 << 7);  // pull-up
  
  // TIM3 encoder mode
  TIM3->CR1 = 0;
  TIM3->SMCR = 0x03;  // SMS = 011: encoder mode 3, count on both edges of TI1 and TI2
  TIM3->CCMR1 = (0x01 << 0) | (0x01 << 8);  // CC1S=01 (TI1), CC2S=01 (TI2)
  TIM3->CCER = 0;  // no polarity inversion
  TIM3->ARR = 0xFFFF;
  TIM3->CNT = 0x8000;  // parti da metà per evitare underflow
  TIM3->CR1 = TIM_CR1_CEN;  // enable
}

int16_t encoder_readDelta() {
  static uint16_t last = 0x8000;
  static int16_t accum = 0;
  uint16_t now = TIM3->CNT;
  int16_t raw = (int16_t)(now - last);
  last = now;
  
  accum += raw;
  int16_t out = accum / 2;
  accum -= out * 2;
  return out;
}

// ==================== BUTTONS with EXTI ====================
const uint32_t btnPins[] = {JOY_BTN, JOY_RIGHT, JOY_DOWN, JOY_LEFT, JOY_UP};
const uint8_t nBtns = 5;

uint32_t lastBtnMs[5] = {0};
#define BTN_DEBOUNCE_MS 15
bool btnLastState[5] = {false};  // true = premuto

void handleBtn(uint8_t idx) {
  uint32_t now = millis();
  if (now - lastBtnMs[idx] < BTN_DEBOUNCE_MS) return;
  
  bool pressed = (digitalRead(btnPins[idx]) == LOW);
  if (pressed == btnLastState[idx]) return;
  
  btnLastState[idx] = pressed;
  lastBtnMs[idx] = now;
  pushEvent(pressed ? EVT_BTN_DOWN : EVT_BTN_UP, idx, 0);
}

void isrBtnEnc()   { handleBtn(0); }
void isrBtnRight() { handleBtn(1); }
void isrBtnDown()  { handleBtn(2); }
void isrBtnLeft()  { handleBtn(3); }
void isrBtnUp()    { handleBtn(4); }

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10); delay(500);
  
  tim3_encoder_init();
  
  pinMode(JOY_BTN, INPUT_PULLUP);
  pinMode(JOY_RIGHT, INPUT_PULLUP);
  pinMode(JOY_DOWN, INPUT_PULLUP);
  pinMode(JOY_LEFT, INPUT_PULLUP);
  pinMode(JOY_UP, INPUT_PULLUP);
  
  attachInterrupt(digitalPinToInterrupt(JOY_BTN),   isrBtnEnc,   CHANGE);
  attachInterrupt(digitalPinToInterrupt(JOY_RIGHT), isrBtnRight, CHANGE);
  attachInterrupt(digitalPinToInterrupt(JOY_DOWN),  isrBtnDown,  CHANGE);
  attachInterrupt(digitalPinToInterrupt(JOY_LEFT),  isrBtnLeft,  CHANGE);
  attachInterrupt(digitalPinToInterrupt(JOY_UP),    isrBtnUp,    CHANGE);
  
  Serial.println(">>> Joycoder test <<<");
}

// ==================== LOOP ====================
void loop() {
  // Leggi encoder ogni 10ms
  static uint32_t lastEnc = 0;
  if (millis() - lastEnc >= 10) {
    lastEnc = millis();
    int16_t d = encoder_readDelta();
    if (d != 0) pushEvent(EVT_ENC_DELTA, 0, d);
  }
  static uint32_t lastDbg = 0;

  // Consuma eventi
  while (evtTail != evtHead) {
    noInterrupts();
    Event e = const_cast<Event&>(evtBuf[evtTail]);
    evtTail = (evtTail + 1) & (EVT_BUF_SIZE - 1);
    interrupts();
    
    switch (e.type) {
      case EVT_ENC_DELTA:
        Serial.print("JOY_ENC delta=");
        if (e.value > 0) Serial.print("+");
        Serial.println(e.value);
        break;
      case EVT_BTN_DOWN:
        Serial.print("JOY_BTN_DOWN "); Serial.println(btnNames[e.idx]);
        break;
      case EVT_BTN_UP:
        Serial.print("JOY_BTN_UP   "); Serial.println(btnNames[e.idx]);
        break;
    }
  }
}
