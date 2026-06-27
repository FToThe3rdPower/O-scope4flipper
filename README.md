# O-scope4flipper

Building the best oscilloscope I can for the Flipper Zero, based on [flipperscope](https://github.com/anfractuosity/flipperscope), to record commercial flight radiation data from a DIY PIN diode detector.

The detector works around 20 mV triggering and a 40 µs window. Hoping to capture elevated background radiation on a flight — may not be achievable with this detector at 9 V, but it works fine with a check source.

### File layout

```text
O-scope4flipper/
├── application.fam         app manifest (appid="oscope", entry=scope_main)
├── scope.c                 app lifecycle, view/scene allocation
├── scope_app_i.h           all types, option lists, ScopeApp struct
├── scope_10px.png          app icon
├── icons/                  pause / play icons
└── scenes/
    ├── adc.c               STM32WB LL ADC driver (copied unchanged from flipperscope)
    ├── scope_scene_run.c   main acquisition, signal processing, display
    ├── scope_scene_setup.c settings menu (LED brightness only)
    ├── scope_scene_save.c  save capture to SD card
    ├── scope_scene_start.c custom main menu (Run + mode picker + Settings + About)
    ├── scope_scene_about.c about / controls reference
    └── scope_scene.{c,h}, scope_scene_config.h, scope_types.h  (scene routing)
```

### Main menu

The main menu has three rows navigated with Up/Down:

- **Run** — with Left/Right to scroll through the seven operating modes (see below). Press OK to enter the selected mode.
- **Settings** — LED brightness selector.
- **About** — controls reference.

The selected mode name and a two-line description are shown below the Run row.

### Operating modes

| Mode | Description |
| --- | --- |
| **Time** | Continuous time-domain oscilloscope trace |
| **Capture** | Triggered: waits for a rising edge above the threshold, snapshots that buffer, pauses for review/save |
| **Record** | Records all samples to SD card continuously — noise is recorded too |
| **FFT** | Frequency spectrum via Fast Fourier Transform |
| **Counter** | Pulse counter — displays CPS and CPM in real time |
| **Histogram** | Pulse-height histogram (PHD) across frames — useful for radiation energy spectroscopy |
| **VGM** | Streams waveform data from the Video Game Module's RP2040 ADC over UART (see Hardware) |

### Live HUD

Time, Capture, Record, and VGM modes all show a two-row interactive HUD while running:

```text
T:1ms                    1x
trig:20mV   +0mV       [▶]
```

Up/Down cycles the selection highlight through the four fields and the play/pause icon. When a field is highlighted, OK enters edit mode and Up/Down/Left/Right change its value. OK or Back exits edit mode.

| Field | Left / Right | Up / Down |
| --- | --- | --- |
| **T:** (time/sample) | Decrease / increase time period | — |
| **trig:** (threshold) | — | Decrease / increase threshold |
| **offset** (mV) | — | Shift waveform ±100 mV |
| **scale** | Decrease / increase zoom | — |

### LED run-state

The Flipper RGB LED reflects the current state while in a run mode:

| Color | State |
| --- | --- |
| Green | Live / recording |
| Yellow | Paused |
| Red | Record mode (actively writing to SD) |

Brightness is configurable in Settings (Off / Low / Mid / High).

### Run controls

| Button | Action |
| --- | --- |
| OK | Pause / unpause (when no HUD field is highlighted) |
| OK *(field highlighted, live)* | Enter field-edit mode |
| OK *(field editing)* | Exit field-edit mode |
| OK *(Capture, waiting)* | Force-capture the current buffer immediately |
| OK *(Capture, triggered)* | Discard snapshot and resume watching for the next trigger |
| OK *(Histogram, paused)* | Clear histogram and resume |
| Up / Down *(live, no edit)* | Cycle HUD field selection |
| Up / Down *(live, editing)* | Adjust highlighted field value |
| Up / Down *(paused)* | Shift waveform ±100 mV (level shift) |
| Left / Right *(live, no edit)* | Cycle HUD field selection |
| Left / Right *(live, editing)* | Adjust highlighted field value |
| Left / Right *(paused)* | Pan through buffer 16 samples at a time |
| Left *(Histogram, paused)* | Clear histogram |
| Right *(Capture, triggered)* | Save snapshot to SD card |
| Right *(Record or VGM, paused)* | Save current buffer to SD card |
| Back *(field editing)* | Exit field-edit mode |
| Back | Exit to main menu |

### Settings

| Setting | Options |
| --- | --- |
| LED Brightness | Off, Low (60), Mid (150), High (255) |

All other parameters (time/sample, scale, threshold, vertical offset) are adjustable live via the on-screen HUD.

### Threshold options

5 mV, 10 mV, 15 mV, 20 mV, 25 mV, 30 mV, 40 mV, 50 mV, 100 mV, 200 mV, 300 mV, 500 mV, 750 mV, 1.0 V, 1.25 V, 1.5 V, 2.0 V, 2.25 V, 2.5 V, 2.75 V, 3 V, 3.2 V

### Hardware

#### STM32 ADC input (all modes except VGM)

Signal input: pin 16 / PC0 — 0 V to 2.5 V, GND to pin 18. The internal 2.5 V VREFBUF is enabled in firmware.

#### VGM mode — RP2040 over UART

The Video Game Module's RP2040 samples a <+3.3 V signal at up to 500 kSPS on GP26 (ADC0) and streams 512-sample frames to the Flipper over UART0 at 921600 baud.

Wiring:

| RP2040 pin | Flipper GPIO pin | Signal |
| --- | --- | --- |
| GP0 (UART0 TX) | Pin 14 (USART1 RX) | RP2040 → Flipper |
| GP1 (UART0 RX) | Pin 13 (USART1 TX) | Flipper → RP2040 |
| GND | Pin 18 | Common ground |

Frame format: `0xAA 0x55` magic | 2-byte sample count (big-endian) | N × uint16_t mV values (big-endian, 0–3300) | CRC8 (poly 0x31, init 0xFF).

RP2040 firmware lives in the companion repo: [flipperVideoGameModDigitizerFirmware](https://github.com/FToThe3rdPower/flipperVideoGameModDigitizerFirmware).

### Build

```bash
cd /path/to/O-scope4flipper
ufbt
```
