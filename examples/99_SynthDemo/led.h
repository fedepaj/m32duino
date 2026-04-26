#pragma once
#include <Arduino.h>

void led_init();
void led_set(uint8_t idx, uint8_t value);
void led_flush_if_dirty();   // chiamato dal main loop
