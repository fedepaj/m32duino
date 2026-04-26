/*
 * USBSerial — minimal USB CDC sketch for the M32.
 *
 * The variant's initVariant() already toggles the USB D+ pull-up (PA8) on
 * boot, so the host enumerates Serial without any setup code. Just open
 * the serial monitor at any baud rate.
 *
 * Requires:  Tools → USB support → "CDC (generic 'Serial' supersede U(S)ART)"
 *
 * To re-flash: power-cycle the M32 while holding the forced-DFU button combo
 * (Shift + your combo) so it comes back up in the NI bootloader, then Upload
 * from the IDE.
 */

#include <Arduino.h>

void setup() {
  Serial.begin(115200);
}

void loop() {
  static uint32_t last = 0;
  if (millis() - last >= 1000) {
    last = millis();
    Serial.print("M32 alive @ ");
    Serial.print(last);
    Serial.println(" ms");
  }
}
