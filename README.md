# m32duino

Arduino board package for the **Native Instruments Komplete Kontrol M32**.

The M32 is an STM32F103RBT6 underneath: 32 velocity-sensing keys, 8 endless encoders with capacitive bodies, two touch strips, an OLED, 21 backlit buttons. Out of the box it only talks to Komplete Kontrol — close that and the device sits there dark. This repo lets you flash arbitrary firmware on it from the Arduino IDE without ever touching the NI bootloader, so you can always roll back to stock.

> Not affiliated with Native Instruments. Reverse-engineered on a board I own. The only NI binary in this repo is the 51 KB factory app image, redistributed for restore-only interoperability.

---

## Install

1. Arduino IDE → **Preferences → Additional Boards Manager URLs**, add:
   ```
   https://raw.githubusercontent.com/fedepaj/m32duino/main/package_m32_index.json
   ```
2. **Tools → Board → Boards Manager** → search `M32` → install **NI Komplete Kontrol M32**.
3. **Tools → Board →** *NI Komplete Kontrol M32*. Recommended: **USB support: CDC (generic 'Serial' supersede U(S)ART)**, **System clock: 72 MHz**.
4. Forced-DFU combo (unplug, hold Shift+combo, plug while holding), then `File → Examples → M32_Examples → 00_HelloUSB` → **Upload**.

Every upload needs the manual combo: the NI bootloader has no software-trigger DFU entry (no Leonardo-style 1200-bps touch). Hold the combo, replug, click Upload.

To roll back to stock NI firmware: **Tools → Programmer → "Restore NI Factory Firmware"** → forced-DFU combo → **Tools → Burn Bootloader**. The 12 KB bootloader at `0x08000000` is never written to either way.

---

## Hardware (STM32F103RBT6, LQFP64)

| Peripheral                     | Pins                                                     | Notes                                                                          |
|--------------------------------|----------------------------------------------------------|--------------------------------------------------------------------------------|
| 32 velocity-sensitive keys     | PB0..PB2, PB14, PC8..PC15                                | 2× 74HC138 select × 8 column return, TOP/BOT dual contacts                     |
| 21 backlit buttons             | same matrix, slots 0..2                                  | semantic indices `M32_BTN_*` in `m32_pinmap.h`                                 |
| 21-LED driver                  | PB6 SCL, PB7 SDA (bit-bang), PA15 RST                    | custom QFN44 @ I²C 0x3C — bitbanged due to F103 I2C1⇔SPI1-remap errata         |
| 8 endless sin/cos encoders     | PA4..PA7 ADC + PB8/PB9 mux                               | 2× 74HC4052, `atan2(sin, cos)` → delta                                         |
| 8 encoder-body touch           | PB10 SCL, PB11 SDA (HW I²C2)                             | Holtek BS83B08A-3 @ 0x50, 8-bit bitmap                                         |
| Pitch / Mod touch strips       | PC0/PC1 + PC2/PC3 (bit-bang), PA10/PA9 RST               | CSM224-5 @ 0x15, multi-touch                                                   |
| OLED SSD1306 128×32            | PB3 SCK, PB5 MOSI (SPI1 remap), PB15 CS, PB4 DC, PD2 RST | works with U8g2 out of the box                                                 |
| Joycoder                       | PC6/PC7 TIM3 quadrature, PA0..PA3 4-way, PB13 click      | silkscreen labels swapped — use `PIN_JOY_*`                                    |
| Audio L/R                      | PC4, PC5                                                 | sigma-delta via TIM4+DMA, 352 kHz carrier, zero CPU                            |
| NI bootloader (DFU)            | `0x08000000..0x08003000`                                 | USB ID `0x17CC:0x1862` in DFU mode — never overwritten                         |

Full wire-level documentation in the [Wiki › Peripherals](https://github.com/fedepaj/m32duino/wiki/PERIPHERALS).

---

## Examples

Each one drives one peripheral; flash them in order to bring up the board piece by piece. Last one is a real mono synth.

| Example                  | What it does                                                       |
|--------------------------|--------------------------------------------------------------------|
| `00_HelloUSB`            | USB CDC heartbeat — the smoke test                                 |
| `01_USBSerial`           | minimal USB Serial echo                                            |
| `10_ButtonsMapped`       | read the 21 buttons by semantic name                               |
| `11_Encoders`            | 8 endless encoders via muxed ADC                                   |
| `12_Joycoder`            | thumbstick + rotary with TIM3 hardware quadrature                  |
| `13_ScanMatrix`          | raw 74HC138×2 scan                                                 |
| `14_ScanMatrixDiscover`  | print `(col, slot)` for each press — handy for re-mapping          |
| `15_LEDDriverTest`       | drive the 21 LEDs                                                  |
| `16_LEDDriverDiscover`   | which register / bit lights which LED                              |
| `17_OLED`                | SSD1306 128×32 init + drawing                                      |
| `18_TouchStrips`         | bit-banged I²C of the CSM224-5 controllers                         |
| `19_TouchEncoder`        | HW I²C2 of the Holtek body-touch                                   |
| `20_PedalJack`           | spare GPIO as sustain-pedal input                                  |
| `21_ArduinoPortBaseline` | the original "everything-in-one" bring-up sketch                   |
| `99_SynthDemo`           | mono synth: audio DMA + envelopes + LFO + OLED UI + touch routing  |

---

## How it works

```
0x08000000 ┌────────────────┐
           │ NI Bootloader  │  12 KB  ← USB DFU lives here, never touched
0x08003000 ├────────────────┤
           │ Your firmware  │ 116 KB  ← linked & flashed here
0x08020000 └────────────────┘
```

Uploads talk to the NI bootloader over plain USB DFU 1.x. Our variant links sketches at `0x08003000` with `VECT_TAB_OFFSET=0x3000`. The uploader is `m32_flash` — a ~300 KB Rust binary, libusb static, no runtime deps. It validates the `.bin` (refuses anything that would land outside the app partition), waits for the device in DFU, and downloads with `override_address = 0x08003000` so the bootloader region is safe even if you ask DfuSe to write earlier.

The reverse-engineering story (decompiling KKMUpdater, dumping the factory firmware over SWD, mapping each chip with a logic analyser) is in the [Wiki › Reverse Notes](https://github.com/fedepaj/m32duino/wiki/reference/REVERSE_NOTES).

---

## Dev

Requirements: Rust toolchain (`rustup`), Python 3, Arduino IDE 2.x. Nothing else.

```bash
./scripts/m32_dev.py up   # cargo build + assemble platform/tool tarballs +
                          # local HTTP server on :8765 + arduino-cli refresh
```

In Arduino IDE → Boards Manager → Remove + Install. Iterate. Other sub-commands: `package` (no serve), `serve` (no build), `stop`, `status`, `ci` (trigger the GitHub workflow without pushing a tag).

**Release** = push a tag:

```bash
git tag v0.0.2 && git push --tags
```

CI cross-builds `m32_flash` on `macos-14` (native arm64 + cross-compiled x86_64), `ubuntu-latest`, and `windows-latest`, packages everything, and creates a GitHub Release. The committed `package_m32_index.json` on `main` is what users actually fetch — the URL never changes.

### Repo layout

```
m32duino/
├── platform/
│   ├── boards.txt                  NI M32 board definition (USB / clock / opt menus)
│   ├── programmers.txt             "Restore NI Factory Firmware" entry
│   └── variants/NI_M32/            linker @ 0x08003000, pinmap, semantic aliases
├── tools/m32_flasher/              Rust crate (m32_flash binary) + factory firmware
├── examples/…                      shipped as the M32_Examples library at install time
├── wiki/…                          rendered into the GitHub Wiki
├── scripts/m32_dev.py              one-stop dev tool (build / serve / ci)
├── templates/platform.patch        recipe applied on top of STM32duino's platform.txt
├── package_m32_index.template.json source for the Boards Manager index
└── .github/workflows/release.yml   CI: matrix Rust build + package + Release
```

---

## Credits & licence

- Platform tarball is a slim fork of [stm32duino/Arduino_Core_STM32](https://github.com/stm32duino/Arduino_Core_STM32) (Apache-2.0), pinned to v2.7.1. Big thanks to that team.
- `m32_flash` is built on [`rusb`](https://crates.io/crates/rusb) + [`dfu-libusb`](https://crates.io/crates/dfu-libusb) + statically-linked `libusb-1.0` (LGPL-2.1).
- The bundled NI factory firmware (`m32_ni_factory.bin`, 51 148 B) is redistributed verbatim for interoperability with hardware users already own.
- Everything original here is MIT-licensed. See [`LICENSE`](LICENSE).
