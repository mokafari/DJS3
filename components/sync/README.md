# DIN Sync Component

## Overview

Full DIN sync (sync24) implementation at 24ppqn, compatible with:
- Roland TR-x0x series drum machines
- Korg Volcas  
- Teenage Engineering Pocket Operators
- Other sync24 compatible gear

## Features

- **Master mode**: Generate 24ppqn clock with start/stop/continue
- **Slave mode**: Follow external DIN sync with clock recovery
- **Jitter compensation**: Median filter for stable BPM detection
- **LED callbacks**: UI integration for sync status indicators
- **Swing support**: Adjustable timing for groove feel

## Hardware Connections

DIN sync uses two signals:
- **Clock**: 24 pulses per quarter note (5V pulses)
- **Run/Stop**: High when running, low when stopped

## Usage

### Master Mode (Output)

```c
analog_sync_config_t config = {
    .clock_out_pin = GPIO_NUM_25,
    .clock_in_pin = -1,
    .run_out_pin = GPIO_NUM_26,
    .run_in_pin = -1
};

analog_sync_t *sync = analog_sync_create(&config);
analog_sync_set_mode(sync, ANALOG_SYNC_MODE_MASTER);
analog_sync_set_bpm(sync, 120.0f);
analog_sync_start(sync);
```

### Slave Mode (Input)

```c
analog_sync_config_t config = {
    .clock_out_pin = -1,
    .clock_in_pin = GPIO_NUM_34,
    .run_out_pin = -1,
    .run_in_pin = GPIO_NUM_35
};

analog_sync_t *sync = analog_sync_create(&config);
analog_sync_set_mode(sync, ANALOG_SYNC_MODE_SLAVE);

// In main loop:
while (1) {
    analog_sync_tick(sync);  // Process deferred events
    
    if (analog_sync_is_locked(sync)) {
        float bpm = analog_sync_get_bpm(sync);
        float phase = analog_sync_get_phase(sync);
        // Use BPM and phase for playback
    }
    
    vTaskDelay(pdMS_TO_TICKS(10));
}
```

### LED Status Callback

```c
void led_callback(analog_sync_t *sync, 
                  analog_sync_led_state_t state,
                  void *user_data) {
    switch (state) {
        case ANALOG_SYNC_LED_OFF:     // Sync disabled
        case ANALOG_SYNC_LED_WAITING: // Waiting for clock
        case ANALOG_SYNC_LED_LOCKED:  // Locked to clock
        case ANALOG_SYNC_LED_RUNNING: // Transport running
        case ANALOG_SYNC_LED_STOPPED: // Ready but stopped
    }
}

analog_sync_set_led_callback(sync, led_callback, NULL);
```

## API Reference

See `include/analog_sync.h` for complete API documentation.
