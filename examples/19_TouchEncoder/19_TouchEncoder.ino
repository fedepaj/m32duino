#include <Arduino.h>
#include <Wire.h>

TwoWire Wire2(PIN_TOUCH_ENC_SDA, PIN_TOUCH_ENC_SCL);  // I2C2 — encoder-body touch

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);
  delay(500);
  
  Wire2.begin();
  Wire2.setClock(100000);
  
  Serial.println(">>> Touch sensor test <<<");
}

uint8_t readTouch() {
  Wire2.beginTransmission(0x50);
  Wire2.write(0xA0);
  Wire2.endTransmission(false);  // repeated start
  Wire2.requestFrom(0x50, 1);
  if (Wire2.available()) return Wire2.read();
  return 0;
}

void loop() {
  static uint8_t prev = 0;
  uint8_t now = readTouch();
  
  if (now != prev) {
    Serial.print("Touch: 0x");
    if (now < 16) Serial.print("0");
    Serial.print(now, HEX);
    Serial.print(" ( ");
    for (int i = 7; i >= 0; i--) Serial.print((now >> i) & 1);
    Serial.print(" )");
    
    // Stampa quali encoder sono toccati
    for (int i = 0; i < 8; i++) {
      if ((now & (1 << i)) && !(prev & (1 << i))) {
        Serial.print("  +enc"); Serial.print(i);
      }
      if (!(now & (1 << i)) && (prev & (1 << i))) {
        Serial.print("  -enc"); Serial.print(i);
      }
    }
    Serial.println();
    prev = now;
  }
  delay(6);
}