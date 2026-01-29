# High-Contrast HUD UI Implementation

## Overview

Complete implementation of the "High-Contrast HUD" UI system for the ESP32-S3 DJ deck project. The UI follows an industrial telemetry aesthetic with phosphor colors, monospaced fonts, and no rounded corners.

## Architecture

### Components

1. **lvgl_driver** - LVGL integration layer with display and touch drivers
2. **hud_theme** - Theme system with three phosphor colors (Amber, Cyan, Green)
3. **waveform_view** - Vertical bar graph waveform display
4. **telemetry_view** - BPM, pitch, and phase error bar display
5. **metadata_view** - Track title, key, and time remaining
6. **crate_view** - Library browser with inverted selection
7. **ui_manager** - Main UI coordinator

### Layout Zones

- **Zone C (Top, 20%)**: Metadata - Track title (scrolling), Key (Camelot), Time remaining
- **Zone A (Center, 50%)**: Waveform - Vertical bars, playhead, grid, touch cursor
- **Zone B (Bottom, 30%)**: Telemetry - BPM (left), Pitch (right), Phase error bar (center)

## Features Implemented

### ✅ Theme System
- Three phosphor color themes: Amber (#FFB000), Cyan (#00FFFF), Green (#00FF33)
- Strictly duotone (foreground on black)
- Runtime theme switching

### ✅ Waveform View
- Vertical bar graph style (spectrum analyzer look)
- Beat grid lines (dotted vertical)
- Playhead indicator (center vertical line)
- Touch feedback cursor

### ✅ Telemetry View
- Large BPM display (left side)
- Large pitch display (right side)
- Phase error bar (horizontal, grows left/right from center)
- Center indicator line

### ✅ Metadata View
- Scrolling track title (VCR-style marquee)
- Camelot key notation
- Time remaining/elapsed display

### ✅ Crate View
- High-density track list
- Inverted selection (black text on phosphor background)
- Scrollable list with selection tracking

### ✅ Touch Integration
- Touch input handling via LVGL
- Ghost cursor feedback on waveform

## Technical Details

### LVGL Integration
- Uses LVGL 8.4.0 via IDF component manager
- Double buffering for smooth rendering
- PSRAM allocation for display buffers
- Custom flush callback integrates with existing display driver

### Performance Optimizations
- Canvas-based waveform rendering
- Direct buffer access for crisp display
- Efficient bar graph drawing
- Minimal LVGL widget usage (prefer primitives)

### Memory Usage
- Display buffers: ~40 lines × width × 2 bytes × 2 buffers
- All allocations use PSRAM where possible

## Integration Notes

### Required Dependencies
- LVGL component (managed via idf_component.yml)
- Display driver (main/display.c)
- FreeRTOS

### Initialization Order
1. Initialize display driver (`display_init()`)
2. Initialize UI manager (`ui_manager_init(width, height)`)
3. Call `ui_manager_process()` in main loop
4. Handle touch events via `ui_manager_handle_touch()`

### Usage Example

```c
// Initialize UI
ui_manager_init(480, 272);

// Set theme
ui_manager_set_theme(UI_THEME_AMBER);

// Update waveform
uint8_t waveform_data[480];
// ... fill waveform_data ...
ui_manager_update_waveform(waveform_data, 480, 0.5f);

// Update telemetry
ui_manager_update_telemetry(124.0f, 0.8f, -0.1f);

// Update metadata
ui_manager_update_metadata("Track Title", "4A", -225);

// Main loop
while (1) {
    ui_manager_process();
    vTaskDelay(pdMS_TO_TICKS(16)); // ~60 FPS
}
```

## Remaining TODOs

1. **Nudge Animation**: Placeholder exists, needs LVGL animator implementation
2. **Fast Forward Effect**: Motion blur and spinning reel icon not yet implemented
3. **Mini-Preview in Crate**: Waveform thumbnail on right side of track list
4. **Settings View**: Not yet implemented
5. **PPA Optimization**: Could use ESP32-P4 PPA for waveform shifting (future enhancement)

## Design Philosophy

- **No Curves**: All UI elements use sharp corners (radius = 0)
- **Monospaced Only**: All text uses monospaced fonts for grid alignment
- **High Contrast**: Strictly duotone (phosphor color on black)
- **Industrial Aesthetic**: Telemetry/HUD style, not iPhone-style
- **Performance First**: Optimized for 60 FPS on ESP32-S3

## Notes

- Phase error bar grows left when behind, right when ahead
- Crate view uses inverted selection (classic tracker style)
- All views are initially hidden and shown/hidden as needed
- Touch feedback cursor appears on waveform when using touch strip

