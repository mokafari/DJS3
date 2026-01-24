# PT1210 ESP32-S3 Development Scripts

## Quick Start

The easiest way to build, flash, and monitor:

```bash
cd esp-idf-pt1210
./dev.py
```

Or use the bash wrapper:

```bash
./dev.sh
```

## Usage

### Default (Build + Flash + Monitor)
```bash
./dev.py
```

### Build Only
```bash
./dev.py --build-only
```

### Flash Only (assumes already built)
```bash
./dev.py --flash-only
```

### Monitor Only
```bash
./dev.py --monitor-only
```

### Reset Device
```bash
./dev.py --reset
```

### Clean Build
```bash
./dev.py --clean
```

### Open Menuconfig
```bash
./dev.py --menuconfig
```

### Use Different Serial Port
```bash
./dev.py -p /dev/cu.usbmodem102
```

### Build and Flash Without Monitor
```bash
./dev.py --no-monitor
```

## Examples

**Full workflow (build, flash, monitor):**
```bash
./dev.py
```

**Clean rebuild:**
```bash
./dev.py --clean
```

**Quick iteration (after code changes):**
```bash
./dev.py --no-monitor  # Build and flash quickly
./dev.py --monitor-only  # Then monitor separately
```

**Reset and monitor:**
```bash
./dev.py --monitor-only --reset
```

## Configuration

Edit `dev.py` to change defaults:
- `SERIAL_PORT`: Default serial port
- `IDF_PATH`: ESP-IDF installation path
- `BAUD_RATE`: Serial monitor baud rate

## Tips

- Press `Ctrl+]` to exit monitor
- Use `--build-only` to check for compilation errors quickly
- Use `--monitor-only` to reconnect to serial output without rebuilding
- The script automatically sets up the ESP-IDF environment

