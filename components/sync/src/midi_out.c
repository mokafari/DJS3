/**
 * @file midi_out.c
 * @brief MIDI output implementation for DJ deck synchronization
 * 
 * Generates MIDI clock at 24 PPQN synchronized to deck playback.
 * Features:
 * - High-precision timer-based clock generation
 * - Transport messages (start/stop/continue)
 * - BPM transmission via CC messages
 * - Phase-locked sync to deck beat grid
 */

#include "midi_out.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static const char *TAG = "midi_out";

/* ============================================================================
 * MIDI Message Constants
 * ============================================================================ */

#define MIDI_CLOCK      0xF8    ///< Timing clock (24 PPQN)
#define MIDI_START      0xFA    ///< Start sequence
#define MIDI_CONTINUE   0xFB    ///< Continue sequence
#define MIDI_STOP       0xFC    ///< Stop sequence
#define MIDI_SPP        0xF2    ///< Song Position Pointer

#define MIDI_CC_STATUS(ch)  (0xB0 | ((ch) & 0x0F))  ///< Control Change status

#define MIDI_CLOCKS_PER_BEAT    24  ///< Standard MIDI clock resolution
#define MIDI_BAUD_RATE          31250  ///< Standard MIDI baud rate

/* BPM encoding range for CC transmission */
#define BPM_CC_MIN      60.0f   ///< Minimum BPM for CC encoding
#define BPM_CC_MAX      187.27f ///< Maximum BPM for CC encoding (60 + 127.27)
#define BPM_CC_SCALE    100.0f  ///< Scale factor for 0.01 BPM precision

/* ============================================================================
 * Internal Structure
 * ============================================================================ */

struct midi_out_s {
    /* Configuration */
    int uart_num;
    int tx_pin;
    uint8_t bpm_cc_msb;
    uint8_t bpm_cc_lsb;
    uint8_t channel;
    bool send_bpm_cc;
    
    /* State */
    bool running;
    float bpm;
    uint32_t pulse_count;
    
    /* Timer for clock generation */
    esp_timer_handle_t clock_timer;
    uint64_t clock_period_us;
    
    /* Sync state */
    float last_deck_phase;
    uint32_t last_sync_time_us;
    float phase_correction;
    
    /* Thread safety */
    SemaphoreHandle_t mutex;
};

/* ============================================================================
 * Internal Functions
 * ============================================================================ */

/**
 * @brief Send a single MIDI byte
 */
static inline void midi_out_send_byte(midi_out_t *out, uint8_t byte) {
    uart_write_bytes(out->uart_num, &byte, 1);
}

/**
 * @brief Calculate clock period in microseconds from BPM
 * 
 * Period = (60 seconds / BPM / 24 clocks) * 1,000,000 us
 */
static inline uint64_t bpm_to_clock_period_us(float bpm) {
    if (bpm < 20.0f) bpm = 20.0f;
    if (bpm > 300.0f) bpm = 300.0f;
    return (uint64_t)((60.0 / (double)bpm / MIDI_CLOCKS_PER_BEAT) * 1000000.0);
}

/**
 * @brief Timer callback for MIDI clock generation
 * 
 * Called every clock tick (24 times per beat).
 */
static void IRAM_ATTR midi_clock_timer_cb(void *arg) {
    midi_out_t *out = (midi_out_t *)arg;
    
    if (!out->running) {
        return;
    }
    
    /* Send MIDI clock byte */
    midi_out_send_byte(out, MIDI_CLOCK);
    out->pulse_count++;
}

/**
 * @brief Initialize UART for MIDI output
 */
static esp_err_t midi_out_init_uart(midi_out_t *out) {
    if (out->tx_pin < 0) {
        ESP_LOGW(TAG, "No TX pin configured, MIDI output disabled");
        return ESP_OK;
    }
    
    uart_config_t uart_config = {
        .baud_rate = MIDI_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    
    esp_err_t ret = uart_param_config(out->uart_num, &uart_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure UART: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = uart_set_pin(out->uart_num, out->tx_pin, UART_PIN_NO_CHANGE,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set UART pins: %s", esp_err_to_name(ret));
        return ret;
    }
    
    /* Small TX buffer, no RX needed */
    ret = uart_driver_install(out->uart_num, 0, 256, 0, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install UART driver: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "UART%d initialized for MIDI (TX: GPIO%d)", out->uart_num, out->tx_pin);
    return ESP_OK;
}

/**
 * @brief Create high-resolution timer for clock generation
 */
static esp_err_t midi_out_init_timer(midi_out_t *out) {
    esp_timer_create_args_t timer_args = {
        .callback = midi_clock_timer_cb,
        .arg = out,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "midi_clock",
        .skip_unhandled_events = true,
    };
    
    esp_err_t ret = esp_timer_create(&timer_args, &out->clock_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create timer: %s", esp_err_to_name(ret));
        return ret;
    }
    
    return ESP_OK;
}

/* ============================================================================
 * Public API
 * ============================================================================ */

midi_out_t *midi_out_create(const midi_out_config_t *config) {
    if (!config) {
        ESP_LOGE(TAG, "Config is NULL");
        return NULL;
    }
    
    midi_out_t *out = calloc(1, sizeof(midi_out_t));
    if (!out) {
        ESP_LOGE(TAG, "Failed to allocate memory");
        return NULL;
    }
    
    /* Copy configuration */
    out->uart_num = config->uart_num;
    out->tx_pin = config->tx_pin;
    out->bpm_cc_msb = config->bpm_cc_msb;
    out->bpm_cc_lsb = config->bpm_cc_lsb;
    out->channel = config->channel & 0x0F;
    out->send_bpm_cc = config->send_bpm_cc;
    
    /* Initialize state */
    out->running = false;
    out->bpm = 120.0f;
    out->pulse_count = 0;
    out->clock_period_us = bpm_to_clock_period_us(120.0f);
    out->last_deck_phase = 0.0f;
    out->last_sync_time_us = 0;
    out->phase_correction = 0.0f;
    
    /* Create mutex */
    out->mutex = xSemaphoreCreateMutex();
    if (!out->mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        free(out);
        return NULL;
    }
    
    /* Initialize UART */
    if (midi_out_init_uart(out) != ESP_OK) {
        vSemaphoreDelete(out->mutex);
        free(out);
        return NULL;
    }
    
    /* Initialize timer */
    if (midi_out_init_timer(out) != ESP_OK) {
        if (out->tx_pin >= 0) {
            uart_driver_delete(out->uart_num);
        }
        vSemaphoreDelete(out->mutex);
        free(out);
        return NULL;
    }
    
    ESP_LOGI(TAG, "MIDI output created (UART%d, TX:%d, CH:%d)",
             out->uart_num, out->tx_pin, out->channel);
    
    return out;
}

void midi_out_destroy(midi_out_t *out) {
    if (!out) return;
    
    /* Stop clock if running */
    if (out->running) {
        midi_out_stop(out);
    }
    
    /* Clean up timer */
    if (out->clock_timer) {
        esp_timer_stop(out->clock_timer);
        esp_timer_delete(out->clock_timer);
    }
    
    /* Clean up UART */
    if (out->tx_pin >= 0) {
        uart_driver_delete(out->uart_num);
    }
    
    /* Clean up mutex */
    if (out->mutex) {
        vSemaphoreDelete(out->mutex);
    }
    
    free(out);
    ESP_LOGI(TAG, "MIDI output destroyed");
}

void midi_out_start(midi_out_t *out, float bpm) {
    if (!out) return;
    
    xSemaphoreTake(out->mutex, portMAX_DELAY);
    
    /* Update BPM and calculate period */
    out->bpm = bpm;
    out->clock_period_us = bpm_to_clock_period_us(bpm);
    out->pulse_count = 0;
    out->last_deck_phase = 0.0f;
    out->phase_correction = 0.0f;
    
    /* Send MIDI Start message */
    if (out->tx_pin >= 0) {
        midi_out_send_byte(out, MIDI_START);
        
        /* Send initial BPM if enabled */
        if (out->send_bpm_cc) {
            xSemaphoreGive(out->mutex);
            midi_out_send_bpm_cc(out, bpm);
            xSemaphoreTake(out->mutex, portMAX_DELAY);
        }
    }
    
    /* Start clock timer */
    esp_timer_start_periodic(out->clock_timer, out->clock_period_us);
    out->running = true;
    
    xSemaphoreGive(out->mutex);
    
    ESP_LOGI(TAG, "MIDI clock started @ %.2f BPM (period: %llu us)",
             bpm, out->clock_period_us);
}

void midi_out_stop(midi_out_t *out) {
    if (!out) return;
    
    xSemaphoreTake(out->mutex, portMAX_DELAY);
    
    /* Stop timer first */
    esp_timer_stop(out->clock_timer);
    out->running = false;
    
    /* Send MIDI Stop message */
    if (out->tx_pin >= 0) {
        midi_out_send_byte(out, MIDI_STOP);
    }
    
    xSemaphoreGive(out->mutex);
    
    ESP_LOGI(TAG, "MIDI clock stopped (sent %lu pulses)", out->pulse_count);
}

void midi_out_continue(midi_out_t *out) {
    if (!out) return;
    
    xSemaphoreTake(out->mutex, portMAX_DELAY);
    
    /* Send MIDI Continue message */
    if (out->tx_pin >= 0) {
        midi_out_send_byte(out, MIDI_CONTINUE);
    }
    
    /* Resume clock timer */
    esp_timer_start_periodic(out->clock_timer, out->clock_period_us);
    out->running = true;
    
    xSemaphoreGive(out->mutex);
    
    ESP_LOGI(TAG, "MIDI clock continued @ %.2f BPM", out->bpm);
}

void midi_out_set_bpm(midi_out_t *out, float bpm) {
    if (!out) return;
    
    /* Clamp BPM to reasonable range */
    if (bpm < 20.0f) bpm = 20.0f;
    if (bpm > 300.0f) bpm = 300.0f;
    
    xSemaphoreTake(out->mutex, portMAX_DELAY);
    
    out->bpm = bpm;
    out->clock_period_us = bpm_to_clock_period_us(bpm);
    
    /* Update timer if running */
    if (out->running) {
        esp_timer_stop(out->clock_timer);
        esp_timer_start_periodic(out->clock_timer, out->clock_period_us);
    }
    
    xSemaphoreGive(out->mutex);
    
    /* Send BPM via CC if enabled */
    if (out->send_bpm_cc && out->tx_pin >= 0) {
        midi_out_send_bpm_cc(out, bpm);
    }
    
    ESP_LOGD(TAG, "BPM set to %.2f (period: %llu us)", bpm, out->clock_period_us);
}

float midi_out_get_bpm(const midi_out_t *out) {
    if (!out) return 120.0f;
    return out->bpm;
}

void midi_out_send_bpm_cc(midi_out_t *out, float bpm) {
    if (!out || out->tx_pin < 0) return;
    
    /*
     * Encode BPM as 14-bit value using two CCs:
     * value = (bpm - 60) * 100
     * This gives 0.01 BPM precision for range 60-187.27 BPM
     */
    bpm = fmaxf(BPM_CC_MIN, fminf(BPM_CC_MAX, bpm));
    uint16_t value = (uint16_t)((bpm - BPM_CC_MIN) * BPM_CC_SCALE);
    
    uint8_t msb = (value >> 7) & 0x7F;
    uint8_t lsb = value & 0x7F;
    
    xSemaphoreTake(out->mutex, portMAX_DELAY);
    
    uint8_t status = MIDI_CC_STATUS(out->channel);
    
    /* Send MSB */
    midi_out_send_byte(out, status);
    midi_out_send_byte(out, out->bpm_cc_msb);
    midi_out_send_byte(out, msb);
    
    /* Send LSB */
    midi_out_send_byte(out, status);
    midi_out_send_byte(out, out->bpm_cc_lsb);
    midi_out_send_byte(out, lsb);
    
    xSemaphoreGive(out->mutex);
    
    ESP_LOGD(TAG, "Sent BPM %.2f as CC (MSB:%d=%d, LSB:%d=%d)",
             bpm, out->bpm_cc_msb, msb, out->bpm_cc_lsb, lsb);
}

void midi_out_sync_to_deck(midi_out_t *out, const midi_out_deck_state_t *state) {
    if (!out || !state) return;
    
    uint64_t now_us = esp_timer_get_time();
    
    xSemaphoreTake(out->mutex, portMAX_DELAY);
    
    /* Handle transport state changes */
    bool was_running = out->running;
    
    if (state->is_playing && !was_running) {
        /* Deck started playing - start MIDI clock */
        xSemaphoreGive(out->mutex);
        midi_out_start(out, state->bpm);
        return;
    } else if (!state->is_playing && was_running) {
        /* Deck stopped - stop MIDI clock */
        xSemaphoreGive(out->mutex);
        midi_out_stop(out);
        return;
    }
    
    if (!out->running) {
        xSemaphoreGive(out->mutex);
        return;
    }
    
    /* Update BPM if changed significantly (>0.1 BPM) */
    if (fabsf(state->bpm - out->bpm) > 0.1f) {
        out->bpm = state->bpm;
        out->clock_period_us = bpm_to_clock_period_us(state->bpm);
        esp_timer_stop(out->clock_timer);
        esp_timer_start_periodic(out->clock_timer, out->clock_period_us);
        
        /* Send BPM CC */
        if (out->send_bpm_cc && out->tx_pin >= 0) {
            uint8_t status = MIDI_CC_STATUS(out->channel);
            float bpm = fmaxf(BPM_CC_MIN, fminf(BPM_CC_MAX, state->bpm));
            uint16_t value = (uint16_t)((bpm - BPM_CC_MIN) * BPM_CC_SCALE);
            
            midi_out_send_byte(out, status);
            midi_out_send_byte(out, out->bpm_cc_msb);
            midi_out_send_byte(out, (value >> 7) & 0x7F);
            
            midi_out_send_byte(out, status);
            midi_out_send_byte(out, out->bpm_cc_lsb);
            midi_out_send_byte(out, value & 0x7F);
        }
    }
    
    /*
     * Phase correction: Adjust clock timing to match deck beat grid
     * 
     * This is a soft sync - we gradually adjust the clock period
     * to drift toward the correct phase rather than jumping.
     */
    if (state->phase >= 0.0f && out->last_sync_time_us > 0) {
        float phase_delta = state->phase - out->last_deck_phase;
        
        /* Handle phase wraparound */
        if (phase_delta < -0.5f) phase_delta += 1.0f;
        if (phase_delta > 0.5f) phase_delta -= 1.0f;
        
        /* Calculate expected phase based on pulses sent */
        float expected_phase = (float)(out->pulse_count % MIDI_CLOCKS_PER_BEAT) 
                               / (float)MIDI_CLOCKS_PER_BEAT;
        
        /* Calculate phase error */
        float phase_error = state->phase - expected_phase;
        if (phase_error < -0.5f) phase_error += 1.0f;
        if (phase_error > 0.5f) phase_error -= 1.0f;
        
        /* Apply gradual correction (5% per sync call) */
        out->phase_correction = phase_error * 0.05f;
        
        /* Adjust period slightly to drift toward correct phase */
        if (fabsf(phase_error) > 0.02f) {  /* 2% threshold */
            int64_t adjustment = (int64_t)(out->phase_correction * out->clock_period_us);
            uint64_t new_period = out->clock_period_us - adjustment;
            
            /* Clamp adjustment to ±5% */
            uint64_t min_period = (uint64_t)(out->clock_period_us * 0.95);
            uint64_t max_period = (uint64_t)(out->clock_period_us * 1.05);
            new_period = new_period < min_period ? min_period : 
                        (new_period > max_period ? max_period : new_period);
            
            if (new_period != out->clock_period_us) {
                esp_timer_stop(out->clock_timer);
                esp_timer_start_periodic(out->clock_timer, new_period);
            }
        }
    }
    
    out->last_deck_phase = state->phase;
    out->last_sync_time_us = now_us;
    
    xSemaphoreGive(out->mutex);
}

bool midi_out_is_running(const midi_out_t *out) {
    if (!out) return false;
    return out->running;
}

uint32_t midi_out_get_pulse_count(const midi_out_t *out) {
    if (!out) return 0;
    return out->pulse_count;
}

void midi_out_send_cc(midi_out_t *out, uint8_t cc, uint8_t value) {
    if (!out || out->tx_pin < 0) return;
    
    xSemaphoreTake(out->mutex, portMAX_DELAY);
    
    midi_out_send_byte(out, MIDI_CC_STATUS(out->channel));
    midi_out_send_byte(out, cc & 0x7F);
    midi_out_send_byte(out, value & 0x7F);
    
    xSemaphoreGive(out->mutex);
}

void midi_out_send_spp(midi_out_t *out, uint16_t position) {
    if (!out || out->tx_pin < 0) return;
    
    /*
     * Song Position Pointer (SPP) format:
     * F2 ll mm
     * Where llmm is a 14-bit value representing position in "MIDI beats"
     * (1 MIDI beat = 6 MIDI clocks = 1/16 note)
     */
    uint8_t lsb = position & 0x7F;
    uint8_t msb = (position >> 7) & 0x7F;
    
    xSemaphoreTake(out->mutex, portMAX_DELAY);
    
    midi_out_send_byte(out, MIDI_SPP);
    midi_out_send_byte(out, lsb);
    midi_out_send_byte(out, msb);
    
    xSemaphoreGive(out->mutex);
    
    ESP_LOGD(TAG, "Sent SPP: %d MIDI beats", position);
}
