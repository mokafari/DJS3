# DJS3 Phase 2: Analysis & Playlists

## Overview

Three major features for the next development phase:

1. **Beat Analysis Engine** — Detect BPM, beats, key from audio
2. **.odk File System** — Complete the read/write pipeline for pre-analyzed metadata
3. **M3U Playlist Support** — Load/save playlists, playlist-based browsing

---

## 1. Beat Analysis Engine

**Goal:** Analyze tracks to detect BPM, beat positions, and musical key.

### Components Needed

#### 1.1 BPM Detection (`components/analysis/bpm_detector.c`)
- Onset detection (energy-based or spectral flux)
- Autocorrelation for tempo estimation
- Range: 60-180 BPM (typical DJ music)
- Output: BPM float + confidence score

#### 1.2 Beat Grid Generation (`components/analysis/beat_grid.c`)
- Find first downbeat (phase alignment)
- Generate beat positions array
- Handle tempo variations (steady BPM assumption for v1)
- Output: `grid_offset` + beat positions for seek table

#### 1.3 Key Detection (`components/analysis/key_detector.c`)
- Chroma feature extraction
- Key profile matching (Krumhansl-Schmuckler or similar)
- Output: Camelot key_id (0-23)

#### 1.4 Waveform Overview Generation (`components/analysis/waveform_gen.c`)
- Scan full track in chunks
- Extract peak amplitudes
- Downsample to 480 points
- Output: `uint8_t[480]` for overview stripe

#### 1.5 Analysis Task (`main/analysis_task.c`)
- Background FreeRTOS task
- Queue-based: analyze tracks when idle
- Progress reporting to UI
- Save results to .odk files

### Analysis Pipeline
```
MP3 File → Decode chunks → BPM detect → Beat grid → Key detect → Waveform → Save .odk
```

### ESP32 Considerations
- Run on Core 0 (audio playback is on Core 1)
- Use PSRAM for buffers
- Analyze in small chunks to avoid blocking
- Target: ~30-60 seconds per track analysis

---

## 2. .odk File System

**Status:** Format defined in `metadata_format.h`, partial implementation exists.

### Tasks Needed

#### 2.1 ODK Reader (`main/metadata.c` - enhance)
- Load TrackMetadata_t from .odk file
- Validate magic/version
- Check source_size matches MP3 (detect if file changed)
- Already partially implemented

#### 2.2 ODK Writer (`main/metadata.c` - add)
- Create/update .odk files from analysis results
- Atomic write (write to .tmp, then rename)
- Create directory structure: `/sdcard/.opendeck/Music/...`

#### 2.3 Hot Cue Persistence (`main/cue_points.c` - enhance)
- Save hot cues to .odk on set
- Load hot cues on track load
- Partial implementation exists

#### 2.4 VBR Seek Table (`main/metadata.c` - add)
- Generate during analysis
- Use for accurate seeking in VBR MP3s
- 100 points (0%, 1%, 2%... 99%)

#### 2.5 Directory Mirroring
- .odk path mirrors MP3 path
- `/sdcard/Music/Artist/Track.mp3` → `/sdcard/.opendeck/Music/Artist/Track.odk`
- Auto-create parent directories

---

## 3. M3U Playlist Support

**Goal:** Load/save standard M3U playlists, integrate into crate browser.

### Components Needed

#### 3.1 M3U Parser (`components/playlist/m3u_parser.c`)
- Parse .m3u and .m3u8 files
- Handle relative and absolute paths
- Support extended M3U (#EXTINF)
- Extract: track path, duration, title

#### 3.2 M3U Writer (`components/playlist/m3u_writer.c`)
- Create new playlists
- Add/remove tracks
- Save with #EXTINF metadata

#### 3.3 Playlist Browser (`components/ui/src/playlist_view.c`)
- New UI view for playlist browsing
- List available playlists
- Show playlist contents
- Load track from playlist

#### 3.4 Playlist Manager (`main/playlist_manager.c`)
- Scan for .m3u files in /sdcard/Playlists/
- In-memory playlist representation
- Create/edit/delete playlists
- Recent playlists list

#### 3.5 UI Integration
- Add "Playlists" option to main menu / crate view
- Show playlist indicator when playing from playlist
- Quick-save current track to playlist

### M3U Format Support
```
#EXTM3U
#EXTINF:180,Artist - Track Title
/Music/Artist/Track.mp3
#EXTINF:240,Artist2 - Another Track
/Music/Artist2/Another.mp3
```

---

## Task Breakdown for Agent Engine

### Phase 2A: Analysis Foundation (Parallel)
| ID | Task | Deps | Est. Time |
|----|------|------|-----------|
| `bpm-detector` | BPM detection algorithm | - | 2h |
| `waveform-gen` | Waveform overview generator | - | 1h |
| `odk-writer` | .odk file write + directory mirroring | - | 1h |

### Phase 2B: Analysis Pipeline (Sequential after 2A)
| ID | Task | Deps | Est. Time |
|----|------|------|-----------|
| `beat-grid` | Beat grid generation | bpm-detector | 1.5h |
| `key-detector` | Musical key detection | - | 2h |
| `analysis-task` | Background analysis FreeRTOS task | bpm-detector, waveform-gen, odk-writer | 2h |

### Phase 2C: Playlists (Parallel with 2B)
| ID | Task | Deps | Est. Time |
|----|------|------|-----------|
| `m3u-parser` | M3U file parser | - | 1h |
| `m3u-writer` | M3U file writer | - | 45min |
| `playlist-manager` | Playlist scanning & management | m3u-parser | 1h |
| `playlist-view` | Playlist browser UI | playlist-manager | 1.5h |

### Phase 2D: Integration (Final)
| ID | Task | Deps | Est. Time |
|----|------|------|-----------|
| `analysis-ui` | Analysis progress UI + queue display | analysis-task | 1h |
| `playlist-integration` | Wire playlists into crate view | playlist-view | 1h |
| `auto-analyze` | Auto-analyze on track load if no .odk | analysis-task, odk-writer | 1h |

---

## Schedule

### Tonight (02:16 - 06:00)
- Phase 2A: Foundation (3 parallel agents)
- Start Phase 2C: m3u-parser, m3u-writer

### Tomorrow Morning
- Phase 2B: Analysis pipeline completion
- Phase 2C: playlist-manager, playlist-view

### Tomorrow Afternoon
- Phase 2D: Integration
- Testing & bug fixes

---

## Files to Create

```
components/
├── analysis/
│   ├── CMakeLists.txt
│   ├── include/
│   │   ├── bpm_detector.h
│   │   ├── beat_grid.h
│   │   ├── key_detector.h
│   │   └── waveform_gen.h
│   └── src/
│       ├── bpm_detector.c
│       ├── beat_grid.c
│       ├── key_detector.c
│       └── waveform_gen.c
├── playlist/
│   ├── CMakeLists.txt
│   ├── include/
│   │   ├── m3u_parser.h
│   │   ├── m3u_writer.h
│   │   └── playlist_manager.h
│   └── src/
│       ├── m3u_parser.c
│       ├── m3u_writer.c
│       └── playlist_manager.c
components/ui/
├── include/
│   └── playlist_view.h      (new)
└── src/
    └── playlist_view.c      (new)
main/
├── analysis_task.c          (new)
└── include/
    └── analysis_task.h      (new)
```

---

## Notes

- BPM detection on ESP32 is challenging — may need simplified algorithm
- Consider offloading heavy analysis to PC tool (create .odk on desktop, sync to SD)
- Key detection is CPU-intensive — could be optional or PC-only for v1
- M3U is simpler and can be fully implemented on-device
