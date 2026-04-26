#include <Arduino.h>
#include <Wire.h>

#define LED_RESET  PA15

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);
  delay(500);
  
  // Reset LED driver
  pinMode(LED_RESET, OUTPUT);
  digitalWrite(LED_RESET, LOW);
  delay(10);
  digitalWrite(LED_RESET, HIGH);
  delay(10);
  
  Wire.setSDA(PB7);
  Wire.setSCL(PB6);
  Wire.begin();
  
  Serial.println("Init LED driver...");
  
  // Sequenza init NI catturata
  // 0x4F 0x00
  Wire.beginTransmission(0x3C);
  Wire.write(0x4F); Wire.write(0x00);
  Serial.print("cmd 0x4F: "); Serial.println(Wire.endTransmission());
  
  // 0x00 0x01
  Wire.beginTransmission(0x3C);
  Wire.write(0x00); Wire.write(0x01);
  Serial.print("cmd 0x00: "); Serial.println(Wire.endTransmission());
  
  // 0x26 + 37 byte (brightness)
  uint8_t brightness[] = {
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  };
  Wire.beginTransmission(0x3C);
  Wire.write(0x26);
  Wire.write(brightness, sizeof(brightness));
  Serial.print("cmd 0x26: "); Serial.println(Wire.endTransmission());
  
  // 0x25 0x01 (update)
  Wire.beginTransmission(0x3C);
  Wire.write(0x25); Wire.write(0x01);
  Serial.print("cmd 0x25: "); Serial.println(Wire.endTransmission());
  
  Serial.println("Init done, starting animation");
}

void loop() {
  // Fai lampeggiare tutti i 21 LED con valori crescenti
  static uint8_t v = 0;
  
  uint8_t data[21];
  for (int i = 0; i < 21; i++) data[i] = v;
  
  Wire.beginTransmission(0x3C);
  Wire.write(0x01);
  Wire.write(data, 21);
  Wire.endTransmission();
  
  // Update
  Wire.beginTransmission(0x3C);
  Wire.write(0x25); Wire.write(0x01);
  Wire.endTransmission();
  
  v = (v + 8) & 0xFF;
  delay(100);
}