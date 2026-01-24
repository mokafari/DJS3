# P4-DJ Deck: Project Vision Document

**Version:** 1.0  
**Last Updated:** December 2024  
**Project Status:** Concept & Prototype Phase

---

## Executive Summary

The **P4-DJ Deck** is a standalone, boutique DJ controller and performance instrument designed around the ESP32-P4 microcontroller. Unlike traditional DJ controllers that require a laptop, this device operates completely independently, loading music directly from USB storage and providing professional-grade audio manipulation through a unique granular synthesis engine.

**Core Philosophy:** Early 2000s industrial aesthetic meets modern embedded performance. A rugged, repairable, open-source instrument that bridges the gap between digital DJing and hardware music production.

---

## Product Vision

### The "Hybrid Deck" Concept

This is not just a media player—it's a **performance instrument** designed to integrate seamlessly with drum machines, modular synthesizers, and other hardware instruments. The deck acts as both a standalone DJ tool and a sync-capable audio source for hybrid electronic music setups.

### Design Aesthetic: "Cyber-Industrial"

- **Visual Language:** High-contrast monochrome HUD (Heads-Up Display) reminiscent of early 2000s test equipment
- **Physical Form:** Resin-printed unibody "monolith" with exposed mechanical switches and industrial-grade controls
- **User Experience:** Tactile, immediate, and transparent—every control has a clear, audible effect

### Target Market

- **Primary:** Electronic music producers and DJs who value standalone operation and hardware integration
- **Secondary:** Synth enthusiasts and modular users seeking sync-capable audio sources
- **Tertiary:** Open-source hardware community and makers who appreciate repairable, hackable devices

---

## Core Features

### 1. Standalone Operation

- **No Laptop Required:** Complete independence from computers
- **USB Host Support:** Direct playback from USB flash drives (MP3, FLAC, WAV)
- **On-Device Analysis:** Automatic BPM detection and beat grid generation without external software
- **Metadata Storage:** Beat grids and analysis data written directly to audio file metadata (ID3/FLAC tags)

### 2. Granular Synthesis Engine

**The "Texture Synthesizer" Hidden Inside:**

Unlike standard time-stretching algorithms that try to sound "natural," this engine exposes the granular synthesis parameters, allowing creative sound design:

- **Grain Size Control:** From metallic ringing (10ms) to lush, smeared textures (200ms)
- **Density/Overlap:** Adjustable from stuttering gates to layered, reverb-like pads
- **Jitter/Randomness:** Add controlled chaos for glitchy, experimental effects
- **Beat-Sync Mode:** Stretch audio beyond tempo while maintaining perfect beat alignment

**Sound Characteristics:**
- **Metallic Ringing:** Short grains create robotic, comb-filter-like tones
- **Lush Scapes:** High-density overlapping grains create ambient textures
- **Glitchy Madness:** Jitter and randomness create experimental, stuttering effects
- **Early Akai Sampler Aesthetic:** Intentional artifacts that sound musical, not broken

### 3. Hybrid Hardware Integration

**MIDI Sync:**
- Master Clock generation (MIDI DIN output)
- Slave mode (follows external MIDI clock)
- Real-time tempo adjustment linked to pitch fader

**Analog Sync:**
- 3.5mm pulse output for "dumb" gear (Korg Volcas, Pocket Operators)
- Programmable swing/groove for rigid sequencers

**Audio Input:**
- Line-level input for processing external sources (drum machines, synths)
- DSP effects (filters, flanger) applied to external audio

### 4. Professional Controls

**Tactile Interface:**
- **Long-Throw Pitch Fader:** 100mm slide potentiometer for precise manual beatmatching
- **Mechanical Key Switches:** Kailh Choc or Cherry MX switches (50M+ cycle lifespan)
- **Nudge Buttons:** Large, responsive buttons for micro pitch adjustments
- **Capacitive Touch Strip:** 60mm horizontal strip for "needle drop" seeking
- **Rotary Encoder:** Large aluminum knob for library browsing

**Control Layout Philosophy:**
- Vertical "slice" layout (similar to Eurorack modules)
- Ergonomic hand positioning for performance
- Shift layers for advanced features (granular parameters)

### 5. High-Contrast HUD Interface

**Visual Design:**
- **Color Palette:** Monochrome with selectable "phosphor" colors (Amber, Cyan, Green)
- **Typography:** Monospaced fonts (JetBrains Mono, VCR OSD style)
- **Layout:** Three-zone display (Waveform, Telemetry, Metadata)
- **Effects:** CRT-style ghosting/trails for retro aesthetic
- **Performance:** 60 FPS locked refresh using P4's 2D accelerator (PPA)

**Key UI Elements:**
- **Waveform Display:** Vertical bar graph style (spectrum analyzer aesthetic)
- **BPM/Pitch Display:** Large, readable numbers for manual beatmatching
- **Phase Error Bar:** Visual feedback for beat alignment
- **Grid Lines:** Dotted vertical lines showing beat positions
- **Touch Feedback:** Ghost cursor showing scrub position

---

## Hardware Architecture

### Compute Module

**ESP32-P4 + ESP32-C6 Companion Board:**
- **CPU:** Dual-core RISC-V @ 400MHz (ESP32-P4)
- **Memory:** 32MB PSRAM (critical for audio buffering)
- **Wireless:** ESP32-C6 companion chip (Wi-Fi 6, Bluetooth) via SDIO
- **Display:** MIPI-DSI interface (supports up to 1024x600 @ 60fps)
- **USB:** USB 2.0 Host (High Speed, 480Mbps)

**Why This Combination:**
- P4 lacks native Wi-Fi/Bluetooth (separated for performance)
- C6 handles wireless communication without interfering with audio processing
- 32MB PSRAM enables "ramdisk mode" (entire MP3 files loaded into RAM)

### Audio Subsystem

**External DAC (Required):**
- **Primary:** PCM5102A or ES8388 (24-bit/96kHz I2S output)
- **Reason:** Onboard audio codecs are low-fidelity, unsuitable for professional use
- **Output:** RCA line-level outputs (standard DJ mixer compatibility)

**Audio Engine:**
- **Sample Rate:** 44.1kHz (CD quality)
- **Buffer Size:** <128 samples (~2.9ms latency) for responsive scratching
- **Processing:** Dual-core architecture (Core 0: Audio, Core 1: UI/Logic)

### Control Interface

**Physical Controls:**
- **Pitch Fader:** 100mm slide potentiometer (long throw for precision)
- **Nudge Buttons:** 2x mechanical switches (Linear, no click)
- **Cue/Play:** Mechanical key switches
- **Touch Strip:** Capacitive PCB pads (60mm horizontal)
- **Rotary Encoder:** Large aluminum knob (clickable)
- **Grid Adjust:** 2x small buttons (shift grid left/right)

**MIDI Interface:**
- **MIDI DIN Output:** Standard 5-pin DIN connector
- **Hardware:** UART → 220Ω resistor → DIN-5 jack
- **Sync Pulse:** 3.5mm jack for analog sync (5V pulses)

### Enclosure Design

**Resin-Printed Unibody:**
- **Material:** ABS-Like Resin (80%) + Tenacious Resin (20%) for durability
- **Finish:** Matte clear coat or Plasti-Dip for premium feel
- **Assembly:** Slide-in PCB rails, nut traps for bottom plate
- **Aesthetic:** Rounded corners, integrated tilt, debossed text labels

**Production Strategy:**
- Large-format resin printer (Elegoo Jupiter, Phrozen Mega)
- Batch printing (4-6 cases per build plate, same time as 1)
- Post-processing: Alcohol wash, UV cure, sanding, coating

**PCB Faceplate:**
- Matte black FR4 with ENIG (gold) text
- Scratch-resistant, premium appearance
- Integrated light pipes for LED backlighting

---

## Software Architecture

### Core Components

**1. Audio Engine (Core 0 - High Priority)**
- **File Loading:** USB Host → Decode MP3/FLAC → PSRAM buffer
- **Granular Synthesis:** Custom grain reader with configurable parameters
- **Time-Stretching:** Beat-synced granular engine (not WSOLA)
- **DSP Processing:** EQ, filters, effects
- **I2S Output:** Low-latency audio streaming

**2. UI & Logic (Core 1)**
- **Graphics:** LVGL library with custom "High-Contrast HUD" theme
- **Touch Input:** Capacitive strip and encoder handling
- **Beat Detection:** Energy-based BPM analysis (background thread)
- **Sync Logic:** MIDI clock generation/following
- **File System:** USB mass storage management

**3. Background Workers**
- **Beat Analysis:** Scans audio files for BPM and grid (first 30 seconds)
- **Metadata Management:** Reads/writes ID3/FLAC tags
- **Sync Communication:** ESP32-C6 wireless sync (Ableton Link, UDP)

### Granular Synthesis Algorithm

**Core Concept:**
Separate the **Clock** (when to restart a grain) from the **Pitch** (how fast to play audio inside the grain).

**Key Parameters:**

1. **Grain Size** (10ms - 200ms)
   - Small: Metallic ringing, robotic tones
   - Large: Choppy, stuttery effects

2. **Density/Overlap** (25% - 300%+)
   - Low: Gated, stuttering silence
   - High: Smear, lush pads, reverb-like textures

3. **Jitter** (Randomness)
   - Adds controlled chaos for glitch effects
   - Random offset to grain start position

4. **Beat Sync**
   - Grain restarts only on beat boundaries
   - File position traverses independently (time-stretch)
   - Creates "frozen" textures that morph on each beat

**Implementation:**
- Custom C++ engine (not SoundTouch or standard libraries)
- Optimized for ESP32-P4 DSP instructions
- Real-time parameter adjustment via controls

### Beat Detection & Grid Generation

**Algorithm:**
1. **Downsample:** 44.1kHz → 11kHz mono (8x reduction)
2. **Low-Pass Filter:** 150Hz cutoff (isolate kick/bass)
3. **Energy Detection:** Square samples, calculate moving average
4. **Threshold:** Mark beats when energy spikes 1.5x above average
5. **BPM Calculation:** Find most common interval between beats
6. **Downbeat Detection:** Assume first detected beat is downbeat (user-adjustable)

**Storage:**
- Write to file metadata (ID3 TXXX frame or FLAC Vorbis comment)
- Format: `P4_DATA: BPM=124.00;OFFSET=450;SIG=4/4;`
- If missing, analyze before playback (15-20 seconds for 5-minute track)

**User Correction:**
- **Grid Adjust Buttons:** Shift grid left/right by 1ms increments
- **Tap BPM:** Manual override if auto-detection fails
- **Visual Feedback:** Metronome click, phase error bar

### File Management

**Ramdisk Mode (Files < 20MB):**
- Load entire file into PSRAM
- Close USB file handle (USB can be removed)
- Atomic save: Write to temp file, verify, rename
- Zero corruption risk

**Stream Mode (Files > 20MB):**
- Ring buffer (4MB) for WAV/FLAC
- Append metadata to end of file (APEv2 tag)
- Avoids rewriting large files

**Supported Formats:**
- MP3 (up to 320 kbps)
- FLAC (compressed)
- WAV/AIFF (streaming mode)

---

## Production Strategy

### Manufacturing Approach

**Small-Batch Boutique Production:**
- Target: 50-100 units initial run
- Team: 3-5 person operation
- Assembly: Hand-assembled, quality-controlled

**Component Sourcing:**

| Component | Choice | Est. Cost | Notes |
|-----------|--------|-----------|-------|
| Compute | ESP32-P4 Module + 32MB | $15-20 | Pre-made module |
| Screen | 5" MIPI IPS | $20-30 | Smartphone quality |
| PCB (x2) | Mainboard + Faceplate | $10 | Matte black finish |
| Audio | PCM5102A + RCA Jacks | $5 | Hi-Fi output |
| Controls | 6x Mech Switches + Slider | $8 | Premium feel |
| Case | Resin Print + Hardware | $10 | Unibody style |
| **Total** | | **~$70-80** | **Sell: $250-350** |

**Assembly Workflow:**
1. **Print Cases:** Batch resin printing (24 shells/day @ 24/7)
2. **PCB Assembly:** SMT components (can outsource)
3. **Module Integration:** Plug ESP32-P4 module into carrier board
4. **Control Installation:** Hot-swap sockets for switches (no soldering)
5. **Screen Mounting:** Adhesive tape, ribbon cable connection
6. **Final Assembly:** Slide PCB into rails, bolt bottom plate
7. **Testing:** Audio output, controls, sync functionality

### Repairability & Open Source

**Design Principles:**
- **Modular:** ESP32-P4 module is replaceable (not soldered)
- **Standard Parts:** Cherry MX switches, standard potentiometers
- **Documentation:** Complete schematics, firmware source code
- **Community:** Open-source firmware, hackable hardware

**Selling Points:**
- "If you can code C++, you can hack this deck"
- Standard components = easy repairs
- No proprietary software required

---

## Technical Specifications

### Performance Targets

**Audio:**
- **Latency:** <10ms (128 sample buffer)
- **Frequency Response:** 20Hz - 20kHz (±0.5dB)
- **THD+N:** <0.01% @ 1kHz
- **Dynamic Range:** >100dB

**Processing:**
- **BPM Detection:** 15-20 seconds for 5-minute track
- **UI Refresh:** 60 FPS locked
- **Granular Engine:** Real-time parameter adjustment, no dropouts

**Storage:**
- **USB Support:** FAT32, exFAT
- **File Size:** Up to 4GB per file (theoretical)
- **Library Size:** Limited by USB capacity

### Connectivity

**Audio:**
- 2x RCA Line Outputs (L/R)
- 1x 3.5mm Line Input (optional)

**Control:**
- 1x MIDI DIN Output (5-pin)
- 1x 3.5mm Sync Pulse Output
- 1x USB-C Power Input
- 1x USB-A Host Port (for storage)

**Wireless:**
- Wi-Fi 6 (via ESP32-C6)
- Bluetooth (via ESP32-C6)
- Ableton Link support (future)

---

## Development Roadmap

### Phase 1: Audio Engine Prototype (Weeks 1-4)

**Goal:** Prove granular synthesis on ESP32-S3 (compatible with P4)

**Hardware:**
- ESP32-S3 dev board
- PCM5102A DAC module
- 3x Potentiometers (grain size, speed, position)

**Software:**
- I2S audio output (sine wave test)
- Ramdisk loader (MP3 → PCM in PSRAM)
- Basic granular buffer reader
- Real-time parameter control

**Milestone:** Load song, turn knob, hear metallic/glitchy textures

**Status:** ✅ Code provided in conversation

### Phase 2: MVP Firmware (Weeks 5-10)

**Goal:** Complete standalone player with UI and sync

**Hardware:**
- ESP32-P4 Function EV Board
- 5" MIPI screen
- Breadboard controls (switches, fader)

**Software:**
- High-Contrast HUD UI (LVGL)
- Beat detection algorithm
- Beat-sync granular engine
- MIDI clock output
- USB Host file loading
- Metadata read/write

**Milestone:** Standalone deck playing from USB, syncing to drum machine

### Phase 3: Production Design (Weeks 11-16)

**Goal:** Boutique product ready for assembly

**Hardware:**
- Custom PCB design (KiCad)
  - Carrier board for P4 module
  - Faceplate PCB (black FR4, gold text)
  - Nut traps, hot-swap sockets
- Resin enclosure design (Fusion 360/Blender)
- Test prints on large resin printer
- Clear resin keycaps + LED backlighting

**Software:**
- Production firmware optimization
- Factory calibration routines
- User manual and documentation

**Milestone:** 5 beta units assembled and tested

### Phase 4: Production Run (Weeks 17-20)

**Goal:** First production batch

**Activities:**
- Component procurement (50-100 units)
- Batch case printing
- PCB assembly (SMT)
- Module integration
- Quality control testing
- Packaging and shipping

**Milestone:** First customers receive units

---

## Design Philosophy

### "Less is More"

**Control Philosophy:**
- Nudge buttons instead of complex jog wheel
- Shift layers for advanced features (granular parameters)
- Physical controls over touch-screen menus
- Immediate, tactile feedback

**Aesthetic Philosophy:**
- Industrial utility over sleek consumer design
- Exposed mechanics (hex screws, visible PCB)
- High-contrast, readable displays
- Rugged, repairable construction

### "Standalone First"

**Core Principle:**
- No external software dependencies
- On-device analysis and metadata storage
- Portable USB stick (works on any P4-DJ deck)
- Open-source firmware (community-driven)

### "Creative Artifacts"

**Audio Philosophy:**
- Intentional granular artifacts (not hidden)
- Exposed synthesis parameters (not locked)
- Musical glitches and textures
- Early 2000s sampler aesthetic

---

## Future Enhancements

### Potential Features (Post-MVP)

**Software:**
- Ableton Link integration (wireless sync)
- Multi-deck support (2+ units sync)
- Custom effect chains
- Sample recording and looping
- Wi-Fi library streaming

**Hardware:**
- Dual-deck version (2 players in one unit)
- Modular expansion (Eurorack integration)
- CV/Gate outputs for modular synths
- Motorized fader (auto-beatmatching)

**Community:**
- Plugin system for custom effects
- User-contributed UI themes
- Hardware mod guides
- Firmware forks and variants

---

## Success Metrics

### Technical Success

- [ ] Audio latency <10ms
- [ ] 60 FPS UI refresh rate
- [ ] BPM detection accuracy >95%
- [ ] Zero audio dropouts during granular manipulation
- [ ] MIDI sync jitter <1ms

### Product Success

- [ ] 50+ units sold in first year
- [ ] Community firmware contributions
- [ ] Positive reviews from DJ/producer community
- [ ] Open-source adoption (forks, variants)
- [ ] Repairability demonstrated (user repairs documented)

### User Experience Success

- [ ] Standalone operation verified (no laptop needed)
- [ ] Hardware sync tested with common drum machines
- [ ] Granular textures praised for creativity
- [ ] UI readability confirmed in low-light club environments
- [ ] Build quality exceeds expectations for price point

---

## Risk Assessment

### Technical Risks

**High:**
- MIPI-DSI screen driver complexity (mitigation: use proven EV board)
- USB Host file system reliability (mitigation: ramdisk mode, atomic saves)
- Granular engine CPU load (mitigation: DSP optimization, dual-core)

**Medium:**
- Beat detection accuracy on complex music (mitigation: user correction tools)
- MIDI sync stability (mitigation: hardware clock, smoothing algorithms)
- Resin print quality consistency (mitigation: post-processing, quality control)

**Low:**
- Component availability (mitigation: multiple suppliers, standard parts)
- Firmware bugs (mitigation: extensive testing, open-source review)

### Business Risks

**High:**
- Small market size (mitigation: niche positioning, community building)
- Production costs vs. pricing (mitigation: careful BOM management)

**Medium:**
- Competition from established brands (mitigation: unique features, open-source)
- Support burden (mitigation: documentation, community forums)

---

## Conclusion

The P4-DJ Deck represents a unique convergence of embedded systems engineering, audio DSP, and industrial design. By combining the raw processing power of the ESP32-P4 with creative granular synthesis algorithms and a rugged, repairable form factor, this project aims to create a new category of standalone performance instrument.

**Key Differentiators:**
1. **Standalone Operation:** No laptop, no proprietary software
2. **Creative Granular Engine:** Exposed parameters for sound design
3. **Hardware Integration:** MIDI sync, analog pulse, audio processing
4. **Boutique Quality:** Resin unibody, mechanical switches, premium feel
5. **Open Source:** Hackable, repairable, community-driven

**Vision Statement:**
*"A professional-grade DJ deck that feels like early 2000s industrial equipment, sounds like a granular synthesizer, and works like a modern embedded system—all without requiring a computer."*

---

**Document Status:** Living document, updated as project evolves  
**Next Review:** After Phase 1 completion  
**Contact:** [Project maintainer/team]

