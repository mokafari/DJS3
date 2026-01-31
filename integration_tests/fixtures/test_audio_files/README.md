# Test Audio Files

This directory should contain test MP3 files for integration testing.

## Required Test Files

- `test_tone_1khz.mp3` - 1kHz sine wave, 5 seconds duration
- `test_silence.mp3` - Silent audio for timing tests
- `test_stereo.mp3` - Stereo test file

## Generating Test Files

Test audio files can be generated using tools like:
- `ffmpeg` - Generate test tones and silence
- `sox` - Audio processing and generation

### Example: Generate 1kHz test tone

```bash
ffmpeg -f lavfi -i "sine=frequency=1000:duration=5" -ar 44100 -ac 2 test_tone_1khz.mp3
```

### Example: Generate silence

```bash
ffmpeg -f lavfi -i "anullsrc=channel_layout=stereo:sample_rate=44100" -t 5 test_silence.mp3
```

## Usage

Place test MP3 files in this directory and copy them to the SD card or USB drive used for testing.

