# Integration Test Runner Usage

## Quick Start

The `run_integration_test.py` script provides a complete workflow for building, flashing, monitoring, and testing integration tests.

### Basic Usage

```bash
# Run all tests for a specific test app
python integration_tests/run_integration_test.py audio_integration

# Run all test apps
python integration_tests/run_integration_test.py all

# Specify serial port
python integration_tests/run_integration_test.py audio_integration --port /dev/cu.usbmodem2101
```

## Command Options

### Test App Selection

- `audio_integration` - Audio hardware integration tests
- `display_integration` - Display and touch integration tests
- `storage_integration` - Storage integration tests
- `controls_integration` - Controls integration tests
- `e2e_workflows` - End-to-end workflow tests
- `all` - Run all test apps sequentially

### Workflow Options

- `--build-only` - Only build, don't flash or run tests
- `--flash-only` - Only flash, don't build or run tests
- `--monitor` / `-m` - Start serial monitor after flashing
- `--no-pytest` - Skip pytest execution (useful for manual testing)
- `--skip-build` - Skip build step (assume already built)
- `--skip-flash` - Skip flash step (assume already flashed)

### Serial Port

- `--port` / `-p` - Specify serial port (auto-detected if not specified)

## Examples

### Complete Workflow (Build + Flash + Test)

```bash
# This is the default - builds, flashes, and runs pytest
python integration_tests/run_integration_test.py audio_integration
```

### Build Only

```bash
python integration_tests/run_integration_test.py audio_integration --build-only
```

### Flash and Monitor (No Tests)

```bash
python integration_tests/run_integration_test.py audio_integration --flash-only --monitor
```

### Run Tests on Already Flashed Device

```bash
python integration_tests/run_integration_test.py audio_integration --skip-build --skip-flash
```

### Run All Test Apps

```bash
python integration_tests/run_integration_test.py all
```

## Test Results

Test results are automatically collected and stored in:
- `integration_tests/results/test_results_YYYYMMDD_HHMMSS.json` - JSON report
- `integration_tests/results/test_results_YYYYMMDD_HHMMSS.html` - HTML report (if pytest-html installed)

## LLM Integration

For LLM/AI agents, the script can be called programmatically:

```python
import subprocess

# Run complete test workflow
result = subprocess.run([
    "python", "integration_tests/run_integration_test.py",
    "audio_integration",
    "--port", "/dev/cu.usbmodem2101"
], capture_output=True, text=True)

print(result.stdout)
print(result.stderr)
print(f"Exit code: {result.returncode}")
```

## Prerequisites

1. **ESP-IDF Environment**: Must be set up and `IDF_PATH` environment variable set
   ```bash
   . $HOME/esp/esp-idf/export.sh
   ```

2. **Python Dependencies**:
   ```bash
   pip install pytest pytest-embedded pytest-embedded-serial-esp pytest-json-report pytest-html
   ```

3. **Hardware**: ESP32-S3 board connected via USB

## Troubleshooting

### "IDF_PATH not set"
Run ESP-IDF export script:
```bash
. $HOME/esp/esp-idf/export.sh
```

### "No serial port found"
- Specify port manually: `--port /dev/cu.usbmodem2101`
- Check USB connection
- Verify device is recognized: `ls /dev/cu.*` (macOS) or `ls /dev/ttyUSB*` (Linux)

### "pytest not found"
Install pytest dependencies:
```bash
pip install pytest pytest-embedded pytest-embedded-serial-esp
```

### Build Failures
- Check ESP-IDF version (requires v5.5.2 or compatible)
- Verify all components are available
- Check CMakeLists.txt in test app directory

### Flash Failures
- Verify serial port is correct
- Check USB cable connection
- Try resetting the board
- Ensure no other process is using the serial port

## Output

The script provides color-coded output:
- 🟢 Green: Success messages
- 🔵 Cyan: Step indicators
- 🟡 Yellow: Warnings
- 🔴 Red: Errors

Test results summary is printed at the end with pass/fail counts.
