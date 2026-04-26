#include <Arduino.h>
#include <Wire.h>

#define LED_RESET  PA15

int currentLED = -1;

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);
  delay(500);
  
  pinMode(LED_RESET, OUTPUT);
  digitalWrite(LED_RESET, LOW);
  delay(10);
  digitalWrite(LED_RESET, HIGH);
  delay(10);
  
  Wire.setSDA(PB7);
  Wire.setSCL(PB6);
  Wire.begin();
  
  Serial.println("Init LED driver...");
  
  Wire.beginTransmission(0x3C);
  Wire.write(0x4F); Wire.write(0x00);
  Serial.print("cmd 0x4F: "); Serial.println(Wire.endTransmission());
  
  Wire.beginTransmission(0x3C);
  Wire.write(0x00); Wire.write(0x01);
  Serial.print("cmd 0x00: "); Serial.println(Wire.endTransmission());
  
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
  
  Wire.beginTransmission(0x3C);
  Wire.write(0x25); Wire.write(0x01);
  Serial.print("cmd 0x25: "); Serial.println(Wire.endTransmission());
  
  Serial.println("Init done.");
  Serial.println("Premi INVIO nel serial monitor per accendere il prossimo LED.");
  Serial.println("Annota quale pulsante si accende per ogni indice 0-20.");
}

void loop() {
  if (Serial.available()) {
    while (Serial.available()) Serial.read();
    
    currentLED++;
    if (currentLED >= 21) currentLED = 0;
    
    uint8_t data[21] = {0};
    data[currentLED] = 0xFF;
    
    Wire.beginTransmission(0x3C);
    Wire.write(0x01);
    Wire.write(data, 21);
    Wire.endTransmission();
    
    Wire.beginTransmission(0x3C);
    Wire.write(0x25); Wire.write(0x01);
    Wire.endTransmission();
    
    Serial.print("LED index ");
    Serial.print(currentLED);
    Serial.println(" ON");
  }
}