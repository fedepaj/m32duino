/*
 * NI Komplete Kontrol M32 — board variant.
 *
 * Pin tables follow the canonical STM32duino F103R(8-B)T variant_generic.cpp
 * so all 16 ADC channels are addressable as PIN_A0..PIN_A15. On top of the
 * canonical layout we:
 *   • reset all peripherals before HAL_Init so the NI DFU bootloader's leftover
 *     state (USB, GPIOs, ...) doesn't leak into the application
 *   • free up JTAG pins (PB3/PB4/PA15) at boot — keeps SWD on for recovery
 *   • configure the 72 / 48 MHz clock tree based on the menu selection
 */

#include "pins_arduino.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Digital PinName array — index = Arduino "Dxx" pin, value = PinName enum. */
const PinName digitalPin[] = {
  PA_0,   // D0/A0
  PA_1,   // D1/A1
  PA_2,   // D2/A2
  PA_3,   // D3/A3
  PA_4,   // D4/A4
  PA_5,   // D5/A5
  PA_6,   // D6/A6
  PA_7,   // D7/A7
  PA_8,   // D8
  PA_9,   // D9
  PA_10,  // D10
  PA_11,  // D11
  PA_12,  // D12
  PA_13,  // D13  (SWDIO)
  PA_14,  // D14  (SWCLK)
  PA_15,  // D15
  PB_0,   // D16/A8
  PB_1,   // D17/A9
  PB_2,   // D18
  PB_3,   // D19  (was JTAG TDO; freed by initVariant())
  PB_4,   // D20  (was JTAG NJTRST; freed by initVariant())
  PB_5,   // D21
  PB_6,   // D22
  PB_7,   // D23
  PB_8,   // D24
  PB_9,   // D25
  PB_10,  // D26
  PB_11,  // D27
  PB_12,  // D28
  PB_13,  // D29
  PB_14,  // D30
  PB_15,  // D31
  PC_0,   // D32/A10
  PC_1,   // D33/A11
  PC_2,   // D34/A12
  PC_3,   // D35/A13
  PC_4,   // D36/A14
  PC_5,   // D37/A15
  PC_6,   // D38
  PC_7,   // D39
  PC_8,   // D40
  PC_9,   // D41
  PC_10,  // D42
  PC_11,  // D43
  PC_12,  // D44
  PC_13,  // D45
  PC_14,  // D46
  PC_15,  // D47
  PD_0,   // D48
  PD_1,   // D49
  PD_2    // D50
};

/* Analog (Ax) pin number array — index = Ax, value = digital pin number. */
const uint32_t analogInputPin[] = {
  0,   // A0,  PA0
  1,   // A1,  PA1
  2,   // A2,  PA2
  3,   // A3,  PA3
  4,   // A4,  PA4
  5,   // A5,  PA5
  6,   // A6,  PA6
  7,   // A7,  PA7
  16,  // A8,  PB0
  17,  // A9,  PB1
  32,  // A10, PC0
  33,  // A11, PC1
  34,  // A12, PC2
  35,  // A13, PC3
  36,  // A14, PC4
  37   // A15, PC5
};

#ifdef __cplusplus
}
#endif


/*
 * The NI DFU bootloader hands control to the application with USB, GPIOs and
 * potentially other peripherals already configured. Force a peripheral reset
 * before HAL_Init runs so HAL doesn't inherit the bootloader's state.
 *
 * constructor(100) runs before main.cpp's premain (priority 101), i.e. before
 * any HAL initialisation or static C++ object construction.
 */
__attribute__((constructor(100))) static void m32_peripheral_reset()
{
  RCC->APB1RSTR = 0xFFFFFFFFU; RCC->APB1RSTR = 0;
  RCC->APB2RSTR = 0xFFFFFFFFU; RCC->APB2RSTR = 0;
}

/*
 * Board init invoked by main() after HAL_Init() and before setup().
 *
 * Two responsibilities:
 *
 *  1. Free up JTAG pins (PB3 / PB4 / PA15) for use as GPIO / SPI1 — on the M32
 *     those carry OLED_SCK, OLED_DC and LED_RST. SWJ_CFG = 010 (NOJTAG)
 *     disables JTAG-DP but keeps SW-DP enabled, so PA13 (SWDIO) and PA14
 *     (SWCLK) remain dedicated to SWD: the board is always recoverable with
 *     any SWD probe (ST-Link, J-Link, Black Magic, ...).
 *
 *  2. When the user selects a USB-class menu that enables CDC, drive the
 *     external USB D+ pull-up (PA8) low briefly and then high so the host
 *     re-enumerates the freshly-attached CDC device. The bootloader hands
 *     control over with PA8 floating; without this toggle the host doesn't
 *     notice the new device. Sketches don't have to do anything to get USB
 *     CDC working.
 */
extern "C" void initVariant()
{
  __HAL_RCC_AFIO_CLK_ENABLE();
  __HAL_AFIO_REMAP_SWJ_NOJTAG();

#if defined(USBCON) && defined(USBD_USE_CDC)
  __HAL_RCC_GPIOA_CLK_ENABLE();
  GPIO_InitTypeDef u = {};
  u.Pin   = GPIO_PIN_8;
  u.Mode  = GPIO_MODE_OUTPUT_PP;
  u.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &u);
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, GPIO_PIN_RESET);
  HAL_Delay(50);
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, GPIO_PIN_SET);
#endif
}

/*
 * System Clock Configuration
 *
 * Selectable via the Tools → Clock menu:
 *   • M32_CLOCK_72MHZ (default): HSE 8 → PLL ×9 → SYSCLK 72, USB div 1.5 → 48
 *   • M32_CLOCK_48MHZ:           HSE 8 → PLL ×6 → SYSCLK 48, USB div 1.0 → 48
 *
 * Common: HCLK = SYSCLK, APB1 = HCLK/2, APB2 = HCLK/1, FLASH wait states tuned
 * to the chosen SYSCLK (2 ws @72, 1 ws @48).
 */
extern "C" WEAK void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {};

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState       = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.PLL.PLLState   = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
#if defined(M32_CLOCK_48MHZ)
  RCC_OscInitStruct.PLL.PLLMUL     = RCC_PLL_MUL6;
#else  /* default: 72 MHz */
  RCC_OscInitStruct.PLL.PLLMUL     = RCC_PLL_MUL9;
#endif
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) Error_Handler();

  RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                   | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
#if defined(M32_CLOCK_48MHZ)
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK) Error_Handler();
#else
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK) Error_Handler();
#endif

  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USB;
#if defined(M32_CLOCK_48MHZ)
  PeriphClkInit.UsbClockSelection    = RCC_USBCLKSOURCE_PLL;          /* 48 / 1   = 48 MHz */
#else
  PeriphClkInit.UsbClockSelection    = RCC_USBCLKSOURCE_PLL_DIV1_5;   /* 72 / 1.5 = 48 MHz */
#endif
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK) Error_Handler();
}
