# Waveform Performance Tuning & Stability Plan

## Problem Analysis

### Issue 1: Waveform Changes Behind Playhead

**Root Cause:** The waveform is computed ON-THE-FLY during MP3 decoding. The `waveform_ring_buffer` is circular (4KB), so as the decoder writes new data, old positions get overwritten when the buffer wraps. This causes visual instability.

### Issue 2: Choppy Display

**Root Causes:**

- Ring buffer index wrapping causes erratic scroll delta calculations
- Full screen redraws triggered too frequently
- 480 columns at full resolution is expensive

### Issue 3: No Tuning Parameters

**Root Cause:** Hard-coded values with no way to adjust resolution/performance tradeoff

## Architecture Diagram

```javascript
CURRENT (Problematic):
┌────────────────────────────────────────────────────────────────────────┐
│ Decoder Task                                                           │
│ ┌─────────────┐    ┌─────────────────────────────────┐                │
│ │ MP3 Decode  │───▶│ waveform_ring_buffer (4KB)      │◀── WRAPS!     │
│ │ (computes   │    │ Peaks constantly overwritten    │                │
│ │  peaks)     │    └─────────────────────────────────┘                │
│ └─────────────┘                    │                                   │
└────────────────────────────────────┼───────────────────────────────────┘
                                     │
                     audio_player_get_waveform() extracts 480 bytes
                     centered on rb_read_head (which also wraps!)
                                     │
                                     ▼
┌────────────────────────────────────────────────────────────────────────┐
│ Waveform View                                                          │
│ - wave_index wraps causing scroll delta bugs                           │
│ - Past data can change because ring buffer overwrites                  │
└────────────────────────────────────────────────────────────────────────┘
```



## Proposed Solutions

### Solution 1: Variable Resolution with Configurable Bin Size

Add tunable parameters to reduce rendering workload:

```c
// New configurable parameters
static int waveform_bin_size = 1;       // 1 = full res, 2 = half res, etc.
static int waveform_visible_bars = 480; // Reduced from 480 if bin_size > 1
```

**Benefits:**

- `bin_size = 2` means 240 bars instead of 480 (50% CPU reduction)
- `bin_size = 4` means 120 bars (75% CPU reduction)
- Transients still visible because we take MAX peak within each bin

### Solution 2: Monotonic Position Counter (Fix Wrap-Around Bug)

The current issue is `wave_index = rb_read_head / WAVEFORM_RATIO` wraps when the ring buffer wraps. Fix by using a **monotonically increasing** sample counter:

```c
// In audio_player.cpp
static volatile uint64_t total_samples_played = 0;  // NEVER wraps

size_t audio_player_get_waveform_index(void) {
    return (size_t)(total_samples_played / SAMPLES_PER_WAVEFORM_BIN);
}
```

**Benefits:**

- Scroll delta always positive during playback (no erratic full redraws)
- Seek detection still works (large jump in counter)

### Solution 3: Stable Display Buffer (Fix Data Changing Behind Playhead)

Instead of reading directly from the circular `waveform_ring_buffer`, maintain a separate **linear display cache** that doesn't get overwritten:

```c
// In waveform_view.c
static uint8_t *display_cache = NULL;     // Linear buffer, 480 bytes
static size_t display_cache_start = 0;    // Start index in track

void update_display_cache(const uint8_t *new_data, size_t new_index) {
    // Only update the RIGHT side (future) of the cache
    // LEFT side (past) stays stable
}
```

**Benefits:**

- Past audio (left of playhead) NEVER changes
- Only future audio (right of playhead) updates as decoder produces it

### Solution 4: Pre-Scan Waveform at Load Time (Best Quality)

For ultimate stability, scan the entire MP3 file at load time and cache the full waveform:**Benefits:**

- Perfect visual stability
- Can show entire track overview
- Enables zoom levels

**Drawbacks:**

- Adds load time (1-3 seconds for a 5-minute track)
- Requires more memory (or on-demand loading)

**Recommendation:** Implement as optional feature for future upgrade

## Implementation Plan

### Phase 1: Quick Fixes (Immediate Performance Improvement)

#### 1.1 Add Variable Resolution Parameter

**File:** `components/ui/src/waveform_view.c`

```c
// Configurable resolution (1 = full, 2 = half, 4 = quarter)
static int waveform_resolution_divider = 1;

void waveform_view_set_resolution(int divider) {
    if (divider < 1) divider = 1;
    if (divider > 8) divider = 8;
    waveform_resolution_divider = divider;
    first_frame = true; // Force full redraw
}
```



#### 1.2 Fix Monotonic Counter

**File:** `main/audio_player.cpp`

```c
// Add monotonic counter (in playback_task, after reading samples)
total_samples_played += bytes_to_read / 4;  // 4 bytes per stereo sample

size_t audio_player_get_waveform_index(void) {
    return (size_t)(total_samples_played / (WAVEFORM_RATIO / 4));
}
```



### Phase 2: Stable Display Cache

#### 2.1 Linear Display Buffer

**File:** `components/ui/src/waveform_view.c`

```c
// Stable display cache
static uint8_t display_cache[480];
static size_t display_center_index = 0;

// Only copy NEW data from right side, keep left side stable
static void update_display_cache(const uint8_t *source, size_t center_index) {
    int delta = (int)center_index - (int)display_center_index;
    
    if (delta > 0 && delta < 240) {
        // Shift left, add new data on right
        memmove(display_cache, display_cache + delta, 480 - delta);
        // Copy new right-side data from source
        memcpy(display_cache + 480 - delta, source + 480 - delta, delta);
    } else {
        // Full refresh
        memcpy(display_cache, source, 480);
    }
    
    display_center_index = center_index;
}
```



### Phase 3: Transient Enhancement (Optional)

Apply simple peak detection to emphasize transients:

```c
// In waveform_view or audio_player
uint8_t enhance_transients(uint8_t current, uint8_t previous) {
    int diff = (int)current - (int)previous;
    if (diff > 30) {  // Sudden increase = transient
        return (current + 50 > 255) ? 255 : current + 50;  // Boost
    }
    return current;
}
```



## Files to Modify

1. `components/ui/src/waveform_view.c`

- Add `waveform_resolution_divider` parameter
- Add `waveform_view_set_resolution()` function
- Add `display_cache[]` linear buffer
- Modify `draw_waveform()` to use bin averaging and cache

2. `components/ui/include/waveform_view.h`

- Add `waveform_view_set_resolution(int divider)` declaration

3. `main/audio_player.cpp`

- Add `total_samples_played` monotonic counter
- Fix `audio_player_get_waveform_index()` to use monotonic counter

4. `components/ui/include/ui_manager.h`

- Add `ui_manager_set_waveform_resolution(int divider)` for external control

## Configuration Parameters

| Parameter | Default | Range | Description |

|-----------|---------|-------|-------------|

| `waveform_resolution_divider` | 1 | 1-8 | Pixels per data bin (higher = fewer bars, faster) |

| `SCROLL_DELTA_THRESHOLD` | 20 | 5-50 | Max scroll before full redraw |

| `waveform_visible_bars` | 480 | 60-480 | Computed from 480/divider |

## Expected Results

| Resolution | Bars Drawn | CPU Reduction | Visual Quality |

|------------|------------|---------------|----------------|

| 1 (full)   | 480        | 0%            | Best           |

| 2 (half)   | 240        | ~40%          | Good           |

| 4 (quarter)| 120        | ~60%          | OK for cueing  |

| 8 (eighth) | 60         | ~75%          | Minimal        |

## Testing

1. Set `waveform_resolution_divider = 2` and verify smoother playback
2. Verify waveform left of playhead stays stable
3. Verify transients/downbeats still visible at lower resolutions
4. Test seek/scrub behavior

## TODOs

- [ ] Add waveform_resolution_divider parameter
- [ ] Implement waveform_view_set_resolution() function
- [ ] Fix monotonic counter in audio_player
- [ ] Add linear display cache to prevent past-data changes
- [ ] Add UI manager wrapper for resolution control
- [ ] Test at various resolution levels