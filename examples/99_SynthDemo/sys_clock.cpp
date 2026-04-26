#include "sys_clock.h"

void clock_init_72mhz() {
  // Abilita HSE
  RCC->CR |= RCC_CR_HSEON;
  while (!(RCC->CR & RCC_CR_HSERDY)) {}
  
  // Configura Flash latency: 2 wait states a 72MHz
  FLASH->ACR = FLASH_ACR_LATENCY_2 | FLASH_ACR_PRFTBE;
  
  // Configura prescaler:
  // AHB = SYSCLK / 1 = 72MHz
  // APB1 = SYSCLK / 2 = 36MHz (max 36MHz, timer × 2 = 72MHz)
  // APB2 = SYSCLK / 1 = 72MHz
  // ADC = APB2 / 6 = 12MHz (max 14MHz)
  RCC->CFGR = RCC_CFGR_HPRE_DIV1
            | RCC_CFGR_PPRE1_DIV2
            | RCC_CFGR_PPRE2_DIV1
            | RCC_CFGR_ADCPRE_DIV6
            | RCC_CFGR_PLLSRC      // HSE come sorgente PLL
            | RCC_CFGR_PLLMULL9;   // ×9 → 8 × 9 = 72MHz
  
  // Abilita PLL
  RCC->CR |= RCC_CR_PLLON;
  while (!(RCC->CR & RCC_CR_PLLRDY)) {}
  
  // Switch su PLL come system clock
  RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
  while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {}
  
  // Aggiorna SystemCoreClock
  SystemCoreClock = 72000000;
}
