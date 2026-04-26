#pragma once
#include <Arduino.h>

// Forza il clock di sistema a 72MHz usando HSE 8MHz × PLL 9
// Da chiamare prima di qualsiasi altra cosa in setup()
void clock_init_72mhz();
