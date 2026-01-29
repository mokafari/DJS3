# DJS3 - ESP32-S3 DJ Controller

A standalone CDJ-style DJ deck application running on ESP32-S3, featuring real-time audio playback, waveform visualization, and a high-contrast HUD interface.

## Overview

DJS3 is a professional DJ controller firmware for the **JC4827W543 ESP32-S3 development board**. It provides CDJ-style functionality including:

- **Audio Playback**: MP3 decoding with low-latency I2S output
- **Waveform Display**: Real-time scrolling waveform visualization with beat grid
- **High-Contrast HUD UI**: Industrial telemetry-style interface with phosphor color themes
- **Track Management**: Library browsing with ID3 tag support
- **DJ Controls**: Cue points, loops, pitch control, and jog wheel support
- **Storage**: SD card and USB host support for music files

## Hardware Platform

### JC4827W543 ESP32-S3 Board

- **MCU**: ESP32-S3-WROOM-1-N4R8 (dual-core 240MHz)
- **PSRAM**: 8MB OSPI PSRAM (for audio buffering)
- **Flash**: 4MB QSPI Flash
- **Display**: NV3041A 480×272 RGB565 (QSPI 4-bit parallel)
- **Touch**: GT911 capacitive touch controller
- **Audio**: I2S output (onboard NS4168 or external DAC)
- **Storage**: SD card slot + USB host port

### Pin Configuration

All pin definitions are centralized in `main/include/board_config.h`:
- Display QSPI interface (CS, SCK, D0-D3)
- Touch controller (I2C: SCL, SDA, RES, INT)
- Audio I2S (BCLK, LRCK, DIN)
- SD card SPI interface
- USB OTG pins

## Quick Start

### Prerequisites

1. **ESP-IDF v5.5.2** (or compatible)
   ```bash
   # Set up ESP-IDF environment
   . $HOME/esp/esp-idf/export.sh
   ```

2. **Python 3** (for development script)

3. **Hardware**: JC4827W543 board connected via USB

### Using the Development Script (Recommended)

The easiest way to build, flash, and monitor:

```bash
./dev.py
```

This automatically:
1. Builds the project
2. Flashes to the device
3. Starts serial monitor

### Development Script Options

```bash
# Full workflow (build + flash + monitor)
./dev.py

# Build only
./dev.py --build-only

# Flash only (assumes already built)
./dev.py --flash-only

# Monitor only
./dev.py --monitor-only

# Reset device
./dev.py --reset

# Clean build
./dev.py --clean

# Use different serial port
./dev.py -p /dev/cu.usbmodem102
```

### Manual Build Process

1. **Set target and configure:**
   ```bash
   idf.py set-target esp32s3
   idf.py menuconfig  # Optional: customize configuration
   ```

2. **Build:**
   ```bash
   idf.py build
   ```

3. **Flash:**
   ```bash
   idf.py flash
   ```

4. **Monitor:**
   ```bash
   idf.py monitor
   ```

## Project Structure

```
DJS3-idf-copy/
├── main/                          # Main application
│   ├── main.c                     # Application entry point
│   ├── audio_player.cpp           # Audio playback engine
│   ├── audio_output.c             # I2S audio output
│   ├── display.c                  # Display driver (NV3041A)
│   ├── controls.c                 # DJ controls (buttons, jog wheel)
│   ├── pitch_control.c            # Pitch fader handling
│   ├── cue_points.c               # Cue point management
│   ├── loop_control.c             # Loop in/out points
│   ├── track_db.c                 # Track library database
│   ├── waveform.c                 # Waveform peak extraction
│   ├── storage.c                  # Storage abstraction (SD/USB)
│   ├── sd_card.c                  # SD card interface
│   ├── usb_host.c                 # USB host mode
│   ├── id3_parser.c               # ID3 tag parsing
│   └── include/                   # Public headers
│       ├── board_config.h         # Hardware pin definitions
│       ├── audio_player.h
│       ├── controls.h
│       └── ...
│
├── components/                    # Reusable components
│   ├── ui/                        # High-contrast HUD UI system
│   │   ├── ui_manager.c          # UI coordinator
│   │   ├── waveform_view.c        # Waveform display
│   │   ├── telemetry_view.c       # BPM/pitch display
│   │   ├── metadata_view.c        # Track info display
│   │   ├── crate_view.c           # Library browser
│   │   ├── hud_theme.c            # Theme system (Amber/Cyan/Green)
│   │   └── lvgl_driver.c          # LVGL integration
│   │
│   ├── esp8266audio/              # MP3 decoder (libmad)
│   ├── libhelix-mp3/              # Alternative MP3 decoder
│   ├── fx/                        # Audio effects
│   ├── timestretch/               # Time-stretching algorithms
│   └── sync/                      # Synchronization (MIDI, Ableton Link)
│
├── CMakeLists.txt                 # Root CMakeLists
├── sdkconfig.defaults              # Default ESP-IDF configuration
├── dev.py                         # Development automation script
└── README.md                       # This file
```

## Features

### Audio Engine

- **MP3 Decoding**: Real-time MP3 decoding using libmad
- **Low Latency**: <10ms audio latency (128 sample buffer @ 44.1kHz)
- **Dual-Core**: Audio processing on Core 0, UI on Core 1
- **I2S Output**: High-quality audio via I2S (supports external DAC)
- **Format Support**: MP3 (primary), FLAC/WAV (planned)

### User Interface

- **High-Contrast HUD**: Industrial telemetry aesthetic
- **Phosphor Themes**: Amber, Cyan, and Green color schemes
- **Waveform Display**: 
  - Real-time scrolling waveform (480 bars)
  - Beat grid overlay
  - Playhead indicator
  - Touch cursor feedback
- **Telemetry View**: BPM, pitch, and phase error display
- **Metadata Display**: Track title, key (Camelot), time remaining
- **Crate View**: Library browser with track selection

### DJ Features

- **Cue Points**: 4 hot cue buttons + main cue
- **Loop Control**: Loop in/out points
- **Pitch Control**: Pitch fader with nudge buttons
- **Jog Wheel**: Touch-sensitive jog wheel (scratch/nudge)
- **Track Loading**: Automatic track scanning from SD/USB

### Storage

- **SD Card**: FAT32/exFAT support
- **USB Host**: USB mass storage device support
- **Track Database**: In-memory track library with ID3 metadata
- **Auto-Scan**: Automatic track discovery on mount

## Configuration

### Default Settings (`sdkconfig.defaults`)

- **Target**: ESP32-S3
- **CPU Frequency**: 240MHz
- **PSRAM**: 8MB OSPI PSRAM enabled
- **Flash**: 4MB QSPI Flash
- **Partition Table**: Default (can be customized)

### Customization

Use `idf.py menuconfig` to customize:
- Component configurations
- FreeRTOS settings
- Memory allocations
- Log levels

## Development

### Initial Setup

1. **Verify Hardware:**
   - Check serial console output
   - Verify PSRAM detection (8MB)
   - Test display initialization
   - Verify touch controller

2. **Add Music Files:**
   - Copy MP3 files to SD card or USB drive
   - Insert SD card or connect USB drive
   - Tracks will be automatically scanned

### Debugging

- **Serial Monitor**: Use `./dev.py --monitor-only` or `idf.py monitor`
- **Logging**: ESP-IDF logging system (ESP_LOGI, ESP_LOGE, etc.)
- **GDB**: Use `idf.py gdb` for debugging (requires OpenOCD)

### Performance Optimization

- **Audio Latency**: Adjust buffer size in `audio_output.c`
- **UI Frame Rate**: Target 60 FPS (16.67ms per frame)
- **Waveform Resolution**: Configurable (1x, 2x, 4x, 8x) for performance

## Architecture

### System Architecture

```
┌─────────────────────────────────────────┐
│           Main Application              │
│  (main.c - Orchestration & Event Loop) │
└──────────────┬──────────────────────────┘
               │
    ┌──────────┼──────────┐
    │          │          │
┌───▼───┐  ┌───▼───┐  ┌───▼───┐
│ Audio │  │  UI   │  │Storage│
│Engine │  │System │  │System │
└───┬───┘  └───┬───┘  └───┬───┘
    │          │          │
┌───▼───┐  ┌───▼───┐  ┌───▼───┐
│ I2S   │  │ LVGL  │  │ SD/USB│
│Output │  │Driver │  │Driver │
└───────┘  └───────┘  └───────┘
```

### Component Responsibilities

- **main.c**: System initialization, event loop, component orchestration
- **audio_player**: MP3 decoding, playback state, waveform extraction
- **ui_manager**: UI coordination, view switching, theme management
- **controls**: Button/encoder input handling, event callbacks
- **track_db**: Track library management, ID3 parsing, metadata storage

## Building from Source

### Requirements

- ESP-IDF v5.5.2 or compatible
- Python 3.6+
- CMake 3.16+
- GCC toolchain (provided by ESP-IDF)

### Build Steps

```bash
# 1. Set up ESP-IDF environment
. $HOME/esp/esp-idf/export.sh

# 2. Navigate to project directory
cd DJS3-idf-copy

# 3. Set target
idf.py set-target esp32s3

# 4. Build
idf.py build

# 5. Flash (adjust port as needed)
idf.py -p /dev/cu.usbmodem2101 flash

# 6. Monitor
idf.py -p /dev/cu.usbmodem2101 monitor
```

## Usage

### Basic Operation

1. **Power On**: Connect board via USB
2. **Load Tracks**: Insert SD card or USB drive with MP3 files
3. **Browse Library**: Use touch screen to navigate crate view
4. **Select Track**: Tap track to load and play
5. **Control Playback**: 
   - Use buttons for play/pause, cue, loops
   - Use touch screen for waveform scrubbing
   - Adjust pitch with fader (if connected)

### UI Navigation

- **Waveform View**: Main playback view (default)
- **Crate View**: Library browser (tap to switch)
- **Theme Selection**: Change phosphor color (Amber/Cyan/Green)

## Troubleshooting

### Common Issues

**Display not working:**
- Check SPI connections
- Verify backlight PWM configuration
- Check display initialization logs

**Audio not playing:**
- Verify I2S pin configuration
- Check audio output initialization
- Ensure MP3 file is valid

**Touch not responding:**
- Verify I2C connections (GT911)
- Check touch controller initialization
- Review touch calibration

**Tracks not loading:**
- Verify SD card/USB is formatted (FAT32)
- Check file format (MP3 supported)
- Review storage initialization logs

## Contributing

See `PROJECT_RULES.md` for coding standards and contribution guidelines.

## License

[Add license information here]

## Acknowledgments

- ESP-IDF framework
- LVGL graphics library
- libmad MP3 decoder
- Helix MP3 decoder

---

**Project Status**: Active Development  
**Last Updated**: January 2025
