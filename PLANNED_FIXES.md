# UI and Waveform Improvement Plan

## 1. Waveform Rendering Overhaul (Fixing "Slow" & "Ghosting")
**Goal:** Create a smooth, centered, scrolling waveform with no ghosting.

- **Switch to Scrolling Mode:**
  - Instead of a playhead moving across a static waveform (sweeping), keep the playhead fixed in the center of the screen.
  - Shift the waveform data leftwards as audio plays.
  - **Note:** Since we are decoding in real-time, showing "future" waveform (to the right of the playhead) requires buffering. We will utilize the existing peak buffer to show as much history/future context as possible.

- **Fix Ghosting (Overlaid Instances):**
  - **Cause:** The canvas is likely not being cleared (filled with black) before drawing the new frame.
  - **Fix:** Explicitly `memset` the canvas buffer to zero/black at the start of every update cycle.

- **Optimization (Fixing "Extremely Slow"):**
  - **Current:** Likely using `lv_canvas_set_px` which has high overhead per pixel.
  - **New:** Access the internal canvas buffer (array of colors) directly. Writing `uint16_t` values directly to RAM is orders of magnitude faster.

- **Centering:**
  - Adjust the Y-coordinate drawing logic to ensure the zero-crossing line is exactly at `height / 2`.

## 2. Track Loading & Playback Reliability
**Goal:** Ensure tracks with ID3 tags load and display waveforms correctly.

- **Issue:** User reported tracks with ID3 tags "don't load" waveform.
- **Hypothesis:** 
  - The fallback filename logic might be working better than the full path handling for ID3-tagged files.
  - Or, the ID3 parser might be consuming bytes that the decoder needs, causing a sync issue at the start of the file.
- **Fix:**
  - Verify `audio_player_load` resets the `waveform_peaks` buffer to zero on new load.
  - Ensure the `audio_task` is properly notified to reset its state.

## 3. UI Enhancements
**Goal:** Polish the user experience.

- **Progress Indicator:**
  - Add a thin, static "overview" bar at the bottom of the waveform view.
  - A small indicator will move across this bar to show overall track position (Start -> End).

- **Time Display Toggle:**
  - Make the time label clickable (or add a transparent button over it).
  - **Logic:** Toggle between `Elapsed Time` (positive) and `Remaining Time` (negative).
  - **Default:** Remaining Time (standard for DJs).

## 4. Implementation Steps
1.  **Modify `waveform_view.c`**: 
    - rewrite `update_waveform` to direct-buffer access.
    - Implement scrolling logic.
    - Implement clear screen logic.
2.  **Modify `metadata_view.c`**:
    - Add click handler for time toggle.
3.  **Modify `audio_player.cpp`**:
    - Ensure peaks are pushed correctly for scrolling.
