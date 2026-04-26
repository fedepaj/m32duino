# M32 Custom Firmware - Reference

## Hardware base

- **MCU**: STM32F103RBT6 (LQFP64, 128KB flash, 20KB RAM)
- **Clock**: HSE 8MHz cristallo presente. Target 72MHz via PLL×9.
- **Bootloader**: NI bootloader a 0x08000000-0x08002800 (~10KB), poi il nostro firmware
- **Backup firmware NI**: `m32_firmware.bin`, restore con `st-flash write m32_firmware.bin 0x08000000`
- **SWD**: PA13 (SWDIO), PA14 (SWCLK), JP1 sul PCB
- **USB**: D+/D- standard, soft-connect via PA8 pullup

## Pinout completo

### Audio output
- PC4 = audio L (sigma-delta DMA)
- PC5 = audio R (stesso bit, mono replicato per ora)
- Cap 10nF da PC4/PC5 a GND aggiunti esterni per LP filter portante
- R 1kΩ in serie già presente sul PCB

### USB
- PA8 = pullup D+ (HIGH = USB connected)

### OLED SSD1306 128x32 (SPI1 remapped)
- PB3 = SCK (richiede JTAG disable)
- PB5 = MOSI
- PB15 = CS
- PB4 = DC
- PD2 = RES
- Init sequence: AE 40 B0 C0 81 E0 A0 A6 A8 1F D3 00 D5 F0 D9 22 DA 02 DB 49 AF
- U8g2 constructor: U8G2_SSD1306_128X32_UNIVISION_F_4W_HW_SPI con SPI.setMOSI/setSCLK in setup

### LED driver (custom QFN44 marked T47103236a)
- PB6 = SCL (I2C bitbang per errata STM32F103: I2C1 master + SPI1 remap conflict)
- PB7 = SDA
- PA15 = LED reset (active low)
- I2C address 0x3C, 21 LED
- Protocollo init:
  - `4F 00`
  - `00 01`
  - `26` + 37 byte brightness (primi 21 = 0x07, resto 0)
  - `25 01` update trigger
- Set LED: `01` + 21 byte (brightness 0..255), poi `25 01`
- LED mapping (idx → pulsante):
  - 0=shift, 1=scale_edit, 2=arp_edit, 3=undo_redo, 4=quantize_auto
  - 5=ideas, 6=loop, 7=metro, 8=tempo, 9=preset_up, 10=preset_down
  - 11=play_restart, 12=rec_countin, 13=stop_clear, 14=left_m, 15=right_s
  - 16=track_instance, 17=plugin_midi, 18=browser, 19=oct_down, 20=oct_up

### Scan matrix (pulsanti + tasti)
- PB0 = SEL_A0
- PB1 = SEL_A1
- PB2 = SEL_A2
- PB14 = CMD (chip select A/B 74HC138)
- PC8-PC15 = 8 colonne return (input pullup)
- 2× 74HC138 decoder (uno per chip A, uno per B)
- 21 pulsanti + 32 tasti × 2 contatti = 85 coordinate totali

### Mappa scan finale (col, slot, name)
**Pulsanti:**
- 0,0: shift | 1,0: scale_edit | 2,0: arp_edit | 3,0: undo_redo | 4,0: quantize_auto | 5,0: ideas
- 0,2: loop | 1,2: metro | 2,2: tempo
- 3,1: preset_up | 4,1: preset_down
- 3,2: play_restart | 4,2: rec_countin | 5,2: stop_clear
- 6,1: left_m | 7,1: right_s
- 2,1: track_instance | 1,1: plugin_midi | 0,1: browser
- 7,2: oct_down | 6,2: oct_up

**Tasti** (slot pari = TOP, slot dispari = BOT, range MIDI da F2 = note 41):
- F2=0 a C5=31 (32 tasti)
- Pattern: ogni gruppo di 8 tasti usa la stessa coppia di slot
  - F2-C3 (idx 0-7): slot 14/15, col 7→0
  - Cs3-Gs3 (idx 8-15): slot 12/13, col 7→0
  - A3-E4 (idx 16-23): slot 10/11, col 7→0
  - F4-C5 (idx 24-31): slot 8/9, col 7→0
- Velocity: delta_us tra TOP e BOT press, mappa 500us=127 .. 50000us=1

### Encoder rotativi (8 endless analogici "0B20K")
- PB8 = MUX A select
- PB9 = MUX B select (condivisi tra i due 74HC4052)
- PA4, PA5 = ADC X/Y encoder 0-3 (gruppo 1, mux uscite)
- PA6, PA7 = ADC X/Y encoder 4-7 (gruppo 2, mux uscite)
- 2× 74HC4052 dual analog mux
- Sin/cos analogici → atan2 per ricavare angolo, delta dal sample precedente
- CW = positivo (segno invertito nel codice rispetto alla formula naturale)
- ADC channel: 4 (PA4), 5 (PA5), 6 (PA6), 7 (PA7)

### Touch sensor sui corpi degli encoder (BS83B08A-3 Holtek)
- PB10 = SCL (I2C2 hardware)
- PB11 = SDA (I2C2 hardware)
- I2C address 0x50
- Protocollo: write 0xA0, read 1 byte = bitmap (bit N = encoder N)
- Polling ogni 30ms

### Touch strip (2x CSM224-5)
- PC0 = SDA pitch strip (bitbang I2C, OUTPUT_OPEN_DRAIN funziona)
- PC1 = SCL pitch strip
- PC2 = SDA mod strip
- PC3 = SCL mod strip
- PA9, PA10 = reset chip A e B (active LOW, alziamo HIGH per operating)
- I2C address 0x15 (entrambi gli stessi, ma su bus separati)
- Protocollo: write 0x00, read 3 byte
  - Byte 0: 0x01 se touched, 0x00 se no
  - Byte 1: posizione 0..127 (sweep)
  - Byte 2: padding 0x00

### Joycoder
- PC6, PC7 = encoder A/B (TIM3 encoder mode HW)
- PB13 = encoder click
- PA0 = btn LEFT (era right, mappato male in hardware → corretto in software)
- PA1 = btn UP (era down)
- PA2 = btn RIGHT (era left)
- PA3 = btn DOWN (era up)
- TIM3 in encoder mode 3 (count both edges), divisore /2 per 1 click meccanico

### Pin liberi
- PB12 (era jack detect ma jack non usato)
- Sustain pedal: rinunciato

## Allocazione timer e DMA

| Risorsa | Funzione | Note |
|---------|----------|------|
| TIM1 | Encoder mux ADC | ISR ~250us |
| TIM2 | Scan matrix | ISR ~30us |
| TIM3 | Joycoder encoder mode HW | Hardware count, no ISR |
| TIM4 + DMA1_CH7 | Audio carrier 352kHz | Zero CPU |

| DMA1 channel | Uso |
|--------------|-----|
| CH7 | TIM4_UP → GPIOC->BSRR (audio sigma-delta) |
| altri | liberi |

## Engine audio

- Sample rate: 22050 Hz
- Oversampling: 16x
- Carrier sigma-delta: 352800 Hz (DMA-driven, ZERO CPU per la portante)
- Buffer DMA circular: 512 valori uint32_t (sample × OVERSAMP × 2 metà)
- Refill: ISR DMA half/complete a 689 Hz × 2 = 1378 Hz, ognuna riempie 256 valori = 16 sample audio

### Voce (mono, last-note priority)
- 2 osc indipendenti: sin/tri/saw/sqr (LUT 256 sample)
- Phase Q24.8 fixed point (32 bit, top 8 bit indicizza LUT)
- Mix tra osc1 e osc2
- Detune in cents su osc2
- Glide: slew dell'inc tra cambio nota
- ADSR su VCA
- ADSR su filter (mod amount)
- LFO assignable (target: pitch/cutoff/amp)
- Filtro LP 1-polo IIR integer

### Calcolo audio per sample
1. Avanza phase1 e phase2
2. Lookup wavetable per osc1 e osc2
3. Mix lineare osc1↔osc2
4. Filtro LP: state += alpha * (input - state)
5. Apply env amplifier
6. Apply velocity
7. Apply volume master
8. Clip a int16
9. → sigma-delta accumulator → bit per BSRR

## Eventi (ring buffer)

Tutte le ISR producono eventi semantici nel ring buffer; il main loop li consuma.

```c
enum EvtType {
  EVT_NOTE_ON, EVT_NOTE_OFF,
  EVT_BTN_DOWN, EVT_BTN_UP,
  EVT_ENC_DELTA,
  EVT_TOUCH_ENC_ON, EVT_TOUCH_ENC_OFF,
  EVT_TOUCH_PITCH, EVT_TOUCH_MOD,         // value 0..127
  EVT_JOY_DELTA, EVT_JOY_BTN_DOWN, EVT_JOY_BTN_UP
};

struct Event {
  uint8_t type;
  uint8_t idx;     // index (encoder n, button n, ecc)
  int16_t value;   // velocity, delta, position, ecc
};
```

## Stato synth (parametri)

```c
struct SynthParams {
  // OSC
  uint8_t wave1, wave2;
  uint16_t mix;       // 0..65535 (0=osc1 only, 65535=osc2 only)
  int16_t detune;     // ±100 cents
  // FILTER
  uint16_t cutoff, reso;
  // GLIDE / VOL
  uint16_t glide;
  uint16_t volume;
  // AMP ADSR
  uint16_t att, dec, sus, rel;
  // FILTER ADSR
  uint16_t fAtt, fDec, fSus, fRel;
  uint16_t fEnvAmount;  // 0..65535 = -100% a +100%? da decidere
  // LFO
  uint8_t lfoWave;
  uint16_t lfoRate, lfoDepth;
  uint8_t lfoTarget;    // 0=pitch, 1=cutoff, 2=amp
  // ARP
  bool arpOn;
  uint16_t arpBpm;
};
```

## Pagine encoder (4 pagine × 8 encoder = 32 parametri)

| Pag | Enc 0 | Enc 1 | Enc 2 | Enc 3 | Enc 4 | Enc 5 | Enc 6 | Enc 7 |
|-----|-------|-------|-------|-------|-------|-------|-------|-------|
| 1 OSC | wave1 | wave2 | mix | detune | cutoff | reso | glide | volume |
| 2 ENV | a.att | a.dec | a.sus | a.rel | f.att | f.dec | f.sus | f.rel |
| 3 MOD | lfoRate | lfoDepth | lfoWave | lfoTarget | fEnvAmt | -- | -- | -- |
| 4 FX  | -- | -- | -- | -- | -- | -- | -- | -- |

Switch pagina: pulsante BROWSER cycla 1→2→3→4.

## Pulsanti (LED)

- PLAY: arp on (LED on quando attivo)
- STOP: arp off + clear (LED flash on press)
- OCT_UP/DOWN: ottava ±1 (LED on quando shifted)
- BROWSER: cycle pagina (LED flash quando premuto)
- SHIFT: modificatore (LED on mentre tenuto)
- IDEAS: random patch (LED flash)
- altri: future

## OLED viste (30fps)

### Default
- Riga top: nome preset / pagina corrente
- Center: nota + nome ottava (es. "C4")
- Bottom: waveform animata (forma di osc1+osc2 mixed) che scrolla

### Touch parameter view
Visualizzazione specifica per parametro toccato:
- Wave selection: disegna grossa la wave selezionata
- ADSR: 4 segmenti dell'inviluppo, segmento corrente in highlight
- Cutoff/reso: curva di risposta filtro che si deforma
- LFO rate/depth: curva LFO che oscilla
- Mix/detune: 2 wave sovrapposte
- Generic: nome param + valore grosso al centro + barra

### Page indicator
Banner "PAGE X NAME" 1 secondo dopo cambio pagina

## Touch strip

- Pitch (PC0/1): pitch bend ±2 semitoni
  - Position 64 = center, 0 = -2 semi, 127 = +2 semi
  - Touch off → return graduale a 64 (con slew)
- Mod (PC2/3): manda valore "mod wheel" 0..127
  - Default: scala LFO depth
  - Future: assignable

Polling I2C nel main ogni 20ms per ognuna.

## Decisioni di design

- **ADSR**: segmenti lineari (no LUT esponenziale)
- **Filter envelope amount**: unsigned (0..100%, solo apre il filtro)
- **Glide**: esponenziale (slew con coefficiente)
- **LFO rate**: 0.1Hz a 20Hz
- **Mono**: legato priority (no re-trigger ADSR su cambio nota mentre altre tenute)
- **Random patch (IDEAS)**: skip per ora
- **Save preset**: skip per ora
