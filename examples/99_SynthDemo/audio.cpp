#include "audio.h"
#include "mappings.h"
#include "synth_state.h"

#define AUDIO_L_BIT (1 << 4)
#define AUDIO_R_BIT (1 << 5)

uint32_t audioBuf[DMA_BUF_SIZE];

static volatile bool voiceActive = false;
static volatile uint8_t voiceNote = 0;
static volatile uint8_t voiceVelocity = 100;

static volatile uint32_t phase1 = 0, phase2 = 0;
static volatile uint32_t inc1 = 0, inc2 = 0;
static volatile uint32_t inc1Cur = 0, inc2Cur = 0;

static volatile uint8_t ampState = 0;
static volatile int32_t ampEnv = 0;

static volatile uint8_t filtState = 0;
static volatile int32_t filtEnv = 0;

static volatile int32_t lpState = 0;

static int32_t sdAccumL = 0;
static int32_t sdAccumR = 0;

static int16_t currentSample = 0;

static volatile uint32_t ampAttStep = 100, ampDecStep = 100, ampRelStep = 100;
static volatile uint32_t filtAttStep = 100, filtDecStep = 100, filtRelStep = 100;

static inline uint32_t envStepFromMs(uint16_t ms) {
  if (ms == 0) return 32767;
  return (32767L * 1000L) / ((int32_t)ms * SAMPLE_RATE);
}

void audio_updateEnvCoefs() {
  ampAttStep = envStepFromMs(synth.aAtt);
  ampDecStep = envStepFromMs(synth.aDec);
  ampRelStep = envStepFromMs(synth.aRel);
  filtAttStep = envStepFromMs(synth.fAtt);
  filtDecStep = envStepFromMs(synth.fDec);
  filtRelStep = envStepFromMs(synth.fRel);
}

static inline __attribute__((always_inline)) int16_t renderAudioSample() {
  switch (ampState) {
    case 1:
      ampEnv += ampAttStep;
      if (ampEnv >= 32767) { ampEnv = 32767; ampState = 2; }
      break;
    case 2: {
      int32_t target = synth.aSus >> 1;
      ampEnv -= ampDecStep;
      if (ampEnv <= target) { ampEnv = target; ampState = 3; }
      break;
    }
    case 3: break;
    case 4:
      ampEnv -= ampRelStep;
      if (ampEnv <= 0) { ampEnv = 0; ampState = 0; voiceActive = false; }
      break;
  }
  
  if (ampState == 0 && ampEnv == 0) return 0;
  
  switch (filtState) {
    case 1:
      filtEnv += filtAttStep;
      if (filtEnv >= 32767) { filtEnv = 32767; filtState = 2; }
      break;
    case 2: {
      int32_t t = synth.fSus >> 1;
      filtEnv -= filtDecStep;
      if (filtEnv <= t) { filtEnv = t; filtState = 3; }
      break;
    }
    case 3: break;
    case 4:
      filtEnv -= filtRelStep;
      if (filtEnv <= 0) { filtEnv = 0; filtState = 0; }
      break;
  }
  
  if (synth.glide == 0) {
    inc1Cur = inc1;
    inc2Cur = inc2;
  } else {
    int32_t shift = (synth.glide >> 13) + 1;
    inc1Cur += ((int32_t)(inc1 - inc1Cur)) >> shift;
    inc2Cur += ((int32_t)(inc2 - inc2Cur)) >> shift;
  }
  
  phase1 += inc1Cur;
  phase2 += inc2Cur;
  uint8_t i1 = phase1 >> 24;
  uint8_t i2 = phase2 >> 24;
  int16_t s1 = getWaveTable(synth.wave1)[i1];
  int16_t s2 = getWaveTable(synth.wave2)[i2];
  
  int32_t mixed = ((int32_t)s1 * (65535 - synth.mix) + (int32_t)s2 * synth.mix) >> 16;
  
  int32_t cutoffMod = synth.cutoff + (((int32_t)filtEnv * synth.fEnvAmount) >> 15);
  if (cutoffMod < 100) cutoffMod = 100;
  if (cutoffMod > 65535) cutoffMod = 65535;
  lpState += ((int32_t)(mixed - lpState) * cutoffMod) >> 16;
  
  int32_t out = lpState;
  out = (out * ampEnv) >> 15;
  out = (out * voiceVelocity) >> 7;
  out = (out * synth.volume) >> 16;
  
  if (out > 32767) out = 32767;
  if (out < -32768) out = -32768;
  
  return (int16_t)out;
}

static void __attribute__((optimize("O3"))) fillBuffer(uint32_t* dst, int n) {
  for (int i = 0; i < n; i++) {
    if ((i & (OVERSAMP - 1)) == 0) {
      currentSample = renderAudioSample();
    }
    
    uint32_t bsrr = 0;
    sdAccumL += currentSample;
    if (sdAccumL > 0) { bsrr |= AUDIO_L_BIT; sdAccumL -= 32767; }
    else { bsrr |= AUDIO_L_BIT << 16; sdAccumL += 32767; }
    
    sdAccumR += currentSample;
    if (sdAccumR > 0) { bsrr |= AUDIO_R_BIT; sdAccumR -= 32767; }
    else { bsrr |= AUDIO_R_BIT << 16; sdAccumR += 32767; }
    
    dst[i] = bsrr;
  }
}

extern "C" void DMA1_Channel7_IRQHandler() {
  uint32_t isr = DMA1->ISR;
  if (isr & DMA_ISR_HTIF7) {
    DMA1->IFCR = DMA_IFCR_CHTIF7;
    fillBuffer(&audioBuf[0], DMA_BUF_SIZE / 2);
  }
  if (isr & DMA_ISR_TCIF7) {
    DMA1->IFCR = DMA_IFCR_CTCIF7;
    fillBuffer(&audioBuf[DMA_BUF_SIZE / 2], DMA_BUF_SIZE / 2);
  }
}

void audio_init() {
  RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;
  RCC->APB1ENR |= RCC_APB1ENR_TIM4EN;
  RCC->AHBENR  |= RCC_AHBENR_DMA1EN;
  
  GPIOC->CRL = (GPIOC->CRL & ~((0xF << (4*4)) | (0xF << (5*4))))
             | ((0x3 << (4*4)) | (0x3 << (5*4)));
  GPIOC->BSRR = (AUDIO_L_BIT | AUDIO_R_BIT) << 16;
  
  for (int i = 0; i < DMA_BUF_SIZE; i++) audioBuf[i] = 0;
  
  DMA1_Channel7->CCR = 0;
  DMA1_Channel7->CPAR = (uint32_t)&GPIOC->BSRR;
  DMA1_Channel7->CMAR = (uint32_t)audioBuf;
  DMA1_Channel7->CNDTR = DMA_BUF_SIZE;
  DMA1_Channel7->CCR = DMA_CCR_MSIZE_1
                     | DMA_CCR_PSIZE_1
                     | DMA_CCR_MINC
                     | DMA_CCR_CIRC
                     | DMA_CCR_DIR
                     | DMA_CCR_HTIE
                     | DMA_CCR_TCIE
                     | DMA_CCR_EN;
  
  NVIC_SetPriority(DMA1_Channel7_IRQn, 1);
  NVIC_EnableIRQ(DMA1_Channel7_IRQn);
  
  TIM4->CR1 = 0;
  TIM4->PSC = 0;
  TIM4->ARR = (SystemCoreClock / CARRIER) - 1;
  TIM4->CNT = 0;
  TIM4->DIER = TIM_DIER_UDE;
  TIM4->CR1 = TIM_CR1_CEN;
  
  audio_updateEnvCoefs();
}

void audio_noteOn(uint8_t midi, uint8_t velocity) {
  if (midi >= 128) return;
  voiceNote = midi;
  voiceVelocity = velocity;
  inc1 = noteToInc[midi];
  int32_t detuneShift = ((int32_t)synth.detune * (int32_t)inc1) / 1731;
  inc2 = inc1 + detuneShift;
  
  if (!voiceActive || ampState == 0) {
    ampState = 1;
    filtState = 1;
    if (synth.glide == 0) {
      inc1Cur = inc1;
      inc2Cur = inc2;
    }
    voiceActive = true;
  }
}

void audio_noteOff() {
  if (voiceActive) {
    ampState = 4;
    filtState = 4;
  }
}

bool audio_voiceActive() { return voiceActive; }
uint8_t audio_currentNote() { return voiceNote; }
uint16_t audio_currentAmpEnv() { return ampEnv * 2; }
uint16_t audio_currentFiltEnv() { return filtEnv * 2; }
uint8_t audio_currentPhase() { return phase1 >> 24; }
