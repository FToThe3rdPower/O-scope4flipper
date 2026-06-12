# O-scope4flipper

I'm trying to build the best Oscilloscope I can in a week for the Flipper zero, based on flipperscope: https://github.com/anfractuosity/flipperscope
, in order to record commercial flight radiation data from a DIY PIN diode detector.

---

## O-scope app — what was built

The `flipperscope/` directory is kept as a read-only reference. The actual app lives at the repo root and is built with `ufbt launch` from there.

### File layout

```text
O-scope4flipper/
├── application.fam       app manifest (appid="oscope", entry=scope_main)
├── scope.c               app lifecycle & defaults
├── scope_app_i.h         all types, option lists, ScopeApp struct
├── scope_10px.png        app icon
├── icons/                pause / play icons
└── scenes/
    ├── adc.c             STM32WB LL ADC driver (copied unchanged)
    ├── scope_scene_run.c main acquisition, signal processing, display
    ├── scope_scene_setup.c settings menu
    ├── scope_scene_save.c  save capture to SD card
    ├── scope_scene_start.c main menu
    ├── scope_scene_about.c about / controls reference
    └── scope_scene.{c,h}, scope_scene_config.h, scope_types.h  (scene routing)
```

### New features vs. flipperscope

| Feature | Detail |
| --- | --- |
| **Time scales** | 14 options: 1s, 500ms, 100ms, 50ms, 10ms, 5ms, 1ms, 500µs, 100µs, 50µs, 10µs, 5µs, 2µs, 1µs (was 5 options) |
| **Level shift** | Up/Down in run mode shifts waveform ±100 mV per press (range ±2.5 V); a dashed zero-reference line appears when offset is active |
| **Horizontal pan** | Left/Right when paused scrolls through the buffer 16 samples per press; current position shown as `offset/total` |
| **Larger buffer** | 512 samples (was 128) — gives 4× scrollable window and more data per frame for pulse counting |
| **PulseCount mode** | Counts rising edges above threshold per buffer window, displays CPS and CPM in real time |
| **Histogram mode** | Accumulates pulse-peak voltages into a 128-bin pulse-height distribution (PHD) across frames — useful for radiation energy spectroscopy; Left when paused clears the histogram |
| **Trigger threshold** | Setup option (50 mV – 2.0 V), used as detection threshold in PulseCount and Histogram modes; default 200 mV |
| **Save uses actual buffer size** | Capture saves all 512 samples rather than a hardcoded 128 |

### Run controls

| Button | Action |
| --- | --- |
| OK | Pause / unpause (in Histogram mode while paused: clears histogram and resumes) |
| Up / Down | Level shift waveform ±100 mV |
| Left / Right (paused) | Pan through buffer; in Histogram mode Left = clear histogram |
| Right (Capture mode, paused) | Save to SD card |
| Back | Exit to menu |

### Hardware

Signal input: pin 16 / PC0 — 0 V to 2.5 V, GND to pin 18. The internal 2.5 V VREFBUF is enabled in firmware (not connected externally on Flipper Zero).

### Build

```bash
cd /path/to/O-scope4flipper
ufbt launch
```
