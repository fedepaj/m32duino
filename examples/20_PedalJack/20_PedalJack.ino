#include <Arduino.h>

#define JACK_DETECT PB12
#define PEDAL_TIP   PC4   // ADC1 ch14
#define PEDAL_RING  PC5   // ADC1 ch15

// ==================== ADC BARE-METAL ====================
void adc_init() {
  RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
  RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_ADCPRE) | RCC_CFGR_ADCPRE_DIV6;
  
  ADC1->CR2 = 0;
  ADC1->CR1 = 0;
  // Sample time: 71.5 cicli (codice 0b110) per pin con impedenza alta tipo pedale
  ADC1->SMPR1 = (6 << ((14-10)*3)) | (6 << ((15-10)*3));  // ch14, ch15 in SMPR1
  ADC1->SQR1 = 0;
  ADC1->CR2 = ADC_CR2_ADON;
  delayMicroseconds(10);
  ADC1->CR2 |= ADC_CR2_CAL;
  while (ADC1->CR2 & ADC_CR2_CAL) {}
}

uint16_t adcRead(uint8_t channel) {
  ADC1->SQR3 = channel;
  ADC1->CR2 |= ADC_CR2_ADON;
  while (!(ADC1->SR & ADC_SR_EOC)) {}
  return ADC1->DR;
}

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10); delay(500);
  
  pinMode(JACK_DETECT, INPUT_PULLUP);
  pinMode(PEDAL_TIP, INPUT_ANALOG);
  pinMode(PEDAL_RING, INPUT_ANALOG);
  
  adc_init();
  
  Serial.println(">>> Pedal test <<<");
  Serial.println("Format: detect | tip(pc4) | ring(pc5)");
}

void loop() {
  bool jack = digitalRead(JACK_DETECT) == HIGH;
  uint16_t tip = adcRead(14);
  uint16_t ring = adcRead(15);
  
  static uint32_t lastPrint = 0;
  if (millis() - lastPrint >= 100) {
    lastPrint = millis();
    Serial.print("JACK="); Serial.print(jack ? "IN " : "OUT");
    Serial.print("  TIP="); Serial.print(tip);
    Serial.print("  RING="); Serial.println(ring);
  }
  
  delay(20);
}
