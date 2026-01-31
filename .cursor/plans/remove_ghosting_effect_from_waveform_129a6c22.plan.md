---
name: Remove Ghosting Effect from Waveform
overview: Remove all ghosting effect infrastructure from the waveform display to create a crisp, clean waveform suitable for professional DJ cueing. The ghosting buffers are allocated but never used, so this is primarily cleanup work.
todos:
  - id: remove-ghosting-code
    content: Remove GHOST_FRAMES define, waveform_history array, history_index variable, and allocation code from waveform_view.c
    status: pending
  - id: update-documentation
    content: Update components/ui/README.md to remove references to ghosting effect
    status: pending
    dependencies:
      - remove-ghosting-code
  - id: verify-rendering
    content: Verify waveform renders crisply without any ghosting artifacts
    status: pending
    dependencies:
      - remove-ghosting-code
---

# Remove Ghosting Effect from Waveform Display

## Current State Analysis

The waveform display in `components/ui/src/waveform_view.c` has ghosting infrastructure that is **allocated but never used**:

- `GHOST_FRAMES` constant (line 20)
- `waveform_history[]` array with 3 buffers (lines 35-36)
- `history_index` variable (line 36)
- History buffer allocation in `waveform_view_init()` (lines 116-124)

The `draw_waveform()` function already clears the buffer completely with `memset()` (line 56), which is correct for crisp rendering. The ghosting buffers are just wasting memory.

## Changes Required

### 1. Remove Ghosting Infrastructure from `waveform_view.c`

**Remove:**

- `#define GHOST_FRAMES 3` (line 20)
- `static uint8_t *waveform_history[GHOST_FRAMES] = {NULL};` (line 35)
- `static int history_index = 0;` (line 36)
- History buffer allocation loop (lines 116-124)
- Comment about ghosting on line 34

**Result:** Cleaner code, reduced memory usage (~1.4KB freed), and no confusion about unused features.

### 2. Update Documentation References

**Files to update:**

- `components/ui/README.md` - Remove mentions of ghosting effect (lines 13, 34, 68, 136)
- `PLANNED_FIXES.md` - Already documents removal of ghosting, no changes needed

### 3. Verify Waveform Rendering Quality

**Confirm:**

- Buffer clearing with `memset()` is working correctly (already implemented, line 56)
- Direct buffer access is being used (already implemented, lines 49-50, 87)
- No alpha blending or transparency effects that could cause ghosting

## Implementation Details

The waveform rendering is already optimized for crisp display:

- Full buffer clear before each frame (`memset(buffer, 0, buffer_size_bytes)`)
- Direct pixel writes to canvas buffer (no LVGL drawing primitives)
- Solid color drawing (no transparency)

## Expected Outcome

- **Memory savings:** ~1.4KB (3 buffers × 480 bytes)
- **Code clarity:** Removed unused infrastructure
- **Performance:** No change (ghosting wasn't being used anyway)
- **Visual result:** Crisp, clean waveform perfect for DJ cueing

## Files to Modify

1. `components/ui/src/waveform_view.c` - Remove ghosting code
2. `components/ui/README.md` - Update documentation

## Testing

After changes:

- Verify waveform displays without any trailing/ghosting effects
- Confirm memory usage is reduced