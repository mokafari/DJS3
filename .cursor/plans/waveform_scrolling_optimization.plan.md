# Waveform Scrolling and Time Alignment Optimization Plan

## Overview

Optimize the scrolling waveform display for better performance and sample-accurate time alignment with audio playback. This builds on the existing architecture which already has a pre-computed waveform cache.

## Current Architecture Analysis

### Data Flow

```javascript
┌─────────────────────────────────────────────────────────────────────────┐
│ audio_player.cpp                                                        │
│ ┌─────────────────┐    ┌──────────────────────┐                        │
│ │ decoder_task    │───▶│ waveform_ring_buffer │ (4KB pre-computed)     │
│ │ (computes peaks)│    │ (1 byte per 1024     │                        │
│ └─────────────────┘    │  PCM bytes)          │                        │
│                        └──────────────────────┘                        │
│                                   │                                     │
│                        audio_player_get_waveform()                      │
│                        (extracts 480 bytes centered on playhead)        │
└───────────────────────────────────┼─────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│ main.c - Main Loop (every 50ms)                                         │
│ ┌─────────────────────────────────────────────────────────────────────┐ │
│ │ uint8_t waveform_data[480];                                         │ │
│ │ audio_player_get_waveform(waveform_data, 480);                      │ │
│ │ ui_manager_update_waveform(waveform_data, 480, position);           │ │
│ └─────────────────────────────────────────────────────────────────────┘ │
└───────────────────────────────────┼─────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│ waveform_view.c                                                         │
│ ┌─────────────────────────────────────────────────────────────────────┐ │
│ │ draw_waveform()                                                     │ │
│ │ - memset(buffer, 0, size)  ◄── CLEARS ENTIRE BUFFER EVERY FRAME    │ │
│ │ - for each x: draw vertical bar                                     │ │
│ └─────────────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────────────┘
```



### What's Already Good

1. **Pre-computed waveform cache** in `audio_player.cpp` - peaks computed during decode
2. **Sample-accurate positioning** - uses `rb_read_head` from audio playback
3. **Direct buffer access** - writes directly to canvas, not via LVGL primitives
4. **Centered playhead** - waveform data already centered around playhead

### Current Inefficiencies

1. **Full buffer clear every frame** - `memset()` clears entire 480×202×2 = 194KB buffer
2. **Full redraw every frame** - redraws all 480 columns even when only a few pixels scrolled
3. **50ms update interval** - fixed interval not tied to actual scroll distance
4. **No delta tracking** - doesn't know how much the waveform scrolled since last frame

## Optimizations to Implement

### 1. Ring Buffer Scroll (Shift Instead of Redraw)

**File:** `components/ui/src/waveform_view.c`**Concept:** Instead of clearing and redrawing the entire buffer, shift the existing pixels left and only draw the new columns that scrolled into view.**Changes:**

- Track `last_wave_index` (the waveform buffer index from last frame)
- Calculate `scroll_delta` = current_wave_index - last_wave_index
- If `scroll_delta` < view_width: use `memmove()` to shift rows left, draw only new columns
- If `scroll_delta` >= view_width: full redraw (skip optimization)

**Expected Benefit:** 10-50x reduction in drawing operations for typical scroll amounts (1-5 pixels per frame at 20fps)

### 2. Sample-Accurate Position Passing

**Files:** `main/audio_player.cpp`, `main/main.c`, `components/ui/src/waveform_view.c`**Concept:** Pass the actual waveform buffer index (sample-accurate) to the view, not just a 0.0-1.0 position.**Changes:**

- Add `audio_player_get_waveform_index()` function returning current waveform buffer index
- Pass this index to `waveform_view_update()` 
- Use index for delta calculation in ring buffer scroll

### 3. Adaptive Update Frequency

**File:** `main/main.c`**Concept:** Only update the waveform display when the position has actually changed by at least 1 pixel.**Changes:**

- Track `last_waveform_index`
- Only call `ui_manager_update_waveform()` when index has changed
- Reduces unnecessary redraws when audio is paused or at very slow playback

## Implementation Details

### Phase 1: Ring Buffer Scroll in waveform_view.c

Add to `waveform_view.c`:

```c
// Track last position for delta scrolling
static size_t last_wave_index = 0;
static bool first_frame = true;

static void draw_waveform_scrolled(const uint8_t *waveform_data, size_t num_samples, size_t current_index) {
    if (!waveform_canvas || !visible) return;
    
    lv_img_dsc_t *canvas_img = lv_canvas_get_img(waveform_canvas);
    lv_color_t *buffer = (lv_color_t *)canvas_img->data;
    if (!buffer) return;
    
    lv_color_t fg_color = hud_theme_get_foreground_color();
    lv_color_t bg_color = lv_color_black();
    int center_y = view_height / 2;
    
    // Calculate scroll delta
    int scroll_delta = 0;
    if (!first_frame) {
        scroll_delta = (int)current_index - (int)last_wave_index;
    }
    first_frame = false;
    last_wave_index = current_index;
    
    // If delta is too large or negative (seek), do full redraw
    if (scroll_delta <= 0 || scroll_delta >= (int)view_width || scroll_delta > 20) {
        // Full redraw
        memset(buffer, 0, view_width * view_height * sizeof(lv_color_t));
        for (int x = 0; x < view_width && x < num_samples; x++) {
            draw_bar_at(buffer, x, waveform_data[x], center_y, fg_color);
        }
    } else {
        // Incremental scroll: shift left and draw new columns
        for (int y = 0; y < view_height; y++) {
            memmove(&buffer[y * view_width], 
                    &buffer[y * view_width + scroll_delta], 
                    (view_width - scroll_delta) * sizeof(lv_color_t));
            // Clear the new columns on the right
            for (int x = view_width - scroll_delta; x < view_width; x++) {
                buffer[y * view_width + x] = bg_color;
            }
        }
        
        // Draw only the new bars on the right edge
        for (int x = view_width - scroll_delta; x < view_width && x < num_samples; x++) {
            draw_bar_at(buffer, x, waveform_data[x], center_y, fg_color);
        }
    }
    
    lv_obj_invalidate(waveform_canvas);
}
```



### Phase 2: Add Waveform Index API

Add to `audio_player.cpp`:

```c
size_t audio_player_get_waveform_index(void) {
    size_t idx;
    xSemaphoreTake(buffer_mutex, portMAX_DELAY);
    idx = rb_read_head / WAVEFORM_RATIO;
    xSemaphoreGive(buffer_mutex);
    return idx;
}
```

Add to `audio_player.h`:

```c
size_t audio_player_get_waveform_index(void);
```



### Phase 3: Update Waveform View Interface

Update `waveform_view.h`:

```c
void waveform_view_update(const uint8_t *waveform_data, size_t num_samples, float position, size_t wave_index);
```

Update `main.c`:

```c
size_t wave_index = audio_player_get_waveform_index();
ui_manager_update_waveform(waveform_data, 480, position, wave_index);
```



## Files to Modify

1. `components/ui/src/waveform_view.c` - Add ring buffer scroll optimization
2. `components/ui/include/waveform_view.h` - Add wave_index parameter
3. `components/ui/src/ui_manager.c` - Pass wave_index through
4. `components/ui/include/ui_manager.h` - Update function signature
5. `main/audio_player.cpp` - Add `audio_player_get_waveform_index()`
6. `main/include/audio_player.h` - Add function declaration
7. `main/main.c` - Use new API

## Expected Performance Improvements

| Metric | Before | After | Improvement |

|--------|--------|-------|-------------|

| Pixels drawn per frame | 480 × 202 = 96,960 | ~5 × 202 = 1,010 | ~96x less |

| memset bytes per frame | 194KB | 0 (incremental) | Eliminated |

| memmove bytes per frame | 0 | ~194KB (shift) | Similar cost |

| Net CPU reduction | - | ~30-50% | Significant |**Note:** The memmove is similar cost to memset, but we avoid the redraw loop which is more expensive due to branching and color lookups.

## Testing

1. Verify waveform scrolls smoothly during playback
2. Verify seek/jump causes full redraw (no artifacts)
3. Verify pause shows static waveform (no unnecessary updates)
4. Verify playhead stays centered
5. Compare frame timing before/after

## Todos

- [ ] Implement ring buffer scroll in waveform_view.c
- [ ] Add audio_player_get_waveform_index() API
- [ ] Update waveform_view interface to accept wave_index
- [ ] Update ui_manager to pass wave_index
- [ ] Update main.c to use new API
- [ ] Test and verify improvements