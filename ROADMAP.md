# DJS3 Development Roadmap

## Completed

### Phase 1: Waveform UI Enhancements ✅
- Overview waveform stripe
- Touch-to-seek on overview
- Beat grid overlay
- Nudge animation effect

### Phase 2: Analysis & Playlists ✅
- BPM detection algorithm
- Key detection (Camelot)
- Waveform overview generation
- Beat grid generation
- Background analysis task
- .odk metadata read/write
- M3U playlist support
- Playlist browser UI

---

## Phase 3: Audio Effects & DSP

### 3.1 Filter Effect (`components/fx/filter.c`)
- High-pass / low-pass resonant filter
- Cutoff frequency control (20Hz - 20kHz)
- Resonance control
- Touch-controlled frequency sweep

### 3.2 Echo/Delay Effect (`components/fx/echo.c`)
- Tempo-synced delay (1/4, 1/2, 3/4, 1 beat)
- Feedback control
- Wet/dry mix
- Ping-pong stereo mode

### 3.3 EQ (3-band) (`components/fx/eq.c`)
- Low/Mid/High frequency bands
- Kill switches per band
- Isolator mode

### 3.4 DSP Pipeline (`main/dsp_pipeline.c`)
- Chain effects in configurable order
- Bypass per effect
- Master limiter for output protection

---

## Phase 4: Performance Features

### 4.1 Cue Point Visuals (`components/ui/src/cue_markers.c`)
- Draw cue markers on waveform
- Color-coded by cue number
- Cue labels on overview stripe
- Memory cue (auto-save last position)

### 4.2 Loop Enhancements (`main/loop_control.c`)
- Auto-loop (1/2/4/8/16 beats)
- Loop roll (momentary loop)
- Loop move (shift loop position)
- Loop resize (halve/double)
- Visual loop region on waveform

### 4.3 Slip Mode (`main/slip_mode.c`)
- Timeline continues in background
- Scratching/looping returns to correct position
- Visual indicator for slip offset

### 4.4 Hot Cue Enhancements (`main/cue_points.c`)
- 8 hot cues (matching .odk format)
- Cue + play mode
- Delete cue function
- Cue color selection

---

## Phase 5: Sync & Connectivity

### 5.1 MIDI Output (`components/sync/midi_out.c`)
- Send MIDI clock
- Send transport (start/stop)
- Send CC for BPM/position
- USB MIDI device class

### 5.2 MIDI Input (`components/sync/midi_in.c`)
- Receive MIDI clock (sync to external)
- Map MIDI CC to controls
- MIDI learn mode

### 5.3 Ableton Link (`components/sync/ableton_link.c`)
- Link session join
- Tempo sync
- Phase alignment
- Start/stop sync

### 5.4 Analog Sync (`components/sync/analog_sync.c`)
- DIN sync output (24 ppqn)
- Clock divider options
- Voltage level configuration

---

## Phase 6: Hardware Integration

### 6.1 Jog Wheel Driver (`main/jog_wheel.c`)
- Capacitive touch detection
- Velocity sensing
- Scratch mode vs nudge mode
- Vinyl mode (spin direction)

### 6.2 LED Controller (`main/led_controller.c`)
- Play/pause indicator
- Cue button LEDs
- Loop active indicator
- Beat flash sync
- WS2812 strip support (optional)

### 6.3 External Controllers (`main/ext_controller.c`)
- Generic HID support
- DJ controller mapping
- Button matrix scanning
- Rotary encoder support

### 6.4 Pitch Fader (`main/pitch_fader.c`)
- ADC calibration routine
- Center detent detection
- Range selection (±4%, ±8%, ±16%, ±50%)
- Master tempo mode

---

## Phase 7: Polish & UX

### 7.1 Settings Menu (`components/ui/src/settings_view.c`)
- Theme selection
- Audio settings (buffer size, output)
- Display settings (brightness)
- Analysis settings (auto-analyze on/off)
- About/version info

### 7.2 Preferences Storage (`main/preferences.c`)
- NVS-based persistence
- Per-track preferences (cues, loops)
- Global settings
- Export/import settings

### 7.3 Track History (`main/track_history.c`)
- Recently played tracks
- Play count tracking
- Last played timestamp
- History browser UI

### 7.4 Search (`components/ui/src/search_view.c`)
- Text search across library
- BPM range filter
- Key filter (compatible keys)
- On-screen keyboard

---

## Phase 8: Advanced Features

### 8.1 Recording (`main/recorder.c`)
- Record master output to SD
- WAV format (16-bit 44.1kHz)
- Auto-split by track
- Recording level meter

### 8.2 Auto-DJ (`main/auto_dj.c`)
- Queue management
- Auto-mix at track end
- BPM matching transitions
- Key-compatible selection

### 8.3 Track Preparation (`main/track_prep.c`)
- Pre-analyze tracks before gig
- Batch analysis UI
- Verify all tracks have .odk
- Missing file detection

### 8.4 Performance Mode (`main/performance.c`)
- Simplified UI for live use
- Large waveform view
- Essential controls only
- High contrast mode

---

## Implementation Notes

### Memory Budget
- PSRAM: 8MB available
- Audio buffer: ~512KB
- Waveform cache: ~200KB
- UI: ~1MB
- Effects: ~500KB
- Remaining: ~5.8MB for analysis/recording

### CPU Budget
- Core 0: Audio decode + DSP (~60% utilization)
- Core 1: UI + background tasks (~40% utilization)

### Priority Order
1. Phase 3 (Effects) - Essential for DJ use
2. Phase 4 (Performance) - Core DJ features
3. Phase 7 (Polish) - User experience
4. Phase 5 (Sync) - Professional use
5. Phase 6 (Hardware) - Physical controls
6. Phase 8 (Advanced) - Nice to have
