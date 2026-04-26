#include <Arduino.h>

// Shift register / 74HC138 control
#define SEL_A0   PB0
#define SEL_A1   PB1
#define SEL_A2   PB2
#define SEL_CMD  PB14

// Maschera per scrittura atomica su GPIOB (PB0, PB1, PB2, PB14)
#define MASK_CTRL (0x4007)  // bit 0, 1, 2, 14

// Colonne tastiera: PC8-PC15
const uint32_t colPins[] = {PC8, PC9, PC10, PC11, PC12, PC13, PC14, PC15};
const int nCols = sizeof(colPins) / sizeof(colPins[0]);

// Stato corrente scan (contatore 4 bit: bit 0-2 = A0-A2, bit 3 = CMD)
volatile uint8_t currentStep = 0;

// Buffer eventi (ring buffer per evitare collisioni ISR/loop)
struct Event { uint8_t col; uint8_t slot; };
#define EVT_BUF_SIZE 64
volatile Event evtBuf[EVT_BUF_SIZE];
volatile uint8_t evtHead = 0;
volatile uint8_t evtTail = 0;

// Debounce: timestamp ultima pressione per ogni (col, slot)
uint32_t lastPressMs[8][16] = {0};
#define DEBOUNCE_MS 30

// --- TIMER ISR: avanza il contatore e scrive i pin ---
void scanTimer_ISR() {
  currentStep = (currentStep + 1) & 0x0F;
  
  // Calcola valore: bit 0-2 dello step vanno su PB0-PB2, bit 3 va su PB14
  uint32_t val = (currentStep & 0x07) | ((currentStep & 0x08) << 11);
  
  // Reset atomico dei 4 bit + set dei nuovi valori
  GPIOB->BSRR = (MASK_CTRL << 16) | val;
}

// --- ISR PIN: cattura col+slot istantanei ---
inline void captureHit(uint8_t colIdx) {
  uint8_t nextHead = (evtHead + 1) & (EVT_BUF_SIZE - 1);
  if (nextHead != evtTail) {  // buffer non pieno
    evtBuf[evtHead].col = colIdx;
    evtBuf[evtHead].slot = currentStep;
    evtHead = nextHead;
  }
}

void isrC8()  { captureHit(0); }
void isrC9()  { captureHit(1); }
void isrC10() { captureHit(2); }
void isrC11() { captureHit(3); }
void isrC12() { captureHit(4); }
void isrC13() { captureHit(5); }
void isrC14() { captureHit(6); }
void isrC15() { captureHit(7); }

typedef void (*voidFn)(void);
voidFn isrTable[] = {isrC8, isrC9, isrC10, isrC11, isrC12, isrC13, isrC14, isrC15};

HardwareTimer *scanTim;

void setup() {
  Serial.begin(115200);
  
  // Select pin come output
  pinMode(SEL_A0, OUTPUT);
  pinMode(SEL_A1, OUTPUT);
  pinMode(SEL_A2, OUTPUT);
  pinMode(SEL_CMD, OUTPUT);
  
  // Colonne come input pull-up con interrupt FALLING
  for (int i = 0; i < nCols; i++) {
    pinMode(colPins[i], INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(colPins[i]), isrTable[i], FALLING);
  }
  
  // Timer TIM2: tick ogni 10us → ciclo completo (16 step) ~ 160us = ~6kHz
  scanTim = new HardwareTimer(TIM2);
  scanTim->setOverflow(10, MICROSEC_FORMAT);
  scanTim->attachInterrupt(scanTimer_ISR);
  scanTim->resume();
  
  Serial.println(">>> M32 KEY SCANNER READY <<<");
  Serial.println("Premi pulsanti/tasti, output: COL <n> SLOT <n>");
}

void loop() {
  while (evtTail != evtHead) {
    noInterrupts();
    Event e = const_cast<Event&>(evtBuf[evtTail]);
    evtTail = (evtTail + 1) & (EVT_BUF_SIZE - 1);
    interrupts();
    
    // Debounce software
    uint32_t now = millis();
    if (now - lastPressMs[e.col][e.slot] < DEBOUNCE_MS) continue;
    lastPressMs[e.col][e.slot] = now;
    
    Serial.print("COL ");
    Serial.print(e.col);
    Serial.print(" (PC");
    Serial.print(8 + e.col);
    Serial.print(")  SLOT ");
    Serial.println(e.slot);
  }
}