# Integration Test Plan

## Overview

This document describes the integration testing strategy for the ESP32-S3 DJ controller project.

## Test Coverage Matrix

| Component | Unit Tests | Integration Tests | E2E Tests |
|-----------|-----------|-------------------|-----------|
| Audio Output (I2S) | ✅ | ✅ | ✅ |
| Audio Player (MP3) | ✅ | ✅ | ✅ |
| Display (NV3041A) | ✅ | ✅ | ✅ |
| Touch (GT911) | - | ✅ | ✅ |
| Storage (SD/USB) | - | ✅ | ✅ |
| Track Database | ✅ | ✅ | ✅ |
| Controls (GPIO) | - | ✅ | ✅ |
| Pitch Control | - | ✅ | ✅ |
| Cue Points | ✅ | - | ✅ |
| Loop Control | ✅ | - | ✅ |
| UI System | ✅ | - | ✅ |

## Hardware Requirements

### Required Hardware
- ESP32-S3 development board (JC4827W543)
- USB cable for programming and serial communication
- SD card with test MP3 files (optional, for storage tests)

### Optional Hardware
- External DAC (PCM5102A) for audio output verification
- Test fixtures for button/encoder simulation

## Test Execution Procedures

### Running Individual Test Apps

1. Navigate to test app directory:
   ```bash
   cd integration_tests/test_apps/audio_integration
   ```

2. Build and flash:
   ```bash
   idf.py build flash
   ```

3. Monitor output:
   ```bash
   idf.py monitor
   ```

### Running All Tests with pytest

1. Navigate to integration_tests directory:
   ```bash
   cd integration_tests
   ```

2. Run all tests:
   ```bash
   pytest host_tests/ -v
   ```

3. Run specific test category:
   ```bash
   pytest host_tests/test_audio_playback.py -v
   ```

### Using Test Runner Script

```bash
cd integration_tests
./run_tests.sh
```

## Test Categories

### 1. Audio Integration Tests
- I2S initialization and configuration
- Audio output verification
- MP3 playback integration
- Waveform extraction

### 2. Display Integration Tests
- Display initialization
- QSPI communication
- Rendering verification
- Touch controller integration

### 3. Storage Integration Tests
- SD card mounting
- USB host detection
- File system access
- Track scanning and metadata

### 4. Controls Integration Tests
- GPIO button configuration
- Jog wheel encoder
- Pitch control encoder
- Control event handling

### 5. End-to-End Workflow Tests
- Complete system initialization
- Track loading workflow
- Playback workflow
- Cue point workflow
- Loop control workflow
- Pitch control workflow

## Test Results

Test execution logs and results should be stored in `integration_tests/results/` directory.

## Continuous Integration

Integration tests can be integrated into CI/CD pipelines using:
- pytest-embedded for test automation
- GitHub Actions or similar CI platforms
- Hardware-in-the-loop test fixtures

## Troubleshooting

### Common Issues

1. **Storage tests fail**: Ensure SD card is inserted and formatted (FAT32)
2. **Display tests fail**: Check QSPI connections and power supply
3. **Audio tests fail**: Verify I2S pin connections
4. **Controls tests skip**: Check board_config.h for pin configuration

### Debug Mode

Enable verbose logging in test apps by setting log level:
```c
esp_log_level_set("*", ESP_LOG_DEBUG);
```

## Future Enhancements

- Automated test fixture for button/encoder simulation
- Performance benchmarking tests
- Stress testing (long-duration playback)
- Multi-device synchronization tests

