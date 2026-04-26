#include "led.h"

#define LED_I2C_ADDR 0x3C
#define LED_RESET_PIN PA15

#define SCL_PIN 6
#define SDA_PIN 7
#define SCL_BIT (1 << SCL_PIN)
#define SDA_BIT (1 << SDA_PIN)

static uint8_t ledState[21] = {0};
static bool ledDirty = false;

static inline void scl_set_od() {
  GPIOB->CRL = (GPIOB->CRL & ~(0xF << (SCL_PIN*4))) | (0x6 << (SCL_PIN*4));
}
static inline void sda_set_od() {
  GPIOB->CRL = (GPIOB->CRL & ~(0xF << (SDA_PIN*4))) | (0x6 << (SDA_PIN*4));
}
static inline void scl_h() { GPIOB->BSRR = SCL_BIT; }
static inline void scl_l() { GPIOB->BSRR = SCL_BIT << 16; }
static inline void sda_h() { GPIOB->BSRR = SDA_BIT; }
static inline void sda_l() { GPIOB->BSRR = SDA_BIT << 16; }
static inline bool sda_r() { return (GPIOB->IDR & SDA_BIT) != 0; }
static inline void d() { for (volatile int i = 0; i < 10; i++) __NOP(); }

static void i2c_start() { sda_h(); scl_h(); d(); sda_l(); d(); scl_l(); d(); }
static void i2c_stop()  { sda_l(); d(); scl_h(); d(); sda_h(); d(); }
static bool i2c_writeByte(uint8_t b) {
  for (int i = 7; i >= 0; i--) {
    if (b & (1 << i)) sda_h(); else sda_l();
    d(); scl_h(); d(); scl_l(); d();
  }
  sda_h(); d(); scl_h(); d();
  bool ack = !sda_r();
  scl_l(); d();
  return ack;
}
static void i2c_writeBuf(uint8_t addr, const uint8_t* data, size_t len) {
  i2c_start(); i2c_writeByte(addr << 1);
  for (size_t i = 0; i < len; i++) i2c_writeByte(data[i]);
  i2c_stop();
}

void led_init() {
  pinMode(LED_RESET_PIN, OUTPUT);
  digitalWrite(LED_RESET_PIN, LOW); delay(10);
  digitalWrite(LED_RESET_PIN, HIGH); delay(10);
  
  scl_h(); sda_h();
  scl_set_od(); sda_set_od();
  
  uint8_t cmd1[] = {0x4F, 0x00}; i2c_writeBuf(LED_I2C_ADDR, cmd1, 2);
  uint8_t cmd2[] = {0x00, 0x01}; i2c_writeBuf(LED_I2C_ADDR, cmd2, 2);
  uint8_t br[38]; br[0] = 0x26;
  for (int i = 0; i < 21; i++) br[1+i] = 0x07;
  for (int i = 21; i < 37; i++) br[1+i] = 0x00;
  i2c_writeBuf(LED_I2C_ADDR, br, 38);
  uint8_t cmd3[] = {0x25, 0x01}; i2c_writeBuf(LED_I2C_ADDR, cmd3, 2);
}

void led_set(uint8_t idx, uint8_t v) {
  if (idx < 21 && ledState[idx] != v) {
    ledState[idx] = v;
    ledDirty = true;
  }
}

void led_flush_if_dirty() {
  if (!ledDirty) return;
  ledDirty = false;
  uint8_t buf[22]; buf[0] = 0x01;
  memcpy(buf + 1, ledState, 21);
  i2c_writeBuf(LED_I2C_ADDR, buf, 22);
  uint8_t upd[] = {0x25, 0x01}; i2c_writeBuf(LED_I2C_ADDR, upd, 2);
}
