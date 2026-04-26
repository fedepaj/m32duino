#include "touch.h"
#include "events.h"
#include <Wire.h>

#define TOUCH_ENC_ADDR 0x50
#define TS_ADDR 0x15

#define RESET_PITCH PA9
#define RESET_MOD PA10

static TwoWire Wire2(2);

// Touch encoder state
static uint8_t lastTouchEncMask = 0;
static uint32_t lastEncPoll = 0;
#define ENC_POLL_MS 30

// Touch strip state
static uint32_t lastStripPoll = 0;
#define STRIP_POLL_MS 20

// =========== STRIP I2C BITBANG (replicato dal driver finale) ===========
struct StripBus { uint8_t scl_pin, sda_pin; };  // PC pin index

static inline void pinOD_pc(uint8_t pin) {
  GPIOC->CRL = (GPIOC->CRL & ~(0xF << (pin*4))) | (0x6 << (pin*4));
}
static inline void pin_hi_pc(uint8_t pin) { GPIOC->BSRR = (1 << pin); }
static inline void pin_lo_pc(uint8_t pin) { GPIOC->BSRR = (1 << pin) << 16; }
static inline bool pin_rd_pc(uint8_t pin) { return (GPIOC->IDR & (1 << pin)) != 0; }
static inline void i2c_d() { for (volatile int i = 0; i < 30; i++) __NOP(); }

static void bus_start(StripBus& b) { pin_hi_pc(b.sda_pin); pin_hi_pc(b.scl_pin); i2c_d(); pin_lo_pc(b.sda_pin); i2c_d(); pin_lo_pc(b.scl_pin); i2c_d(); }
static void bus_stop(StripBus& b)  { pin_lo_pc(b.sda_pin); i2c_d(); pin_hi_pc(b.scl_pin); i2c_d(); pin_hi_pc(b.sda_pin); i2c_d(); }

static bool bus_writeByte(StripBus& b, uint8_t v) {
  for (int i = 7; i >= 0; i--) {
    if (v & (1 << i)) pin_hi_pc(b.sda_pin); else pin_lo_pc(b.sda_pin);
    i2c_d(); pin_hi_pc(b.scl_pin); i2c_d(); pin_lo_pc(b.scl_pin); i2c_d();
  }
  pin_hi_pc(b.sda_pin); i2c_d(); pin_hi_pc(b.scl_pin); i2c_d();
  bool ack = !pin_rd_pc(b.sda_pin);
  pin_lo_pc(b.scl_pin); i2c_d();
  return ack;
}

static uint8_t bus_readByte(StripBus& b, bool ack) {
  pin_hi_pc(b.sda_pin);
  uint8_t v = 0;
  for (int i = 7; i >= 0; i--) {
    i2c_d(); pin_hi_pc(b.scl_pin); i2c_d();
    if (pin_rd_pc(b.sda_pin)) v |= (1 << i);
    pin_lo_pc(b.scl_pin); i2c_d();
  }
  if (ack) pin_lo_pc(b.sda_pin); else pin_hi_pc(b.sda_pin);
  i2c_d(); pin_hi_pc(b.scl_pin); i2c_d(); pin_lo_pc(b.scl_pin); i2c_d();
  return v;
}

static bool readStrip(StripBus& b, uint8_t* out) {
  bus_start(b);
  if (!bus_writeByte(b, TS_ADDR << 1)) { bus_stop(b); return false; }
  if (!bus_writeByte(b, 0x00)) { bus_stop(b); return false; }
  bus_start(b);
  if (!bus_writeByte(b, (TS_ADDR << 1) | 1)) { bus_stop(b); return false; }
  out[0] = bus_readByte(b, true);
  out[1] = bus_readByte(b, true);
  out[2] = bus_readByte(b, false);
  bus_stop(b);
  return true;
}

static StripBus pitchBus = {1, 0};  // SCL=PC1, SDA=PC0
static StripBus modBus   = {3, 2};  // SCL=PC3, SDA=PC2

static bool pitchTouching = false;
static bool modTouching = false;

void touch_init() {
  // I2C2 hardware per encoder bodies
  Wire2.setSDA(PB11);
  Wire2.setSCL(PB10);
  Wire2.begin();
  Wire2.setClock(400000);
  
  // Reset touch strip chips
  pinMode(RESET_PITCH, OUTPUT);
  pinMode(RESET_MOD, OUTPUT);
  digitalWrite(RESET_PITCH, HIGH); digitalWrite(RESET_MOD, HIGH);
  delay(10);
  digitalWrite(RESET_PITCH, LOW); digitalWrite(RESET_MOD, LOW);
  delay(50);
  digitalWrite(RESET_PITCH, HIGH); digitalWrite(RESET_MOD, HIGH);
  delay(200);
  
  // Init bitbang strip buses (open-drain, idle high)
  pin_hi_pc(0); pin_hi_pc(1); pinOD_pc(0); pinOD_pc(1);
  pin_hi_pc(2); pin_hi_pc(3); pinOD_pc(2); pinOD_pc(3);
}

void touch_poll(uint32_t now) {
  // Touch encoder
  if (now - lastEncPoll >= ENC_POLL_MS) {
    lastEncPoll = now;
    Wire2.beginTransmission(TOUCH_ENC_ADDR);
    Wire2.write(0xA0);
    if (Wire2.endTransmission(false) == 0) {
      Wire2.requestFrom(TOUCH_ENC_ADDR, (uint8_t)1);
      if (Wire2.available()) {
        uint8_t curr = Wire2.read();
        uint8_t changed = curr ^ lastTouchEncMask;
        if (changed) {
          for (int i = 0; i < 8; i++) {
            if (changed & (1 << i)) {
              bool on = (curr & (1 << i)) != 0;
              pushEvent(on ? EVT_TOUCH_ENC_ON : EVT_TOUCH_ENC_OFF, i, 0);
            }
          }
          lastTouchEncMask = curr;
        }
      }
    }
  }
  
  // Touch strip
  if (now - lastStripPoll >= STRIP_POLL_MS) {
    lastStripPoll = now;
    uint8_t p[3], m[3];
    if (readStrip(pitchBus, p)) {
      bool t = (p[0] != 0);
      if (t) {
        pitchTouching = true;
        pushEvent(EVT_TOUCH_PITCH, 0, p[1]);
      } else if (pitchTouching) {
        pitchTouching = false;
        pushEvent(EVT_TOUCH_PITCH_OFF, 0, 0);
      }
    }
    if (readStrip(modBus, m)) {
      bool t = (m[0] != 0);
      if (t) {
        modTouching = true;
        pushEvent(EVT_TOUCH_MOD, 0, m[1]);
      } else if (modTouching) {
        modTouching = false;
        pushEvent(EVT_TOUCH_MOD_OFF, 0, 0);
      }
    }
  }
}
