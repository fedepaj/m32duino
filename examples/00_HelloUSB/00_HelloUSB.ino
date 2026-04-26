/*
 * 00_HelloUSB — first sketch to verify the whole toolchain.
 *
 * Prints a heartbeat on USB CDC at 115200 bps. If you can see this line
 * appearing in the Arduino Serial Monitor after flashing, everything
 * (variant, linker offset @ 0x08003000, USB, auto-DFU) is working.
 *
 * Requires Tools → USB support → "CDC (generic Serial supersede U(n)ART)".
 *
 * Also implements the 1200 bps-touch auto-DFU reset (jumps back into the
 * NI bootloader) so every subsequent Upload from the IDE is one-click.
 */

#include <Arduino.h>

static void jump_to_bootloader(void) {
  HAL_RCC_DeInit();
  HAL_DeInit();
  __disable_irq();
  SysTick->CTRL = 0;
  SCB->VTOR = 0x08000000;
  uint32_t sp = *(volatile uint32_t *)0x08000000;
  uint32_t pc = *(volatile uint32_t *)0x08000004;
  __set_MSP(sp);
  ((void(*)(void))pc)();
  while (1) {}
}

void setup() {
  Serial.begin(115200);
  /* Wait briefly for the host to open the port, then carry on regardless. */
  uint32_t t0 = millis();
  while (!Serial && (millis() - t0 < 2000)) { delay(10); }
  Serial.println();
  Serial.println("=== NI Komplete Kontrol M32 — HelloUSB ===");
  Serial.println("Firmware is running from 0x08003000 (app partition).");
  Serial.println("Send 'DFU' to jump back to the NI bootloader, or open this");
  Serial.println("port at 1200 bps (what the IDE uploader does) for auto-DFU.");
}

void loop() {
  /* 1200-bps touch auto-DFU. */
  if (Serial.baud() == 1200) {
    Serial.println("1200 bps touch detected, rebooting to DFU...");
    Serial.flush();
    delay(50);
    jump_to_bootloader();
  }

  /* Manual trigger: type "DFU\n" in Serial Monitor. */
  if (Serial.available() >= 3) {
    if (Serial.read() == 'D' && Serial.read() == 'F' && Serial.read() == 'U') {
      Serial.println("DFU requested, rebooting...");
      Serial.flush();
      delay(100);
      jump_to_bootloader();
    }
  }

  /* Heartbeat every second so you see USB CDC is alive. */
  static uint32_t last = 0;
  if (millis() - last >= 1000) {
    last = millis();
    Serial.print("tick @ ");
    Serial.print(millis());
    Serial.println(" ms");
  }
}
