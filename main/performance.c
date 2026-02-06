/**
 * @file performance.c
 * @brief Performance mode controller
 * 
 * Integrates performance view with the rest of the system:
 * - Touch gesture detection (double-tap to enter)
 * - Button mapping for performance mode entry/exit
 * - State synchronization with audio player
 */

#include "performance.h"
#include "ui_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "performance";

// ============================================================================
// CONFIGURATION
// ============================================================================

#define DOUBLE_TAP_TIMEOUT_MS   500     // Max time between taps for double-tap
#define LONG_PRESS_DURATION_MS  800     // Duration for long press detection

// ============================================================================
// STATE
// ============================================================================

static bool performance_mode_active = false;
static uint64_t last_tap_time = 0;
static bool initialized = false;

// ============================================================================
// CALLBACKS
// ============================================================================

// ============================================================================
// PUBLIC API
// ============================================================================

void performance_init(void) {
    if (initialized) return;
    
    ESP_LOGI(TAG, "Initializing performance mode controller");
    
    // Performance view is already initialized by ui_manager
    // Exit callback is handled internally by ui_manager
    
    performance_mode_active = false;
    last_tap_time = 0;
    initialized = true;
    
    ESP_LOGI(TAG, "Performance mode controller ready");
}

void performance_deinit(void) {
    initialized = false;
    performance_mode_active = false;
}

void performance_enter(void) {
    if (!initialized) {
        ESP_LOGW(TAG, "Performance mode not initialized");
        return;
    }
    
    if (performance_mode_active) {
        ESP_LOGD(TAG, "Already in performance mode");
        return;
    }
    
    ESP_LOGI(TAG, "Entering performance mode");
    
    // Enter performance view
    ui_manager_enter_performance_mode();
    performance_mode_active = true;
}

void performance_exit(void) {
    if (!performance_mode_active) return;
    
    ESP_LOGI(TAG, "Exiting performance mode");
    performance_mode_active = false;
    ui_manager_exit_performance_mode();
}

bool performance_is_active(void) {
    return performance_mode_active;
}

void performance_toggle(void) {
    if (performance_mode_active) {
        performance_exit();
    } else {
        performance_enter();
    }
}

bool performance_handle_tap(int x, int y) {
    uint64_t now = esp_timer_get_time() / 1000;  // Convert to ms
    
    // Check for double-tap (to enter performance mode from normal view)
    if (!performance_mode_active) {
        if ((now - last_tap_time) < DOUBLE_TAP_TIMEOUT_MS) {
            // Double tap detected - enter performance mode
            ESP_LOGI(TAG, "Double-tap detected at (%d, %d) - entering performance mode", x, y);
            performance_enter();
            last_tap_time = 0;  // Reset
            return true;
        }
        last_tap_time = now;
    }
    
    return false;  // Tap not consumed
}

void performance_handle_button(performance_button_t button, bool pressed) {
    if (!initialized) return;
    
    switch (button) {
        case PERF_BUTTON_MODE:
            // Toggle performance mode on button press
            if (pressed) {
                performance_toggle();
            }
            break;
            
        case PERF_BUTTON_SHIFT:
            // Shift could be used for alternate functions
            break;
            
        default:
            break;
    }
}

// ============================================================================
// TASK FOR GESTURE MONITORING (optional)
// ============================================================================

#ifdef CONFIG_PERFORMANCE_GESTURE_TASK

static TaskHandle_t gesture_task_handle = NULL;

static void gesture_monitor_task(void *pvParameters) {
    ESP_LOGI(TAG, "Gesture monitor task started");
    
    while (1) {
        // This task could monitor for gestures like:
        // - Swipe up to enter performance mode
        // - Three-finger tap
        // etc.
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void performance_start_gesture_monitor(void) {
    if (gesture_task_handle == NULL) {
        xTaskCreate(gesture_monitor_task, "perf_gesture", 2048, NULL, 2, &gesture_task_handle);
    }
}

void performance_stop_gesture_monitor(void) {
    if (gesture_task_handle) {
        vTaskDelete(gesture_task_handle);
        gesture_task_handle = NULL;
    }
}

#endif // CONFIG_PERFORMANCE_GESTURE_TASK
