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
    ├── scope_scene_setup.c settings menu (Time/sample, FFT window, Scale, Threshold)
    ├── scope_scene_save.c  save capture to SD card
    ├── scope_scene_start.c custom main menu (Run + mode picker + Settings + About)
    ├── scope_scene_about.c about / controls reference
    └── scope_scene.{c,h}, scope_scene_config.h, scope_types.h  (scene routing)
```

### Main menu

The main menu has three rows navigated with Up/Down:

- **Run** — with Left/Right to scroll through the six operating modes (see below). Press OK to enter the selected mode.
- **Settings** — configure Time/sample, FFT window, display scale, and trigger threshold.
- **About** — controls reference.

The selected mode name and a two-line description are shown below the Run row.

### Operating modes

| Mode | Description |
| --- | --- |
| **Time** | Continuous time-domain oscilloscope trace |
| **Capture** | Triggered: waits for a rising edge above the threshold, snapshots that buffer, pauses for review/save |
| **Record** | Records all samples to SD card continuously — silence is recorded too *(placeholder)* |
| **FFT** | Frequency spectrum via Fast Fourier Transform |
| **Counter** | Pulse counter — displays CPS and CPM in real time |
| **Histogram** | Pulse-height histogram (PHD) across frames — useful for radiation energy spectroscopy |

### New features vs. flipperscope

| Feature | Detail |
| --- | --- |
| **Time scales** | 14 options: 1s → 1µs (was 5) |
| **Level shift** | Up/Down in run mode shifts waveform ±100 mV per press (range ±2.5 V); a dashed zero-reference line appears when offset is active |
| **Horizontal pan** | Left/Right when paused scrolls through the buffer 16 samples per press; position shown as `offset/total` |
| **Larger buffer** | 512 samples (was 128) |
| **Triggered capture** | Capture mode auto-fires on a rising edge above threshold; OK force-triggers immediately; Right saves the snapshot to SD |
| **Pulse counter** | Rising-edge count per buffer window → real-time CPS and CPM |
| **Pulse-height histogram** | Accumulates peak voltages of pulses above threshold into a 128-bin PHD across frames |
| **Trigger threshold** | Configurable in Settings (50 mV – 2.0 V, default 200 mV); used by Capture, Counter, and Histogram modes |
| **Save uses actual buffer size** | Saves all 512 samples rather than a hardcoded 128 |

### Run controls

| Button | Action |
| --- | --- |
| OK | Pause / unpause |
| OK *(Capture, waiting)* | Force-capture the current buffer immediately |
| OK *(Capture, triggered)* | Discard snapshot and resume watching for the next trigger |
| OK *(Histogram, paused)* | Clear histogram and resume |
| Up / Down | Shift waveform ±100 mV (level shift) |
| Left / Right *(paused)* | Pan through buffer 16 samples at a time |
| Left *(Histogram, paused)* | Clear histogram |
| Right *(Capture, triggered)* | Save snapshot to SD card |
| Back | Exit to main menu |

### Settings

| Setting | Options |
| --- | --- |
| Time/sample | 1s, 500ms, 100ms, 50ms, 10ms, 5ms, 1ms, 500µs, 100µs, 50µs, 10µs, 5µs, 2µs, 1µs |
| FFT window | 256, 512, 1024 |
| Scale | 1×, 2×, 4×, 10×, 100× |
| Threshold | 50 mV, 100 mV, 200 mV, 300 mV, 500 mV, 750 mV, 1.0 V, 1.25 V, 1.5 V, 2.0 V |

### Hardware

Signal input: pin 16 / PC0 — 0 V to 2.5 V, GND to pin 18. The internal 2.5 V VREFBUF is enabled in firmware.

### Build

```bash
cd /path/to/O-scope4flipper
ufbt launch
```
