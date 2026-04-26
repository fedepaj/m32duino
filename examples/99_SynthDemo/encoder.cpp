#include "encoder.h"
#include "events.h"
#include <math.h>

#define ENC_DEADZONE 2.0f

static volatile uint8_t muxCh = 0;
static volatile uint8_t lastAngleByte[8] = {0};
static volatile int16_t accumByte[8] = {0};
static volatile bool initDone[8] = {false};

static HardwareTimer *encTim;

// ADC bare-metal
static void adc_init() {
  RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
  ADC1->CR2 = 0; ADC1->CR1 = 0;
  ADC1->SMPR2 = (1<<(4*3)) | (1<<(5*3)) | (1<<(6*3)) | (1<<(7*3));
  ADC1->SQR1 = 0;
  ADC1->CR2 = ADC_CR2_ADON;
  delayMicroseconds(10);
  ADC1->CR2 |= ADC_CR2_CAL;
  while (ADC1->CR2 & ADC_CR2_CAL) {}
}

static inline uint16_t adcRead(uint8_t ch) {
  ADC1->SQR3 = ch;
  ADC1->CR2 |= ADC_CR2_ADON;
  while (!(ADC1->SR & ADC_SR_EOC)) {}
  return ADC1->DR;
}

static void encoder_isr() {
  // Mux select
  uint32_t bsrr = 0;
  if (muxCh & 1) bsrr |= (1 << 8); else bsrr |= (1 << (8+16));
  if (muxCh & 2) bsrr |= (1 << 9); else bsrr |= (1 << (9+16));
  GPIOB->BSRR = bsrr;
  
  for (volatile int i = 0; i < 30; i++) __NOP();
  
  // Leggiamo ADC
  int16_t x1 = (int16_t)adcRead(4) - 2048;
  int16_t y1 = (int16_t)adcRead(5) - 2048;
  int16_t x2 = (int16_t)adcRead(6) - 2048;
  int16_t y2 = (int16_t)adcRead(7) - 2048;
  
  uint8_t indices[2] = {muxCh, (uint8_t)(muxCh + 4)};
  int16_t xs[2] = {x1, x2}, ys[2] = {y1, y2};
  
  for (int e = 0; e < 2; e++) {
    uint8_t idx = indices[e];
    int16_t xv = xs[e], yv = ys[e];
    
    // Quadrante + interpolazione lineare per angolo grezzo
    // Restituiamo un valore 0..255 che "gira" con la rotazione
    // Senza trigonometria: usiamo il segno + rapporto |y|/|x| come approssimazione
    
    // CORDIC semplificato a 8 bit? Facciamo semplice:
    // angle_byte = atan2_approx(y, x) * 256 / (2*pi)
    
    int16_t ax = xv < 0 ? -xv : xv;
    int16_t ay = yv < 0 ? -yv : yv;
    int16_t mn = ax < ay ? ax : ay;
    int16_t mx = ax > ay ? ax : ay;
    if (mx == 0) continue;  // centro, indefinito
    
    // atan2 approx: uso la formula octant-based
    // angolo in range [0..255] corrispondente a [0..2pi]
    uint8_t a;
    int16_t ratio = (mn * 64) / mx;  // 0..64
    if (ax >= ay) {
    a = ratio;  // 0..64
    if (xv < 0) a = 128 - a;
    if (yv < 0) a = 256 - a;
  } else {
    a = 128 - ratio;  // 64..128
    if (xv < 0) a = 128 - a;
    if (yv < 0) a = 256 - a;
  }
    
    if (!initDone[idx]) { lastAngleByte[idx] = a; initDone[idx] = true; continue; }
    
    int8_t delta = (int8_t)(lastAngleByte[idx] - a);  // signed wrap-around automatico
    lastAngleByte[idx] = a;
    
    accumByte[idx] += delta;
    if (accumByte[idx] >= 2 || accumByte[idx] <= -2) {
      int16_t emit = accumByte[idx];
      accumByte[idx] = 0;
      pushEvent(EVT_ENC_DELTA, idx, emit);
    }
  }
  
  muxCh = (muxCh + 1) & 0x03;
}

void encoder_init() {
  pinMode(PB8, OUTPUT);
  pinMode(PB9, OUTPUT);
  pinMode(PA4, INPUT_ANALOG);
  pinMode(PA5, INPUT_ANALOG);
  pinMode(PA6, INPUT_ANALOG);
  pinMode(PA7, INPUT_ANALOG);
  
  adc_init();
  
  encTim = new HardwareTimer(TIM1);
  encTim->setOverflow(500, MICROSEC_FORMAT);  // 500us tick
  encTim->attachInterrupt(encoder_isr);
  encTim->resume();
}
