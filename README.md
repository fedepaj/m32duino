# m32duino

Arduino IDE support for **Native Instruments Komplete Kontrol M32**, the way the vendor never gave you.

![Ops I did it again](https://media1.tenor.com/m/z6DCAAAAC/britney-spears-oops-i-did-it-again.gif)

Native Instruments ships a signed macOS updater, a USB DFU bootloader locked behind a button combo, and no hint of an SDK. The M32's hardware is a beautifully routed STM32F103RBT6 board with 32 velocity-sensing keys, 8 endless encoders with capacitive touch bodies, two multi-touch strips, a small OLED and 21 backlit pulsanti — all left dark the moment Komplete Kontrol isn't running. This repo turns it into a blank-slate Arduino board **while preserving the NI bootloader**, so you can flash any firmware you want and still roll back to factory in one click.

> Not affiliated with Native Instruments. All hardware reversing was done on a device I own; nothing here is NI copyrighted material beyond the 51 KB app-partition image that gets bundled as a `restore-factory` asset (redistributed for interoperability with hardware the user already owns).

---

## Install (Arduino IDE)

**1.** Arduino IDE → **Preferences → Additional Boards Manager URLs** → add:

```
https://raw.githubusercontent.com/fedepaj/m32duino/main/package_m32_index.json
```

**2.** **Tools → Board → Boards Manager** → search `M32` → install *"NI Komplete Kontrol M32"*.

**3.** **Tools → Board →** *"NI Komplete Kontrol M32"*. USB support: **CDC (generic Serial supersede U(n)ART)**.

**4.** Put the M32 in forced DFU (power-on holding Shift + your combo), open `File → Examples → M32_Examples → 00_HelloUSB`, click **Upload**. Done.

Subsequent uploads also need the forced-DFU button combo — the M32's bootloader doesn't expose a software-triggered DFU entry, so there's no Leonardo-style "1200 bps touch" we can wire up. Hold Shift+combo, replug, click Upload.

To roll back to the NI factory firmware: **Tools → Programmer → "Restore NI Factory Firmware"** → manual DFU entry → **Tools → Burn Bootloader**. The 12 KB bootloader at `0x08000000` is never written to by either flow.

---

## What you can do with this

The variant maps every GPIO to a semantic `PIN_*` alias and ships a `m32_pinmap.h` with the real peripheral layout — LED indices, scan-matrix coordinates, I²C addresses and protocol bytes for the LED driver, the touch strips, and the encoder-body touch chip. See [`wiki/PERIPHERALS.md`](wiki/PERIPHERALS.md) for the full wire-level story. Thirteen `examples/` sketches take you one peripheral at a time, culminating in a mono synth (`99_SynthDemo/`) that drives keyboard + encoders + LEDs + OLED + touch strips + sigma-delta audio on PC4/PC5.

### Hardware surface (STM32F103RBT6, LQFP64)

| Peripheral                     | Pins                                 | Notes                                                        |
|--------------------------------|--------------------------------------|--------------------------------------------------------------|
| 32 velocity-sensitive keys     | PB0..PB2, PB14, PC8..PC15            | 2× 74HC138 select × 8 return columns, TOP/BOT dual-contact   |
| 21 backlit pulsanti            | same matrix, slots 0..2              | semantic indices `M32_BTN_*` → coordinates in `m32_pinmap.h` |
| 21 LED driver                  | PB6 SCL, PB7 SDA (bit-banged), PA15 RST | custom QFN44 @ I²C 0x3C — F103 I2C1⇔SPI1-remap errata forces bitbang |
| 8 endless sin/cos encoders     | PA4..PA7 ADC + PB8/PB9 mux select    | 2× 74HC4052 dual 4:1 mux, `atan2` → delta                    |
| 8 encoder-body touch           | PB10 SCL, PB11 SDA (HW I²C2)         | Holtek BS83B08A-3 @ 0x50, 8-bit bitmap                       |
| Pitch touch strip              | PC0 SDA, PC1 SCL (bit-banged), PA10 RST | CSM224-5 @ 0x15, multi-touch (up to 2 fingers)            |
| Mod touch strip                | PC2 SDA, PC3 SCL (bit-banged), PA9 RST  | identical controller, distinct bus                        |
| OLED SSD1306 128×32            | PB3 SCK, PB5 MOSI (SPI1 remap), PB15 CS, PB4 DC, PD2 RST | U8g2 supports it out of the box     |
| Joycoder (stick+rotary+click)  | PC6/PC7 HW quadrature (TIM3), PA0..PA3 4-way, PB13 click | silkscreen labels are swapped — use `PIN_JOY_*` |
| Audio L/R (sigma-delta, opt.)  | PC4, PC5                             | TIM4+DMA1_CH7 drives 352 kHz carrier, zero CPU              |
| Forced DFU bootloader          | `0x08000000..0x08003000`             | untouched by uploads; USB VID 0x17CC, PID 0x1862 in DFU     |

Full layout diagram and every protocol byte are in [`wiki/PERIPHERALS.md`](wiki/PERIPHERALS.md).

### The examples

| Example                         | What you learn                                                   |
|---------------------------------|------------------------------------------------------------------|
| `00_HelloUSB`                   | smoke-test: USB CDC heartbeat — the first sketch to flash         |
| `01_USBSerial`                  | minimal USB CDC echo example                                      |
| `10_ButtonsMapped`              | read the 21 backlit pulsanti by semantic name                    |
| `11_Encoders`                   | 8 endless encoders via muxed ADC sin/cos → delta                 |
| `12_Joycoder`                   | thumbstick + rotary with TIM3 hardware quadrature                |
| `13_ScanMatrix`                 | raw 74HC138×2 scan                                               |
| `14_ScanMatrixDiscover`         | helper that prints `(col, slot)` for each press — re-map friendly |
| `15_LEDDriverTest`              | drive the 21 LEDs over bit-banged I²C                            |
| `16_LEDDriverDiscover`          | discover which register / bit lights which LED                   |
| `17_OLED`                       | SSD1306 128×32 init + drawing                                    |
| `18_TouchStrips`                | bit-banged I²C of the CSM224-5 pitch + mod controllers           |
| `19_TouchEncoder`               | Holtek BS83B08A-3 body-touch bitmap via HW I²C2                  |
| `20_PedalJack`                  | spare GPIO as sustain-pedal input                                |
| `21_ArduinoPortBaseline`        | original "everything-in-one" bring-up sketch                     |
| `99_SynthDemo`                  | mono synth — audio DMA + envelopes + LFO + OLED UI + touch routing |

---

## How it works (and why this approach)

### Memory map — bootloader stays, app rewrites

```
0x08000000 ┌────────────────┐
           │ NI Bootloader  │  12 KB  ← USB DFU lives here — we never touch it
0x08003000 ├────────────────┤
           │  Your firmware │ 116 KB  ← Arduino sketch is linked/flashed here
0x08020000 └────────────────┘
```

Every upload talks to the NI bootloader over vanilla USB DFU 1.x; the bootloader writes to the app partition only. If your sketch bricks USB, the bootloader's forced-DFU button combo always brings the board back, and **Tools → Burn Bootloader** flashes the stock 51 148-byte NI app back on top (one-click rollback).

### How the upload flow actually works

1. Arduino IDE compiles your sketch with our variant's linker script (`FLASH ORIGIN = 0x08003000`) and our `VECT_TAB_OFFSET=0x3000`.
2. The IDE invokes `m32_flash` — a single-file Rust binary (~300 KB per host, libusb statically linked, zero runtime deps), which:
   - validates the `.bin` (initial SP in RAM, Reset_Handler in app partition, size ≤ 116 KB)
   - if the M32 is not already in DFU, prints a clear "hold Shift+combo and re-plug USB" hint and polls for up to 2 minutes
3. Once `0x17CC:0x1862` is on the bus, the binary speaks DfuSe directly via `rusb` and writes the app partition (`override_address=0x08003000`).
4. NI bootloader resets the device into your firmware.

The same binary handles **Tools → Programmer → "Restore NI Factory Firmware"** + **Tools → Burn Bootloader** as `m32_flash restore-factory`, which writes the bundled NI factory firmware (`m32_ni_factory.bin`, 51 148 B) back into the app partition — full rollback.

### How we got here (the reverse-engineering story)

- **USB protocol** was decompiled from `KKMUpdater_1_4` (macOS x86_64 Mach-O, Ghidra). The `NI::NHL2::DFUDevice::writeImage` method is plain USB DFU 1.x — no NI-specific framing, no crypto. [`reference/REVERSE_NOTES.md`](reference/REVERSE_NOTES.md) has the full story with control-transfer values.
- **Firmware blob** was found at VA `0x10022DB70` in the Mach-O, 51 148 bytes of uncompressed Cortex-M raw binary. It matches byte-for-byte with an SWD dump of `0x08003000..0x0800FBCC` on a factory board. Shipped verbatim as the `restore-factory` asset.
- **Memory map** (bootloader 12 KB, app 116 KB) confirmed by reading the initial SP/RV vector pair at the start of the extracted blob.
- **Pin mapping** was done with a multimeter + a logic analyser on a running factory board (every chip-select, every I²C address captured live). Each peripheral then reimplemented from scratch in Arduino — see the `examples/` sequence.

---

## Dev (for contributors)

Requirements: Python 3, [Rust toolchain](https://rustup.rs/), Arduino IDE 2.x. No system libusb / no Docker / no extra runtime needed.

### Local build + test

```bash
./scripts/m32_dev.py up
```

That single command:

1. `cargo build --release` for your host (`m32_flash` binary, ~600 KB, libusb static)
2. fetches STM32duino at the pinned version, assembles the platform tarball
3. assembles the `m32-dfu-…-<host>` tool tarball (only your host — the JSON's `systems[]` array is filtered to match)
4. starts a local HTTP server on `:8765`
5. patches `~/.arduinoIDE/arduino-cli.yaml` to point at it
6. runs `arduino-cli core update-index`

In Arduino IDE: Boards Manager → Remove + Install. Iterate by re-running `./scripts/m32_dev.py up`.

Other sub-commands: `package` (no serve), `serve` (no build), `stop`, `status`, `ci` (trigger the GitHub workflow remotely via `gh`, no tag push).

### Release (push-tag-to-GitHub)

```bash
git tag v0.0.2-alpha
git push --tags
```

The `release.yml` workflow does the cross-compile-on-real-Mac-runners that you can't do locally:

1. Matrix `build-rust` job builds `m32_flash` on real `macos-14` (arm64) + `macos-13` (x86_64) + `ubuntu-latest` + `windows-latest` runners
2. `package` job (Linux) downloads the four binaries, runs `m32_dev.py package` with `RUST_BUILDS_DIR` pointing at them and `ASSET_URL_PREFIX` at the upcoming Release URL, producing the platform + four tool tarballs and the index JSON
3. Creates a GitHub Release `v<version>` with the tarballs as assets
4. Commits the regenerated `package_m32_index.json` to `main`

The Boards-Manager URL (`https://raw.githubusercontent.com/fedepaj/m32duino/main/package_m32_index.json`) never changes — users see the new version appear under Boards Manager updates.

To dry-run the workflow without pushing a tag: `./scripts/m32_dev.py ci --version 0.0.2-cidev`. This triggers `workflow_dispatch` on the real GitHub runners and uploads the artefacts to the workflow run, skipping Release creation.

### Repo layout

```
m32duino/
├── platform/
│   ├── boards.txt                  standalone NI M32 board (clock + USB + opt menus)
│   └── variants/NI_M32/            linker @ 0x08003000, pin aliases, peripheral constants
├── tools/
│   ├── m32_flasher/                Rust crate — the m32_flash binary (rusb + dfu-libusb)
│   └── m32_dfu/firmware/           NI factory firmware blob bundled into tool tarballs
├── examples/…                      the 15 example sketches (M32_Examples library at install time)
├── wiki/…                          INSTALL, FLASHING_WORKFLOW, PERIPHERALS, TROUBLESHOOTING (→ GitHub Wiki)
├── scripts/m32_dev.py              the one-stop dev tool (build + serve + ci)
├── templates/platform.patch        section recipe applied on top of STM32duino's platform.txt
├── package_m32_index.template.json template; m32_dev.py renders concrete instances
├── .github/workflows/release.yml   CI: matrix Rust build + package + GitHub Release
└── VERSION                         current version string
```

---

## Acknowledgements & licence

- Platform tarball includes a slim fork of [stm32duino/Arduino_Core_STM32](https://github.com/stm32duino/Arduino_Core_STM32) (Apache-2.0) pinned to `v2.7.1`. Kudos to that team.
- The `m32_flash` binary uses [`rusb`](https://crates.io/crates/rusb) (MIT) + [`dfu-libusb`](https://crates.io/crates/dfu-libusb) (MIT/Apache-2.0) + statically-linked libusb-1.0 (LGPL-2.1).
- Bundled NI factory firmware (`m32_ni_factory.bin`, 51 148 B) redistributed verbatim for interoperability with hardware the end user already owns.
- Everything original in this repo is MIT-licensed. See [`LICENSE`](LICENSE).
