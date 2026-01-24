# PT1210 ESP32-S3 Project (ESP-IDF)

ESP-IDF project for PT1210 running on JC4827W543 ESP32-S3 board.

## Board: JC4827W543

- **MCU**: ESP32-S3-WROOM-1-N4R8 (dual-core 240MHz)
- **PSRAM**: 8MB OSPI PSRAM
- **Flash**: 4MB QSPI Flash
- **Display**: NV3041A 480x272 RGB565 (QSPI 4-bit parallel)
- **Touch**: XPT2046 (resistive) or GT911 (capacitive)

## Quick Start

### Using the Development Script (Recommended)

The easiest way to build, flash, and monitor:

```bash
cd esp-idf-pt1210
./dev.py
```

This will:
1. Build the project
2. Flash to the device
3. Start serial monitor

See `DEV_SCRIPT_USAGE.md` for all options.

### Manual Build Process

1. Set up ESP-IDF environment:
   ```bash
   . $HOME/esp/esp-idf/export.sh
   ```

2. Configure the project:
   ```bash
   idf.py set-target esp32s3
   idf.py menuconfig  # Optional: customize configuration
   ```

3. Build the project:
   ```bash
   idf.py build
   ```

4. Flash to device:
   ```bash
   idf.py flash
   ```

5. Monitor serial output:
   ```bash
   idf.py monitor
   ```

## Development Script Usage

The `dev.py` script automates common development tasks:

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

# Use different port
./dev.py -p /dev/cu.usbmodem102
```

See `DEV_SCRIPT_USAGE.md` for complete documentation.

## Project Structure

```
esp-idf-pt1210/
├── CMakeLists.txt          # Root CMakeLists
├── sdkconfig.defaults      # Default SDK configuration
├── dev.py                  # Development automation script
├── dev.sh                  # Bash wrapper for dev.py
├── main/
│   ├── CMakeLists.txt      # Main component CMakeLists
│   ├── main.c              # Main application
│   └── include/
│       └── board_config.h  # Board pin definitions
└── README.md               # This file
```

## Configuration

Default configuration is set in `sdkconfig.defaults`:
- ESP32-S3 target
- 240MHz CPU frequency
- 8MB OSPI PSRAM enabled
- 4MB Flash

## Development

Start with the minimal build to verify:
- Serial console output
- PSRAM detection (8MB)
- Basic GPIO and LEDC functionality

Then add components incrementally:
1. Display initialization
2. Touch controller
3. Graphics library (LVGL)
4. Audio subsystem
5. Application-specific features

## Pin Configuration

See `main/include/board_config.h` for all pin definitions:
- Display QSPI pins (CS, SCK, D0-D3)
- Touch controller pins (XPT2046/GT911)
- Backlight PWM pin
- Other peripherals

# DJS3
