# Integration Test Workflow for LLMs

This document describes how to use the integration test runner script for automated testing workflows.

## Quick Reference

### Single Command Workflow

```bash
# Complete workflow: build + flash + test
python integration_tests/run_integration_test.py audio_integration
```

### Available Test Apps

- `audio_integration` - Audio hardware (I2S, MP3 playback)
- `display_integration` - Display and touch controller
- `storage_integration` - SD card and USB storage
- `controls_integration` - GPIO buttons and encoders
- `e2e_workflows` - Complete end-to-end workflows
- `all` - Run all test apps sequentially

## LLM Integration Examples

### Python Subprocess Call

```python
import subprocess
import sys

def run_integration_test(test_app, port=None, build_only=False, flash_only=False):
    """Run integration test workflow"""
    cmd = [
        sys.executable,
        "integration_tests/run_integration_test.py",
        test_app
    ]
    
    if port:
        cmd.extend(["--port", port])
    if build_only:
        cmd.append("--build-only")
    if flash_only:
        cmd.append("--flash-only")
    
    result = subprocess.run(cmd, capture_output=True, text=True)
    
    return {
        "success": result.returncode == 0,
        "stdout": result.stdout,
        "stderr": result.stderr,
        "returncode": result.returncode
    }

# Example usage
result = run_integration_test("audio_integration", port="/dev/cu.usbmodem2101")
if result["success"]:
    print("Tests passed!")
else:
    print(f"Tests failed: {result['stderr']}")
```

### Shell Script Execution

```bash
#!/bin/bash
# Run integration test and capture results

TEST_APP="audio_integration"
PORT="/dev/cu.usbmodem2101"

cd /path/to/DJS3-idf-copy
python integration_tests/run_integration_test.py "$TEST_APP" --port "$PORT"

# Check exit code
if [ $? -eq 0 ]; then
    echo "SUCCESS: All tests passed"
    exit 0
else
    echo "FAILURE: Some tests failed"
    exit 1
fi
```

## Workflow Steps

The script automates these steps:

1. **Environment Check** - Verifies ESP-IDF is set up
2. **Build** - Compiles the test app (`idf.py build`)
3. **Flash** - Uploads to device (`idf.py flash`)
4. **Monitor** (optional) - Serial monitor for debugging
5. **Test Execution** - Runs pytest tests
6. **Results Collection** - Generates JSON and HTML reports

## Command Options

| Option | Description | Example |
|--------|-------------|---------|
| `--port` / `-p` | Serial port | `--port /dev/cu.usbmodem2101` |
| `--build-only` | Only build, skip flash/test | `--build-only` |
| `--flash-only` | Only flash, skip build/test | `--flash-only` |
| `--monitor` / `-m` | Start serial monitor | `--monitor` |
| `--no-pytest` | Skip pytest execution | `--no-pytest` |
| `--skip-build` | Skip build (assume built) | `--skip-build` |
| `--skip-flash` | Skip flash (assume flashed) | `--skip-flash` |

## Test Results

Results are stored in `integration_tests/results/`:
- `test_results_YYYYMMDD_HHMMSS.json` - Machine-readable JSON
- `test_results_YYYYMMDD_HHMMSS.html` - Human-readable HTML

## Error Handling

The script provides:
- Color-coded output (green=success, red=error, yellow=warning)
- Exit codes (0=success, non-zero=failure)
- Detailed error messages
- Automatic port detection

## Prerequisites Check

Before running, ensure:
1. ESP-IDF environment is set: `export IDF_PATH=...`
2. Python dependencies installed: `pip install -r integration_tests/requirements.txt`
3. Hardware connected via USB
4. Serial port accessible

## Common Workflows

### 1. Build and Test Single App
```bash
python integration_tests/run_integration_test.py audio_integration
```

### 2. Build All Apps
```bash
python integration_tests/run_integration_test.py all --build-only
```

### 3. Flash and Monitor (No Tests)
```bash
python integration_tests/run_integration_test.py audio_integration --flash-only --monitor
```

### 4. Run Tests on Already Flashed Device
```bash
python integration_tests/run_integration_test.py audio_integration --skip-build --skip-flash
```

### 5. Complete Test Suite
```bash
python integration_tests/run_integration_test.py all
```

## Output Format

The script outputs:
- Step-by-step progress indicators (▶)
- Success messages (✓)
- Error messages (✗)
- Warning messages (⚠)
- Test summary at the end

## Integration with CI/CD

For continuous integration:

```yaml
# Example GitHub Actions workflow
- name: Run Integration Tests
  run: |
    source $HOME/esp/esp-idf/export.sh
    python integration_tests/run_integration_test.py all
  continue-on-error: false
```

## Troubleshooting

### Port Not Found
- Specify manually: `--port /dev/cu.usbmodem2101`
- Check USB connection
- Verify device permissions

### Build Failures
- Check ESP-IDF version
- Verify all components available
- Review CMakeLists.txt

### Test Failures
- Check serial output for device errors
- Verify hardware connections
- Review test logs in `results/` directory

## Return Codes

- `0` - All tests passed
- `1` - Build/flash/test failure
- `2` - Invalid arguments or missing prerequisites
