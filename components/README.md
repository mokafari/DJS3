# DJ Deck Components

This directory contains the core components for the P4-DJ Deck project.

## Component Structure

### `timestretch/` - Granular Synthesis Engine

The time-stretching engine implements beat-synced granular synthesis for creative audio manipulation.

**Key Features:**
- Beat-synced grain restart
- Configurable grain size (10-200ms)
- Density/overlap control
- Jitter for glitchy effects
- Freeze mode for infinite loops
- **Slip loop engine** with circular buffer for short stutters

**Files:**
- `granular_engine.h/c` - Main granular synthesis engine
- `beat_sync.h/c` - Beat grid synchronization
- `slip_loop.h/c` - Slip loop engine (Pioneer CDJ-style slip mode)

**Usage:**
```c
granular_engine_t engine;
granular_engine_init(&engine, audio_buffer, buffer_size, 44100);

granular_params_t params = granular_engine_default_params();
params.grain_size_ms = 50.0f;
params.beat_sync_enabled = true;
granular_engine_set_params(&engine, &params);

granular_engine_set_bpm(&engine, 120.0f);
granular_engine_process(&engine, output_buffer, num_samples);
```

**Slip Loop Usage:**
```c
slip_loop_t slip;
slip_loop_init(&slip, 44100 * 4, 44100); // 4 second buffer

// Time-based stutter (500ms)
slip_loop_start_time(&slip, current_pos, 500);

// Beat-synced stutter (1 beat)
slip_loop_set_bpm(&slip, 120.0f);
slip_loop_start_beat(&slip, current_pos, 1);

// Process audio (loops section while track continues in background)
slip_loop_process(&slip, main_buffer, buffer_size, output, num_samples);

// Stop and jump to background position
uint32_t background_pos = slip_loop_stop(&slip);
```

### `fx/` - Audio Effects Processing

Audio effects chain for EQ, filters, delay, reverb, and creative effects.

**Key Features:**
- Modular FX chain
- Lowpass/Highpass/Bandpass filters
- Parametric EQ
- Delay with feedback
- Reverb
- Flanger
- Rhythmic Gater

**Files:**
- `audio_fx.h/c` - Main FX chain manager
- `filter.c` - Digital filters
- `delay.c` - Delay/echo
- `reverb.c` - Reverb effect

**Usage:**
```c
audio_fx_chain_t *chain = audio_fx_chain_create(44100);
int fx_id = audio_fx_add(chain, FX_TYPE_LOWPASS, true);
audio_fx_set_lowpass(chain, fx_id, 1000.0f, 0.7f);
audio_fx_process(chain, input, output, num_samples);
```

### `sync/` - Synchronization

MIDI and analog sync for hardware integration.

**Key Features:**
- MIDI clock generation (master mode)
- MIDI clock following (slave mode)
- Analog sync pulse output (for Volcas, Pocket Operators)
- Swing/groove control
- Future: Ableton Link support

**Files:**
- `midi_sync.h/c` - MIDI clock synchronization
- `analog_sync.h/c` - Analog pulse sync
- `ableton_link.h/c` - Wireless sync (future)

**Usage:**
```c
midi_sync_t *sync = midi_sync_create(UART_NUM_1, GPIO_NUM_17);
midi_sync_set_mode(sync, MIDI_SYNC_MODE_MASTER);
midi_sync_set_bpm(sync, 120.0f);
midi_sync_start(sync);
```

### `ui/` - High-Contrast HUD Interface

User interface components for the monochrome HUD display.

**Key Features:**
- High-contrast monochrome theme (Amber/Cyan/Green)
- Waveform display (vertical bar graph style)
- Telemetry display (BPM, pitch, phase error)
- Library browser (crate view)
- Touch feedback

**Files:**
- `ui_manager.h/c` - Main UI manager
- `hud_theme.h/c` - Theme system
- `waveform_view.h/c` - Waveform display
- `telemetry_view.h/c` - BPM/pitch display
- `crate_view.h/c` - Library browser

**Usage:**
```c
ui_manager_init(800, 480);
ui_manager_set_theme(UI_THEME_AMBER);
ui_manager_set_view(UI_VIEW_WAVEFORM);
ui_manager_update_waveform(waveform_data, num_samples, position);
ui_manager_update_telemetry(120.0f, 0.5f, 0.1f);
```

## Integration

All components are designed to work together:

1. **Timestretch Engine** processes audio with granular synthesis
2. **FX Chain** applies effects to the processed audio
3. **Sync** generates/follows MIDI clock for hardware integration
4. **UI** displays visual feedback and controls

## Dependencies

- ESP-IDF (FreeRTOS, driver components)
- LVGL (for UI, to be integrated)
- Standard C library (math, string, stdlib)

## Build

Components are automatically included when building the main project. Each component has its own `CMakeLists.txt` that defines dependencies and source files.

## Status

- ✅ **Timestretch**: Core engine implemented, ready for testing
- 🚧 **FX**: Framework in place, individual effects to be implemented
- ✅ **Sync**: MIDI and analog sync implemented
- 🚧 **UI**: Framework in place, LVGL integration pending

