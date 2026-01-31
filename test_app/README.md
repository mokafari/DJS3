# CDJ DJ Deck Test Suite

Automated tests for the ESP32 CDJ DJ Deck project using the ESP-IDF Unity test framework.

## Test Categories

### Duration Calculation Tests (`test_duration_calculation.c`)
- Validates MP3 duration calculation from file size and bitrate
- Regression tests for the "track too long" bug (fixed 128kbps assumption)
- Edge cases: zero bitrate, small files, large files

### Waveform View Tests (`test_waveform_view.c`)
- Resolution divider calculations
- Binned peak detection (transient preservation)
- Scroll delta logic (incremental vs full redraw)
- Bar height calculations
- Display cache operations

### Audio Player Tests (`test_audio_player.c`)
- Player state transitions
- Position calculation from bytes played
- Gain/volume application with clamping
- Ring buffer operations (write, read, wrap, full condition)

## Running Tests

### Build and Flash Test App

```bash
# From project root
cd test_app
source ~/.espressif/v5.5.2/esp-idf/export.sh
idf.py build flash monitor
```

### Quick Run (using dev.py)

```bash
# From project root
python3 dev.py --test
```

## Test Output

Successful run example:
```
I (xxx) test_runner: ========================================
I (xxx) test_runner: CDJ DJ Deck Test Suite
I (xxx) test_runner: ========================================

test_duration_calculation.c:55:Duration calculation 320kbps:PASS
test_duration_calculation.c:66:Duration calculation 192kbps:PASS
test_duration_calculation.c:77:Duration calculation 128kbps:PASS
...

-----------------------
20 Tests 0 Failures 0 Ignored
OK

I (xxx) test_runner: ========================================
I (xxx) test_runner: Test Suite Complete
I (xxx) test_runner: ========================================
```

## Adding New Tests

1. Create a new test file: `test_<component>.c`
2. Use Unity macros:
   ```c
   #include "unity.h"
   
   TEST_CASE("Test description", "[tag1][tag2]")
   {
       TEST_ASSERT_EQUAL(expected, actual);
   }
   ```
3. Add registration function (for organization):
   ```c
   void register_<component>_tests(void) {
       // Tests auto-registered by TEST_CASE macro
   }
   ```
4. Add to `test_runner.c`:
   ```c
   extern void register_<component>_tests(void);
   // ...
   register_<component>_tests();
   ```
5. Update `main/CMakeLists.txt` with new source file

## Test Tags

- `[duration]` - Duration calculation tests
- `[waveform]` - Waveform rendering tests
- `[audio]` - Audio player tests
- `[resolution]` - Resolution divider tests
- `[peak]` - Peak detection tests
- `[scroll]` - Scroll/navigation tests
- `[edge]` - Edge case tests
- `[regression]` - Regression tests for fixed bugs

## Hardware Tests

Some tests require hardware and are disabled by default:
- SD card access tests
- I2S output tests
- Touch input tests

Enable by uncommenting in `test_runner.c`.

