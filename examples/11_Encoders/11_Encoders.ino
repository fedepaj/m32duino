#include <Arduino.h>
#include <Wire.h>
#include <math.h>

// ==================== ENCODER CONFIG ====================
#define MUX_A PB8
#define MUX_B PB9
#define ENC_X1 PA4
#define ENC_Y1 PA5
#define ENC_X2 PA6
#define ENC_Y2 PA7

// === PARAMETRI DI REATTIVITÀ ===
#define ENCODER_TICK_US       250   // 250us tick, 4 canali = 1ms ciclo = 1kHz per encoder
#define ENCODER_DEADZONE_DEG  2.0f  // soglia emissione evento
#define ENCODER_SENSITIVITY   1.0f  // moltiplicatore delta

// ==================== TOUCH CONFIG ====================
TwoWire Wire2(PIN_TOUCH_ENC_SDA, PIN_TOUCH_ENC_SCL);  // I2C2 — encoder-body touch
#define TOUCH_ADDR 0x50
#define TOUCH_POLL_MS 10

// ==================== EVENT BUFFER ====================
enum EvtType { EVT_ENC_DELTA, EVT_TOUCH_ON, EVT_TOUCH_OFF };
struct Event { uint8_t type; uint8_t idx; int16_t value; };
#define EVT_BUF_SIZE 64
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

// ==================== ADC BARE-METAL ====================
void adc_init() {
  RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
  // ADC clock = PCLK2/6 = 12MHz (max 14MHz)
  RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_ADCPRE) | RCC_CFGR_ADCPRE_DIV6;
  
  ADC1->CR2 = 0;
  ADC1->CR1 = 0;
  // Sample time 7.5 cycles su canali 4,5,6,7
  ADC1->SMPR2 = (1 << (4*3)) | (1 << (5*3)) | (1 << (6*3)) | (1 << (7*3));
  ADC1->SQR1 = 0;
  ADC1->CR2 = ADC_CR2_ADON;
  delayMicroseconds(10);
  // Calibrazione
  ADC1->CR2 |= ADC_CR2_CAL;
  while (ADC1->CR2 & ADC_CR2_CAL) {}
}

static inline uint16_t adcRead(uint8_t channel) {
  ADC1->SQR3 = channel;
  ADC1->CR2 |= ADC_CR2_ADON;
  while (!(ADC1->SR & ADC_SR_EOC)) {}
  return ADC1->DR;
}

// ==================== ENCODER STATE (ISR) ====================
volatile uint8_t currentMuxCh = 0;
volatile float lastAngle[8] = {0};
volatile float accumDegrees[8] = {0};
volatile bool encInit[8] = {false};

void encoderTimer_ISR() {
  uint32_t bsrr_mask = 0;
  if (currentMuxCh & 1) bsrr_mask |= (1 << 8); else bsrr_mask |= (1 << (8+16));
  if (currentMuxCh & 2) bsrr_mask |= (1 << 9); else bsrr_mask |= (1 << (9+16));
  GPIOB->BSRR = bsrr_mask;
  
  // Settling
  for (volatile int i = 0; i < 20; i++) __NOP();
  
  uint16_t x1 = adcRead(4);
  uint16_t y1 = adcRead(5);
  uint16_t x2 = adcRead(6);
  uint16_t y2 = adcRead(7);
  
  struct { uint8_t idx; uint16_t x, y; } enc[2] = {
    {currentMuxCh, x1, y1},
    {(uint8_t)(currentMuxCh + 4), x2, y2}
  };
  
  for (int e = 0; e < 2; e++) {
    uint8_t idx = enc[e].idx;
    float fx = enc[e].x - 2048.0f;
    float fy = enc[e].y - 2048.0f;
    float angle = atan2f(fy, fx);
    
    if (!encInit[idx]) {
      lastAngle[idx] = angle;
      encInit[idx] = true;
      continue;
    }
    
    float delta = lastAngle[idx] - angle;
    if (delta > M_PI) delta -= 2 * M_PI;
    if (delta < -M_PI) delta += 2 * M_PI;
    
    float deltaDeg = delta * (180.0f / M_PI) * ENCODER_SENSITIVITY;
    accumDegrees[idx] += deltaDeg;
    lastAngle[idx] = angle;
    
    if (fabsf(accumDegrees[idx]) >= ENCODER_DEADZONE_DEG) {
      int16_t emit = (int16_t)accumDegrees[idx];
      accumDegrees[idx] -= emit;
      pushEvent(EVT_ENC_DELTA, idx, emit);
    }
  }
  
  currentMuxCh = (currentMuxCh + 1) & 0x03;
}

HardwareTimer *encTim;

void encoder_begin() {
  pinMode(MUX_A, OUTPUT);
  pinMode(MUX_B, OUTPUT);
  pinMode(ENC_X1, INPUT_ANALOG);
  pinMode(ENC_Y1, INPUT_ANALOG);
  pinMode(ENC_X2, INPUT_ANALOG);
  pinMode(ENC_Y2, INPUT_ANALOG);
  
  adc_init();
  
  encTim = new HardwareTimer(TIM3);
  encTim->setOverflow(ENCODER_TICK_US, MICROSEC_FORMAT);
  encTim->attachInterrupt(encoderTimer_ISR);
  encTim->resume();
}

// ==================== TOUCH ====================
uint8_t lastTouch = 0;
uint32_t lastTouchPoll = 0;

void touch_poll(uint32_t now) {
  if (now - lastTouchPoll < TOUCH_POLL_MS) return;
  lastTouchPoll = now;
  
  Wire2.beginTransmission(TOUCH_ADDR);
  Wire2.write(0xA0);
  if (Wire2.endTransmission(false) != 0) return;
  Wire2.requestFrom(TOUCH_ADDR, (uint8_t)1);
  if (!Wire2.available()) return;
  uint8_t curr = Wire2.read();
  
  uint8_t changed = curr ^ lastTouch;
  if (changed) {
    for (int i = 0; i < 8; i++) {
      if (changed & (1 << i)) {
        bool on = (curr & (1 << i)) != 0;
        pushEvent(on ? EVT_TOUCH_ON : EVT_TOUCH_OFF, i, 0);
      }
    }
    lastTouch = curr;
  }
}

// ==================== SETUP/LOOP ====================
void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10); delay(500);
  
  Wire2.begin();
  Wire2.setClock(400000);
  
  encoder_begin();
  
  Serial.println(">>> Encoders + Touch (ISR bare-metal) <<<");
  Serial.print("Sample rate per encoder: ");
  Serial.print(1000000 / (ENCODER_TICK_US * 4));
  Serial.println(" Hz");
  Serial.print("Deadzone: ");
  Serial.print(ENCODER_DEADZONE_DEG);
  Serial.println("°");
}

void loop() {
  touch_poll(millis());
  
  while (evtTail != evtHead) {
    noInterrupts();
    Event e = const_cast<Event&>(evtBuf[evtTail]);
    evtTail = (evtTail + 1) & (EVT_BUF_SIZE - 1);
    interrupts();
    
    switch (e.type) {
      case EVT_ENC_DELTA:
        Serial.print("ENC"); Serial.print(e.idx);
        Serial.print(" delta=");
        if (e.value > 0) Serial.print("+");
        Serial.println(e.value);
        break;
      case EVT_TOUCH_ON:
        Serial.print("TOUCH_ON  enc"); Serial.println(e.idx);
        break;
      case EVT_TOUCH_OFF:
        Serial.print("TOUCH_OFF enc"); Serial.println(e.idx);
        break;
    }
  }
}