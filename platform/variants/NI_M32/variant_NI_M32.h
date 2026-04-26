/*
 * NI Komplete Kontrol M32 — full board variant header.
 *
 * Pin numbering and Arduino-core plumbing follow STM32duino's canonical
 * F103R(8-B)T variant_generic.h (so analogRead, Wire, SPI defaults all
 * resolve correctly). On top of that we add semantic aliases tied to the
 * wiring reverse-engineered from a factory board (PIN_OLED_*, PIN_MATRIX_*,
 * etc.); see wiki/PERIPHERALS.md for protocol details and m32_pinmap.h for
 * the high-level software maps (LED indices, scan-matrix → key, ...).
 */

#pragma once

/*================================ Pin numbering (canonical) ========================
 * PIN_A0..PIN_A15 are core macros that mark these pins as analog-capable; the
 * core uses them to look up the ADC channel via analogInputPin[]. Plain numbers
 * (PA8..PD2) are digital-only.
 */
#define PA0                     PIN_A0
#define PA1                     PIN_A1
#define PA2                     PIN_A2
#define PA3                     PIN_A3
#define PA4                     PIN_A4
#define PA5                     PIN_A5
#define PA6                     PIN_A6
#define PA7                     PIN_A7
#define PA8                     8
#define PA9                     9
#define PA10                    10
#define PA11                    11
#define PA12                    12
#define PA13                    13
#define PA14                    14
#define PA15                    15
#define PB0                     PIN_A8
#define PB1                     PIN_A9
#define PB2                     18
#define PB3                     19
#define PB4                     20
#define PB5                     21
#define PB6                     22
#define PB7                     23
#define PB8                     24
#define PB9                     25
#define PB10                    26
#define PB11                    27
#define PB12                    28
#define PB13                    29
#define PB14                    30
#define PB15                    31
#define PC0                     PIN_A10
#define PC1                     PIN_A11
#define PC2                     PIN_A12
#define PC3                     PIN_A13
#define PC4                     PIN_A14
#define PC5                     PIN_A15
#define PC6                     38
#define PC7                     39
#define PC8                     40
#define PC9                     41
#define PC10                    42
#define PC11                    43
#define PC12                    44
#define PC13                    45
#define PC14                    46
#define PC15                    47
#define PD0                     48
#define PD1                     49
#define PD2                     50

#define NUM_DIGITAL_PINS        51
#define NUM_ANALOG_INPUTS       16

/*================================ Default peripheral pin assignments ===============
 * These are STM32duino-canonical defaults (e.g. Wire() with no args uses
 * PIN_WIRE_SDA/SCL). Override in the sketch if your wiring differs.
 */

/* SPI1 default pins (PA5/PA6/PA7) */
#ifndef PIN_SPI_SS
  #define PIN_SPI_SS            PA4
#endif
#ifndef PIN_SPI_MOSI
  #define PIN_SPI_MOSI          PA7
#endif
#ifndef PIN_SPI_MISO
  #define PIN_SPI_MISO          PA6
#endif
#ifndef PIN_SPI_SCK
  #define PIN_SPI_SCK           PA5
#endif

/* I2C1 default pins (PB6/PB7) */
#ifndef PIN_WIRE_SDA
  #define PIN_WIRE_SDA          PB7
#endif
#ifndef PIN_WIRE_SCL
  #define PIN_WIRE_SCL          PB6
#endif

/* Tone / Servo timers */
#ifndef TIMER_TONE
  #define TIMER_TONE            TIM3
#endif
#ifndef TIMER_SERVO
  #define TIMER_SERVO           TIM4
#endif

/* HardwareSerial default = USART1 (PA9 TX, PA10 RX) — this is also the M32's
 * UART exposed on PIN_MOD_RST/PIN_PITCH_RST, so keep in mind if you use it. */
#ifndef SERIAL_UART_INSTANCE
  #define SERIAL_UART_INSTANCE  1
#endif
#ifndef PIN_SERIAL_RX
  #define PIN_SERIAL_RX         PA10
#endif
#ifndef PIN_SERIAL_TX
  #define PIN_SERIAL_TX         PA9
#endif

#ifdef __cplusplus
  #ifndef SERIAL_PORT_MONITOR
    #define SERIAL_PORT_MONITOR   Serial
  #endif
  #ifndef SERIAL_PORT_HARDWARE
    #define SERIAL_PORT_HARDWARE  Serial
  #endif
#endif

/*================================ Semantic peripheral pins ========================*/

/* --- SWD (do not reuse as GPIO without JTAG disable; initVariant() in the .cpp
       already calls __HAL_AFIO_REMAP_SWJ_NOJTAG for us) --- */
#define PIN_SWDIO           PA13
#define PIN_SWDCLK          PA14

/* --- USB full-speed device --- */
#define PIN_USB_DM          PA11
#define PIN_USB_DP          PA12
#define PIN_USB_PULLUP      PA8        /* drive HIGH to connect; LOW for re-enum */

/* --- Audio output (sigma-delta via TIM4+DMA; optional, see examples) --- */
#define PIN_AUDIO_L         PC4
#define PIN_AUDIO_R         PC5

/* --- OLED SSD1306 128×32, SPI1 remapped (PB3/PB4/PB5) --- */
#define PIN_OLED_SCK        PB3
#define PIN_OLED_MOSI       PB5
#define PIN_OLED_CS         PB15
#define PIN_OLED_DC         PB4
#define PIN_OLED_RST        PD2

/* --- LED driver (custom QFN44, I2C bit-banged on PB6/PB7 due to F103 errata:
       HW I2C1 + SPI1 remap conflict) --- */
#define PIN_LED_SCL         PB6
#define PIN_LED_SDA         PB7
#define PIN_LED_RST         PA15       /* active LOW */
#define M32_LED_I2C_ADDR    0x3C       /* 7-bit */
#define M32_LED_COUNT       21

/* --- Key/button scan matrix: 2× 74HC138 selected by PB14 (A/B), with
       PB0..PB2 as A0..A2, column returns on PC8..PC15 (8 bits) --- */
#define PIN_MATRIX_A0       PB0
#define PIN_MATRIX_A1       PB1
#define PIN_MATRIX_A2       PB2
#define PIN_MATRIX_SEL      PB14       /* selects 74HC138 chip A (LOW) or B (HIGH) */
#define PIN_MATRIX_COL0     PC8
#define PIN_MATRIX_COL1     PC9
#define PIN_MATRIX_COL2     PC10
#define PIN_MATRIX_COL3     PC11
#define PIN_MATRIX_COL4     PC12
#define PIN_MATRIX_COL5     PC13
#define PIN_MATRIX_COL6     PC14
#define PIN_MATRIX_COL7     PC15

/* --- 8 rotary-encoder "analog sin/cos" group (2× 74HC4052 muxes) --- */
#define PIN_MUX_SEL_A       PB8
#define PIN_MUX_SEL_B       PB9
#define PIN_ADC_X_A         PA4        /* group 1 sin */
#define PIN_ADC_Y_A         PA5        /* group 1 cos */
#define PIN_ADC_X_B         PA6        /* group 2 sin */
#define PIN_ADC_Y_B         PA7        /* group 2 cos */

/* --- Encoder-body touch sensor (Holtek BS83B08A-3, HW I2C2 on PB10/PB11) --- */
#define PIN_TOUCH_ENC_SCL   PB10
#define PIN_TOUCH_ENC_SDA   PB11
#define M32_TOUCH_ENC_ADDR  0x50       /* 7-bit */

/* --- Pitch touch strip (CSM224-5, bit-banged I2C on PC0/PC1) --- */
#define PIN_PITCH_SDA       PC0
#define PIN_PITCH_SCL       PC1
#define PIN_PITCH_RST       PA10       /* active LOW → HIGH for operating */

/* --- Mod touch strip (CSM224-5, bit-banged I2C on PC2/PC3) --- */
#define PIN_MOD_SDA         PC2
#define PIN_MOD_SCL         PC3
#define PIN_MOD_RST         PA9        /* active LOW → HIGH for operating */

#define M32_TOUCH_STRIP_ADDR 0x15      /* shared 7-bit addr; distinct buses */

/* --- Joycoder (thumbstick + rotary + click) --- */
#define PIN_JOY_ENC_A       PC6        /* TIM3_CH1 hardware quadrature */
#define PIN_JOY_ENC_B       PC7        /* TIM3_CH2 */
#define PIN_JOY_BTN         PB13
#define PIN_JOY_LEFT        PA0
#define PIN_JOY_UP          PA1
#define PIN_JOY_RIGHT       PA2
#define PIN_JOY_DOWN        PA3

/*================================ USB identity ====================================*/

/* Stay on the NI DFU PID during bring-up so the host path is unambiguous for
   the uploader. Change these once you have your own USB ID. */
#define USBCON
#define USBD_VID                0x17CC
#define USBD_PID                0x1862
#define USB_MANUFACTURER_STRING "Komplete Kontrol M32 (custom)"
#define USB_PRODUCT_STRING      "M32 Custom FW"

/*================================ Clock tree =====================================
 * SYSCLK is selectable from the Tools → Clock menu (defaults to 72 MHz). USB
 * is always 48 MHz: PLL ×9 → 72 MHz / 1.5 (M32_CLOCK_72MHZ); PLL ×6 → 48 MHz
 * with USB div 1.0 (M32_CLOCK_48MHZ).
 */
#define HSE_VALUE               8000000U

#ifdef __cplusplus
extern "C" {
void SystemClock_Config(void);
}
#endif
