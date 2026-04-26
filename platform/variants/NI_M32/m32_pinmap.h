/*
 * m32_pinmap.h — high-level board maps for the NI Komplete Kontrol M32.
 * Included automatically when Arduino sketches target this variant.
 *
 * This is the single source of truth for:
 *   • LED index → semantic button name      (M32_LED_*)
 *   • scan-matrix (col, slot) → button name (M32_BTN_* + M32_KEY_TOP/BOT)
 *   • fixed protocol bytes / I2C addresses
 *
 * Raw pins live in variant_NI_M32.h.  See wiki/PERIPHERALS.md for protocol-
 * level wire traces.
 */

#pragma once
#include <stdint.h>

/*======================== LED driver — 21 logical LEDs ============================*/

enum M32_LEDIndex {
  M32_LED_SHIFT          =  0,
  M32_LED_SCALE_EDIT     =  1,
  M32_LED_ARP_EDIT       =  2,
  M32_LED_UNDO_REDO      =  3,
  M32_LED_QUANTIZE_AUTO  =  4,
  M32_LED_IDEAS          =  5,
  M32_LED_LOOP           =  6,
  M32_LED_METRO          =  7,
  M32_LED_TEMPO          =  8,
  M32_LED_PRESET_UP      =  9,
  M32_LED_PRESET_DOWN    = 10,
  M32_LED_PLAY_RESTART   = 11,
  M32_LED_REC_COUNTIN    = 12,
  M32_LED_STOP_CLEAR     = 13,
  M32_LED_LEFT_M         = 14,
  M32_LED_RIGHT_S        = 15,
  M32_LED_TRACK_INSTANCE = 16,
  M32_LED_PLUGIN_MIDI    = 17,
  M32_LED_BROWSER        = 18,
  M32_LED_OCT_DOWN       = 19,
  M32_LED_OCT_UP         = 20,
  M32_LED_NUM            = 21,
};

/* LED driver (QFN44 T47103236a) I2C protocol.
   Bitbanged on PIN_LED_SCL / PIN_LED_SDA, addr 0x3C.
   Init:
     write {0x4F, 0x00}
     write {0x00, 0x01}
     write {0x26, brightness[37]}   // first 21 bytes = 0x07, rest 0
     write {0x25, 0x01}             // update trigger
   Setting LEDs each frame:
     write {0x01, brightness[21]}
     write {0x25, 0x01}
*/
#define M32_LED_REG_CONFIG1       0x4F
#define M32_LED_REG_CONFIG2       0x00
#define M32_LED_REG_BRIGHTNESS    0x26
#define M32_LED_REG_SET           0x01
#define M32_LED_REG_TRIGGER       0x25
#define M32_LED_TRIGGER_VAL       0x01

/*======================== Scan matrix — 21 buttons + 32 keys × 2 =================*/
/* The matrix is organised as two 74HC138 chips (A, B) selected by PIN_MATRIX_SEL,
 * each exposing 8 slots (0..7) addressed via A0..A2.  PIN_MATRIX_COL0..7 (PC8..PC15)
 * read 8 columns back on each slot.
 *
 * "slot" below means the combined (chip_AB × slot03) address;  slots 0..2 carry
 * pulsanti, slots 8..15 carry tasti (top/bot pairs for velocity).
 */

struct M32_ButtonCoord { uint8_t col; uint8_t slot; };

enum M32_ButtonIndex {
  M32_BTN_SHIFT = 0,
  M32_BTN_SCALE_EDIT,
  M32_BTN_ARP_EDIT,
  M32_BTN_UNDO_REDO,
  M32_BTN_QUANTIZE_AUTO,
  M32_BTN_IDEAS,
  M32_BTN_LOOP,
  M32_BTN_METRO,
  M32_BTN_TEMPO,
  M32_BTN_PRESET_UP,
  M32_BTN_PRESET_DOWN,
  M32_BTN_PLAY_RESTART,
  M32_BTN_REC_COUNTIN,
  M32_BTN_STOP_CLEAR,
  M32_BTN_LEFT_M,
  M32_BTN_RIGHT_S,
  M32_BTN_TRACK_INSTANCE,
  M32_BTN_PLUGIN_MIDI,
  M32_BTN_BROWSER,
  M32_BTN_OCT_DOWN,
  M32_BTN_OCT_UP,
  M32_BTN_NUM,
};

/* (col, slot) for each logical button.  Indexable by M32_BTN_*. */
static const struct M32_ButtonCoord M32_BUTTON_COORDS[M32_BTN_NUM] = {
  /* SHIFT          */ {0, 0},
  /* SCALE_EDIT     */ {1, 0},
  /* ARP_EDIT       */ {2, 0},
  /* UNDO_REDO      */ {3, 0},
  /* QUANTIZE_AUTO  */ {4, 0},
  /* IDEAS          */ {5, 0},
  /* LOOP           */ {0, 2},
  /* METRO          */ {1, 2},
  /* TEMPO          */ {2, 2},
  /* PRESET_UP      */ {3, 1},
  /* PRESET_DOWN    */ {4, 1},
  /* PLAY_RESTART   */ {3, 2},
  /* REC_COUNTIN    */ {4, 2},
  /* STOP_CLEAR     */ {5, 2},
  /* LEFT_M         */ {6, 1},
  /* RIGHT_S        */ {7, 1},
  /* TRACK_INSTANCE */ {2, 1},
  /* PLUGIN_MIDI    */ {1, 1},
  /* BROWSER        */ {0, 1},
  /* OCT_DOWN       */ {7, 2},
  /* OCT_UP         */ {6, 2},
};

/*======================== Keys (32-key velocity-sensitive) ========================*/
/* Each key has a TOP and BOT contact; TOP closes first, BOT closes later; the
 * delta measures velocity.  Slots pairs (14/15, 12/13, 10/11, 8/9) cover the
 * four octave ranges, with col 7→0 being the 8 notes within each group.
 *
 * Key 0 = F2 (MIDI 41), key 31 = C5 (MIDI 72).
 */
#define M32_KEY_COUNT       32
#define M32_KEY_MIDI_BASE   41        /* MIDI note for key #0 (F2) */
#define M32_KEY_MIDI_LAST   (M32_KEY_MIDI_BASE + M32_KEY_COUNT - 1) /* 72 = C5 */

/* Returns {col, top_slot, bot_slot} for keyIdx in [0..31]. */
static inline void m32_keyCoord(uint8_t keyIdx, uint8_t *col,
                                uint8_t *slotTop, uint8_t *slotBot) {
  uint8_t group = keyIdx >> 3;          // 0..3
  uint8_t withinGroup = keyIdx & 7;     // 0..7
  /* groups 0..3 correspond to slot pairs (14/15), (12/13), (10/11), (8/9) */
  uint8_t topBase = (uint8_t)(14 - (group << 1));
  *col     = (uint8_t)(7 - withinGroup);
  *slotTop = topBase;
  *slotBot = (uint8_t)(topBase + 1);
}

/* Velocity mapping: delta_us (TOP→BOT) to MIDI velocity 1..127.
 *   500 µs   → 127 (hard strike)
 *   50000 µs → 1   (whisper)
 * Logarithmic interpolation works better in practice; this linear fallback is
 * what the discovery sketch used and ships as a baseline. */
static inline uint8_t m32_velocityFromDeltaUs(uint32_t dt_us) {
  if (dt_us <= 500)    return 127;
  if (dt_us >= 50000)  return 1;
  /* linear between those endpoints */
  uint32_t num = (50000 - dt_us) * 126;
  uint32_t den = (50000 - 500);
  return (uint8_t)(1 + (num / den));
}

/*======================== Touch strips (pitch & mod) ==============================*/
/* Two CSM224-5 controllers, one per strip, same I2C address on different buses.
 * Read protocol: write 0x00 → repeated-start → read 3 bytes
 *   [0] = touch count  (0 = idle, 1 = one finger, 2 = two fingers)
 *   [1] = position A   (0..0xDF for "full strip")
 *   [2] = position B   (valid when count == 2; a stray nonzero can be
 *                       observed briefly under high-pressure single touches).
 */
#define M32_TOUCH_STRIP_REG_DATA   0x00
#define M32_TOUCH_STRIP_READ_LEN   3
#define M32_TOUCH_STRIP_MAX_POS    0xDF

/*======================== Encoder-body touch sensor (BS83B08A-3) ==================*/
/* HW I2C2, addr 0x50.  Read protocol: write 0xA0, read 1 byte.
 * Returned byte is an 8-bit bitmap; bit N = encoder N currently touched.
 */
#define M32_TOUCH_ENC_REG          0xA0
#define M32_TOUCH_ENC_READ_LEN     1

/*======================== Joycoder (button re-mapping note) =======================*/
/* The silkscreen on the board is LEFT/UP/RIGHT/DOWN but the hardware wiring
 * was swapped; PIN_JOY_LEFT/RIGHT/UP/DOWN above reflect the hardware pins so
 * software should always go through the semantic names here. */
