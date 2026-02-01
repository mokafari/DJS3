# Integration Tests

This directory contains integration tests for hardware and software components of the ESP32-S3 DJ controller.

## Structure

```
integration_tests/
├── test_apps/              # ESP-IDF test applications
│   ├── audio_integration/
│   ├── display_integration/
│   ├── storage_integration/
│   ├── controls_integration/
│   ├── dsp_integration/
│   └── e2e_workflows/
├── host_tests/             # Python pytest test scripts
├── fixtures/               # Test data and helpers
│   ├── test_audio_files/
│   ├── test_images/
│   └── helpers/
├── pytest.ini             # pytest configuration
├── conftest.py            # pytest fixtures
└── run_tests.sh           # Test runner script
```

## Quick Start

### Prerequisites

1. ESP-IDF v5.5.2 or compatible
2. Python 3 with pytest and pytest-embedded:
   ```bash
   pip install pytest pytest-embedded pytest-embedded-serial-esp
   ```

### Running Tests

#### Single Test App

```bash
cd integration_tests/test_apps/audio_integration
idf.py build flash monitor
```

#### All Tests (pytest)

```bash
cd integration_tests
pytest host_tests/ -v
```

#### Using Test Runner

```bash
cd integration_tests
./run_tests.sh
```

## Test Categories

- **Audio Integration**: I2S, MP3 playback, waveform extraction
- **Display Integration**: QSPI communication, rendering, touch input
- **Storage Integration**: SD card, USB host, track scanning
- **Controls Integration**: GPIO buttons, jog wheel, pitch encoder
- **DSP Integration**: Resampler, 3-band EQ, soft limiter, pitch control
- **End-to-End Workflows**: Complete user workflows

## Documentation

See [TEST_PLAN.md](TEST_PLAN.md) for detailed test plan and coverage matrix.

## Test Fixtures

Place test MP3 files in `fixtures/test_audio_files/` and copy to SD card for storage tests.

See `fixtures/test_audio_files/README.md` for instructions on generating test files.

