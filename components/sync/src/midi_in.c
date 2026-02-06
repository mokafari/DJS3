/**
 * @file midi_in.c
 * @brief MIDI input receiver implementation
 * 
 * Implements MIDI input via UART with:
 * - Real-time message handling (clock, transport)
 * - CC mapping to deck controls
 * - BPM detection from MIDI clock
 * - Running status support
 */

#include "midi_in.h"
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

static const char *TAG = "midi_in";

/* ============================================================================
 * Constants
 * ============================================================================ */

#define MIDI_BAUD_RATE          31250
#define MIDI_RX_BUF_SIZE        256
#define MIDI_TX_BUF_SIZE        0       // No TX buffer needed for thru
#define MIDI_TASK_STACK_SIZE    4096
#define MIDI_TASK_PRIORITY      5

#define MAX_CC_MAPPINGS         64
#define CLOCK_TIMEOUT_US        500000  // 500ms = no clock
#define BPM_FILTER_ALPHA        0.1f    // EMA filter for BPM smoothing

/* ============================================================================
 * Internal Types
 * ============================================================================ */

/**
 * @brief MIDI parser state machine
 */
typedef enum {
    PARSER_IDLE,
    PARSER_STATUS,
    PARSER_DATA1,
    PARSER_DATA2
} parser_state_t;

/**
 * @brief MIDI input instance
 */
struct midi_in_s {
    // Configuration
    midi_in_config_t config;
    
    // UART
    int uart_num;
    bool running;
    TaskHandle_t rx_task;
    
    // Parser state
    parser_state_t parser_state;
    uint8_t running_status;
    uint8_t current_status;
    uint8_t data1;
    uint8_t data2;
    uint8_t expected_bytes;
    
    // CC mappings
    midi_cc_mapping_t mappings[MAX_CC_MAPPINGS];
    size_t mapping_count;
    SemaphoreHandle_t mapping_mutex;
    
    // Clock sync
    bool clock_active;
    bool transport_running;
    uint32_t clock_count;           // Clocks since start
    uint32_t beat_count;            // Beats since start
    int64_t last_clock_time_us;
    float detected_bpm;
    float clock_phase;              // 0.0-1.0 within beat
    
    // BPM averaging
    float clock_intervals[MIDI_CLOCKS_PER_BEAT];
    uint8_t interval_index;
    
    // Statistics
    uint32_t stat_clock_count;
    uint32_t stat_cc_count;
    uint32_t stat_note_count;
    uint32_t stat_error_count;
};

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

static void midi_rx_task(void *arg);
static void process_byte(midi_in_t *midi, uint8_t byte);
static void process_realtime(midi_in_t *midi, uint8_t byte);
static void process_channel_message(midi_in_t *midi);
static void process_system_message(midi_in_t *midi);
static void handle_cc(midi_in_t *midi, uint8_t channel, uint8_t cc, uint8_t value);
static void handle_note(midi_in_t *midi, uint8_t channel, uint8_t note, 
                        uint8_t velocity, bool on);
static void handle_clock_tick(midi_in_t *midi);
static void handle_transport(midi_in_t *midi, midi_transport_event_t event);
static uint8_t get_data_bytes_for_status(uint8_t status);
static float map_cc_value(const midi_cc_mapping_t *mapping, uint8_t value);

/* ============================================================================
 * Public Functions
 * ============================================================================ */

midi_in_config_t midi_in_get_default_config(void) {
    midi_in_config_t config = {
        .uart_num = 1,              // UART1 (UART0 is debug console)
        .rx_pin = 14,               // Default RX pin
        .tx_pin = -1,               // No TX by default
        .listen_channel = 0xFF,     // Omni (all channels)
        .enable_clock_sync = true,
        .enable_thru = false,
        .control_cb = NULL,
        .transport_cb = NULL,
        .clock_cb = NULL,
        .note_cb = NULL,
        .callback_arg = NULL
    };
    return config;
}

midi_in_t *midi_in_create(const midi_in_config_t *config) {
    if (!config) {
        ESP_LOGE(TAG, "Config is NULL");
        return NULL;
    }
    
    midi_in_t *midi = (midi_in_t *)calloc(1, sizeof(midi_in_t));
    if (!midi) {
        ESP_LOGE(TAG, "Failed to allocate MIDI instance");
        return NULL;
    }
    
    memcpy(&midi->config, config, sizeof(midi_in_config_t));
    midi->uart_num = config->uart_num;
    
    // Create mutex for mapping table
    midi->mapping_mutex = xSemaphoreCreateMutex();
    if (!midi->mapping_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        free(midi);
        return NULL;
    }
    
    // Configure UART for MIDI (31250 baud, 8N1)
    uart_config_t uart_config = {
        .baud_rate = MIDI_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    
    esp_err_t err = uart_param_config(midi->uart_num, &uart_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART config failed: %s", esp_err_to_name(err));
        vSemaphoreDelete(midi->mapping_mutex);
        free(midi);
        return NULL;
    }
    
    // Set pins
    int tx_pin = config->tx_pin >= 0 ? config->tx_pin : UART_PIN_NO_CHANGE;
    int rx_pin = config->rx_pin >= 0 ? config->rx_pin : UART_PIN_NO_CHANGE;
    
    err = uart_set_pin(midi->uart_num, tx_pin, rx_pin, 
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART set pin failed: %s", esp_err_to_name(err));
        vSemaphoreDelete(midi->mapping_mutex);
        free(midi);
        return NULL;
    }
    
    // Install UART driver
    err = uart_driver_install(midi->uart_num, MIDI_RX_BUF_SIZE, 
                              MIDI_TX_BUF_SIZE, 0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART driver install failed: %s", esp_err_to_name(err));
        vSemaphoreDelete(midi->mapping_mutex);
        free(midi);
        return NULL;
    }
    
    // Initialize state
    midi->parser_state = PARSER_IDLE;
    midi->detected_bpm = 120.0f;  // Default BPM
    
    ESP_LOGI(TAG, "MIDI input created on UART%d (RX:%d, TX:%d)", 
             midi->uart_num, config->rx_pin, config->tx_pin);
    
    return midi;
}

void midi_in_destroy(midi_in_t *midi) {
    if (!midi) return;
    
    midi_in_stop(midi);
    
    uart_driver_delete(midi->uart_num);
    
    if (midi->mapping_mutex) {
        vSemaphoreDelete(midi->mapping_mutex);
    }
    
    free(midi);
    ESP_LOGI(TAG, "MIDI input destroyed");
}

bool midi_in_start(midi_in_t *midi) {
    if (!midi || midi->running) return false;
    
    midi->running = true;
    
    BaseType_t ret = xTaskCreate(
        midi_rx_task,
        "midi_rx",
        MIDI_TASK_STACK_SIZE,
        midi,
        MIDI_TASK_PRIORITY,
        &midi->rx_task
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create RX task");
        midi->running = false;
        return false;
    }
    
    ESP_LOGI(TAG, "MIDI input started");
    return true;
}

void midi_in_stop(midi_in_t *midi) {
    if (!midi || !midi->running) return;
    
    midi->running = false;
    
    if (midi->rx_task) {
        // Wait for task to exit
        vTaskDelay(pdMS_TO_TICKS(100));
        midi->rx_task = NULL;
    }
    
    ESP_LOGI(TAG, "MIDI input stopped");
}

/* ============================================================================
 * CC Mapping
 * ============================================================================ */

bool midi_in_add_cc_mapping(midi_in_t *midi, const midi_cc_mapping_t *mapping) {
    if (!midi || !mapping) return false;
    
    xSemaphoreTake(midi->mapping_mutex, portMAX_DELAY);
    
    if (midi->mapping_count >= MAX_CC_MAPPINGS) {
        xSemaphoreGive(midi->mapping_mutex);
        ESP_LOGW(TAG, "CC mapping table full");
        return false;
    }
    
    // Check for duplicate and replace
    for (size_t i = 0; i < midi->mapping_count; i++) {
        if (midi->mappings[i].cc_number == mapping->cc_number &&
            midi->mappings[i].channel == mapping->channel) {
            midi->mappings[i] = *mapping;
            xSemaphoreGive(midi->mapping_mutex);
            ESP_LOGD(TAG, "Replaced CC %d mapping", mapping->cc_number);
            return true;
        }
    }
    
    // Add new mapping
    midi->mappings[midi->mapping_count++] = *mapping;
    xSemaphoreGive(midi->mapping_mutex);
    
    ESP_LOGD(TAG, "Added CC %d -> Deck %d, Ctrl %d", 
             mapping->cc_number, mapping->deck, mapping->ctrl);
    return true;
}

void midi_in_remove_cc_mapping(midi_in_t *midi, uint8_t cc_number, uint8_t channel) {
    if (!midi) return;
    
    xSemaphoreTake(midi->mapping_mutex, portMAX_DELAY);
    
    for (size_t i = 0; i < midi->mapping_count; i++) {
        if (midi->mappings[i].cc_number == cc_number &&
            (channel == 0xFF || midi->mappings[i].channel == channel)) {
            // Shift remaining entries
            memmove(&midi->mappings[i], &midi->mappings[i + 1],
                    (midi->mapping_count - i - 1) * sizeof(midi_cc_mapping_t));
            midi->mapping_count--;
            i--;  // Check same index again
        }
    }
    
    xSemaphoreGive(midi->mapping_mutex);
}

void midi_in_clear_mappings(midi_in_t *midi) {
    if (!midi) return;
    
    xSemaphoreTake(midi->mapping_mutex, portMAX_DELAY);
    midi->mapping_count = 0;
    xSemaphoreGive(midi->mapping_mutex);
}

void midi_in_load_default_mappings(midi_in_t *midi) {
    if (!midi) return;
    
    midi_in_clear_mappings(midi);
    
    // Deck A mappings (Channel 1)
    midi_cc_mapping_t deck_a_mappings[] = {
        { .cc_number = MIDI_CC_VOLUME, .channel = 0, .deck = MIDI_DECK_A,
          .ctrl = MIDI_CTRL_VOLUME, .min_value = 0.0f, .max_value = 1.0f },
        { .cc_number = MIDI_CC_TEMPO, .channel = 0, .deck = MIDI_DECK_A,
          .ctrl = MIDI_CTRL_TEMPO, .min_value = -0.5f, .max_value = 0.5f },
        { .cc_number = MIDI_CC_FILTER, .channel = 0, .deck = MIDI_DECK_A,
          .ctrl = MIDI_CTRL_FILTER_CUTOFF, .min_value = 0.0f, .max_value = 1.0f },
        { .cc_number = MIDI_CC_EQ_LOW, .channel = 0, .deck = MIDI_DECK_A,
          .ctrl = MIDI_CTRL_EQ_LOW, .min_value = -1.0f, .max_value = 1.0f },
        { .cc_number = MIDI_CC_EQ_MID, .channel = 0, .deck = MIDI_DECK_A,
          .ctrl = MIDI_CTRL_EQ_MID, .min_value = -1.0f, .max_value = 1.0f },
        { .cc_number = MIDI_CC_EQ_HIGH, .channel = 0, .deck = MIDI_DECK_A,
          .ctrl = MIDI_CTRL_EQ_HIGH, .min_value = -1.0f, .max_value = 1.0f },
    };
    
    // Deck B mappings (Channel 2)
    midi_cc_mapping_t deck_b_mappings[] = {
        { .cc_number = MIDI_CC_VOLUME, .channel = 1, .deck = MIDI_DECK_B,
          .ctrl = MIDI_CTRL_VOLUME, .min_value = 0.0f, .max_value = 1.0f },
        { .cc_number = MIDI_CC_TEMPO, .channel = 1, .deck = MIDI_DECK_B,
          .ctrl = MIDI_CTRL_TEMPO, .min_value = -0.5f, .max_value = 0.5f },
        { .cc_number = MIDI_CC_FILTER, .channel = 1, .deck = MIDI_DECK_B,
          .ctrl = MIDI_CTRL_FILTER_CUTOFF, .min_value = 0.0f, .max_value = 1.0f },
        { .cc_number = MIDI_CC_EQ_LOW, .channel = 1, .deck = MIDI_DECK_B,
          .ctrl = MIDI_CTRL_EQ_LOW, .min_value = -1.0f, .max_value = 1.0f },
        { .cc_number = MIDI_CC_EQ_MID, .channel = 1, .deck = MIDI_DECK_B,
          .ctrl = MIDI_CTRL_EQ_MID, .min_value = -1.0f, .max_value = 1.0f },
        { .cc_number = MIDI_CC_EQ_HIGH, .channel = 1, .deck = MIDI_DECK_B,
          .ctrl = MIDI_CTRL_EQ_HIGH, .min_value = -1.0f, .max_value = 1.0f },
    };
    
    // Master mappings (Channel 16)
    midi_cc_mapping_t master_mappings[] = {
        { .cc_number = MIDI_CC_CROSSFADER, .channel = 15, .deck = MIDI_DECK_MASTER,
          .ctrl = MIDI_CTRL_CROSSFADER, .min_value = 0.0f, .max_value = 1.0f },
        { .cc_number = MIDI_CC_VOLUME, .channel = 15, .deck = MIDI_DECK_MASTER,
          .ctrl = MIDI_CTRL_VOLUME, .min_value = 0.0f, .max_value = 1.0f },
        { .cc_number = MIDI_CC_EFFECT_WET, .channel = 15, .deck = MIDI_DECK_MASTER,
          .ctrl = MIDI_CTRL_EFFECT_WET, .min_value = 0.0f, .max_value = 1.0f },
    };
    
    for (size_t i = 0; i < sizeof(deck_a_mappings) / sizeof(deck_a_mappings[0]); i++) {
        midi_in_add_cc_mapping(midi, &deck_a_mappings[i]);
    }
    for (size_t i = 0; i < sizeof(deck_b_mappings) / sizeof(deck_b_mappings[0]); i++) {
        midi_in_add_cc_mapping(midi, &deck_b_mappings[i]);
    }
    for (size_t i = 0; i < sizeof(master_mappings) / sizeof(master_mappings[0]); i++) {
        midi_in_add_cc_mapping(midi, &master_mappings[i]);
    }
    
    ESP_LOGI(TAG, "Loaded %zu default CC mappings", midi->mapping_count);
}

/* ============================================================================
 * Clock Sync
 * ============================================================================ */

float midi_in_get_bpm(const midi_in_t *midi) {
    if (!midi) return 120.0f;
    return midi->detected_bpm;
}

float midi_in_get_phase(const midi_in_t *midi) {
    if (!midi) return 0.0f;
    return midi->clock_phase;
}

bool midi_in_is_clock_active(const midi_in_t *midi) {
    if (!midi) return false;
    
    // Check if we've received a clock tick recently
    int64_t now = esp_timer_get_time();
    return midi->clock_active && 
           (now - midi->last_clock_time_us) < CLOCK_TIMEOUT_US;
}

bool midi_in_is_transport_running(const midi_in_t *midi) {
    if (!midi) return false;
    return midi->transport_running;
}

uint32_t midi_in_get_beat_count(const midi_in_t *midi) {
    if (!midi) return 0;
    return midi->beat_count;
}

/* ============================================================================
 * Statistics
 * ============================================================================ */

void midi_in_get_stats(const midi_in_t *midi, uint32_t *clock_count,
                       uint32_t *cc_count, uint32_t *note_count,
                       uint32_t *error_count) {
    if (!midi) return;
    
    if (clock_count) *clock_count = midi->stat_clock_count;
    if (cc_count) *cc_count = midi->stat_cc_count;
    if (note_count) *note_count = midi->stat_note_count;
    if (error_count) *error_count = midi->stat_error_count;
}

void midi_in_reset_stats(midi_in_t *midi) {
    if (!midi) return;
    
    midi->stat_clock_count = 0;
    midi->stat_cc_count = 0;
    midi->stat_note_count = 0;
    midi->stat_error_count = 0;
}

/* ============================================================================
 * Internal Functions
 * ============================================================================ */

/**
 * @brief UART receive task
 */
static void midi_rx_task(void *arg) {
    midi_in_t *midi = (midi_in_t *)arg;
    uint8_t data[32];
    
    ESP_LOGI(TAG, "MIDI RX task started");
    
    while (midi->running) {
        int len = uart_read_bytes(midi->uart_num, data, sizeof(data), 
                                  pdMS_TO_TICKS(10));
        
        if (len > 0) {
            for (int i = 0; i < len; i++) {
                process_byte(midi, data[i]);
                
                // MIDI thru: echo input to output
                if (midi->config.enable_thru && midi->config.tx_pin >= 0) {
                    uart_write_bytes(midi->uart_num, &data[i], 1);
                }
            }
        }
        
        // Check for clock timeout
        if (midi->clock_active) {
            int64_t now = esp_timer_get_time();
            if ((now - midi->last_clock_time_us) > CLOCK_TIMEOUT_US) {
                midi->clock_active = false;
                ESP_LOGD(TAG, "MIDI clock timeout");
            }
        }
    }
    
    ESP_LOGI(TAG, "MIDI RX task exiting");
    vTaskDelete(NULL);
}

/**
 * @brief Process a single MIDI byte
 */
static void process_byte(midi_in_t *midi, uint8_t byte) {
    // Real-time messages can occur anywhere
    if (byte >= 0xF8) {
        process_realtime(midi, byte);
        return;
    }
    
    // Status byte (high bit set)
    if (byte & 0x80) {
        // System common messages (0xF0-0xF7)
        if ((byte & 0xF0) == 0xF0) {
            midi->current_status = byte;
            midi->running_status = 0;  // Clear running status
            midi->parser_state = PARSER_STATUS;
            midi->expected_bytes = get_data_bytes_for_status(byte);
            
            if (midi->expected_bytes == 0) {
                process_system_message(midi);
                midi->parser_state = PARSER_IDLE;
            }
        } else {
            // Channel voice message
            midi->current_status = byte;
            midi->running_status = byte;  // Save for running status
            midi->parser_state = PARSER_DATA1;
            midi->expected_bytes = get_data_bytes_for_status(byte);
        }
        return;
    }
    
    // Data byte (high bit clear)
    switch (midi->parser_state) {
        case PARSER_IDLE:
            // Data byte without status - use running status
            if (midi->running_status) {
                midi->current_status = midi->running_status;
                midi->expected_bytes = get_data_bytes_for_status(midi->running_status);
                midi->data1 = byte;
                
                if (midi->expected_bytes == 1) {
                    process_channel_message(midi);
                    // Stay ready for more data with running status
                } else {
                    midi->parser_state = PARSER_DATA2;
                }
            } else {
                midi->stat_error_count++;
            }
            break;
            
        case PARSER_STATUS:
            midi->data1 = byte;
            if (midi->expected_bytes == 1) {
                process_system_message(midi);
                midi->parser_state = PARSER_IDLE;
            } else {
                midi->parser_state = PARSER_DATA2;
            }
            break;
            
        case PARSER_DATA1:
            midi->data1 = byte;
            if (midi->expected_bytes == 1) {
                process_channel_message(midi);
                midi->parser_state = PARSER_DATA1;  // Ready for next with running status
            } else {
                midi->parser_state = PARSER_DATA2;
            }
            break;
            
        case PARSER_DATA2:
            midi->data2 = byte;
            if ((midi->current_status & 0xF0) == 0xF0) {
                process_system_message(midi);
                midi->parser_state = PARSER_IDLE;
            } else {
                process_channel_message(midi);
                midi->parser_state = PARSER_DATA1;  // Ready for next with running status
            }
            break;
    }
}

/**
 * @brief Get expected data bytes for a status byte
 */
static uint8_t get_data_bytes_for_status(uint8_t status) {
    switch (status & 0xF0) {
        case MIDI_STATUS_NOTE_OFF:
        case MIDI_STATUS_NOTE_ON:
        case MIDI_STATUS_POLY_PRESSURE:
        case MIDI_STATUS_CONTROL_CHANGE:
        case MIDI_STATUS_PITCH_BEND:
            return 2;
            
        case MIDI_STATUS_PROGRAM_CHANGE:
        case MIDI_STATUS_CHANNEL_PRESSURE:
            return 1;
            
        default:
            break;
    }
    
    // System common
    switch (status) {
        case MIDI_SYS_SONG_POSITION:
            return 2;
        case MIDI_SYS_SONG_SELECT:
            return 1;
        default:
            return 0;
    }
}

/**
 * @brief Process real-time messages
 */
static void process_realtime(midi_in_t *midi, uint8_t byte) {
    switch (byte) {
        case MIDI_RT_CLOCK:
            if (midi->config.enable_clock_sync) {
                handle_clock_tick(midi);
            }
            break;
            
        case MIDI_RT_START:
            midi->transport_running = true;
            midi->clock_count = 0;
            midi->beat_count = 0;
            midi->clock_phase = 0.0f;
            handle_transport(midi, MIDI_TRANSPORT_PLAY);
            ESP_LOGD(TAG, "MIDI Start");
            break;
            
        case MIDI_RT_STOP:
            midi->transport_running = false;
            handle_transport(midi, MIDI_TRANSPORT_STOP);
            ESP_LOGD(TAG, "MIDI Stop");
            break;
            
        case MIDI_RT_CONTINUE:
            midi->transport_running = true;
            handle_transport(midi, MIDI_TRANSPORT_CONTINUE);
            ESP_LOGD(TAG, "MIDI Continue");
            break;
            
        case MIDI_RT_ACTIVE_SENSE:
            // Just acknowledge - could use for connection detection
            break;
            
        case MIDI_RT_RESET:
            midi->transport_running = false;
            midi->clock_count = 0;
            midi->beat_count = 0;
            midi->parser_state = PARSER_IDLE;
            midi->running_status = 0;
            ESP_LOGI(TAG, "MIDI Reset");
            break;
    }
}

/**
 * @brief Process channel voice messages
 */
static void process_channel_message(midi_in_t *midi) {
    uint8_t msg_type = midi->current_status & 0xF0;
    uint8_t channel = midi->current_status & 0x0F;
    
    // Channel filter
    if (midi->config.listen_channel != 0xFF && 
        channel != midi->config.listen_channel) {
        return;
    }
    
    switch (msg_type) {
        case MIDI_STATUS_NOTE_ON:
            if (midi->data2 > 0) {
                handle_note(midi, channel, midi->data1, midi->data2, true);
            } else {
                // Velocity 0 = note off
                handle_note(midi, channel, midi->data1, 0, false);
            }
            break;
            
        case MIDI_STATUS_NOTE_OFF:
            handle_note(midi, channel, midi->data1, midi->data2, false);
            break;
            
        case MIDI_STATUS_CONTROL_CHANGE:
            handle_cc(midi, channel, midi->data1, midi->data2);
            break;
            
        case MIDI_STATUS_PITCH_BEND:
            // Pitch bend: 14-bit value (data1 = LSB, data2 = MSB)
            // Could map to jog wheel / scratch
            {
                int16_t bend = ((int16_t)midi->data2 << 7) | midi->data1;
                bend -= 8192;  // Center at 0
                float bend_norm = (float)bend / 8192.0f;
                
                // Call control callback for pitch bend
                if (midi->config.control_cb) {
                    midi_deck_t deck = (channel < 8) ? MIDI_DECK_A : MIDI_DECK_B;
                    midi->config.control_cb(deck, MIDI_CTRL_JOG_PITCH, bend_norm,
                                           midi->config.callback_arg);
                }
            }
            break;
            
        case MIDI_STATUS_PROGRAM_CHANGE:
            // Could use for preset selection
            ESP_LOGD(TAG, "Program Change: ch=%d prog=%d", channel, midi->data1);
            break;
            
        default:
            break;
    }
}

/**
 * @brief Process system common messages
 */
static void process_system_message(midi_in_t *midi) {
    switch (midi->current_status) {
        case MIDI_SYS_SONG_POSITION:
            // Song position pointer (for transport sync)
            {
                uint16_t position = ((uint16_t)midi->data2 << 7) | midi->data1;
                // Position is in MIDI beats (1/16 notes)
                midi->beat_count = position / 4;
                midi->clock_count = position * 6;  // 6 clocks per 1/16 note
                ESP_LOGD(TAG, "Song Position: %d (beat %d)", position, midi->beat_count);
            }
            break;
            
        case MIDI_SYS_SONG_SELECT:
            ESP_LOGD(TAG, "Song Select: %d", midi->data1);
            break;
            
        case MIDI_SYS_SYSEX_START:
            // TODO: Handle SysEx if needed
            break;
            
        default:
            break;
    }
}

/**
 * @brief Handle CC message
 */
static void handle_cc(midi_in_t *midi, uint8_t channel, uint8_t cc, uint8_t value) {
    midi->stat_cc_count++;
    
    ESP_LOGD(TAG, "CC: ch=%d cc=%d val=%d", channel, cc, value);
    
    xSemaphoreTake(midi->mapping_mutex, portMAX_DELAY);
    
    for (size_t i = 0; i < midi->mapping_count; i++) {
        midi_cc_mapping_t *m = &midi->mappings[i];
        
        if (m->cc_number == cc && 
            (m->channel == 0xFF || m->channel == channel)) {
            
            float mapped_value = map_cc_value(m, value);
            
            if (midi->config.control_cb) {
                midi->config.control_cb(m->deck, m->ctrl, mapped_value,
                                       midi->config.callback_arg);
            }
            
            ESP_LOGD(TAG, "Mapped CC %d -> Deck %d, Ctrl %d, Val %.3f",
                     cc, m->deck, m->ctrl, mapped_value);
        }
    }
    
    xSemaphoreGive(midi->mapping_mutex);
}

/**
 * @brief Map CC value (0-127) to control range
 */
static float map_cc_value(const midi_cc_mapping_t *mapping, uint8_t value) {
    float normalized = (float)value / 127.0f;
    
    if (mapping->invert) {
        normalized = 1.0f - normalized;
    }
    
    return mapping->min_value + normalized * (mapping->max_value - mapping->min_value);
}

/**
 * @brief Handle note message
 */
static void handle_note(midi_in_t *midi, uint8_t channel, uint8_t note, 
                        uint8_t velocity, bool on) {
    midi->stat_note_count++;
    
    ESP_LOGD(TAG, "Note %s: ch=%d note=%d vel=%d", 
             on ? "On" : "Off", channel, note, velocity);
    
    if (midi->config.note_cb) {
        midi->config.note_cb(channel, note, on ? velocity : 0,
                            midi->config.callback_arg);
    }
}

/**
 * @brief Handle MIDI clock tick
 */
static void handle_clock_tick(midi_in_t *midi) {
    int64_t now = esp_timer_get_time();
    
    midi->stat_clock_count++;
    midi->clock_active = true;
    
    // Calculate BPM from clock interval
    if (midi->last_clock_time_us > 0) {
        int64_t delta_us = now - midi->last_clock_time_us;
        
        if (delta_us > 0 && delta_us < 1000000) {  // Sanity check (< 1 sec)
            // Store interval for averaging
            midi->clock_intervals[midi->interval_index] = (float)delta_us;
            midi->interval_index = (midi->interval_index + 1) % MIDI_CLOCKS_PER_BEAT;
            
            // Calculate average interval over one beat
            float avg_interval = 0.0f;
            for (int i = 0; i < MIDI_CLOCKS_PER_BEAT; i++) {
                avg_interval += midi->clock_intervals[i];
            }
            avg_interval /= MIDI_CLOCKS_PER_BEAT;
            
            if (avg_interval > 0) {
                // BPM = 60 / (avg_interval * 24 / 1e6)
                float instant_bpm = 60000000.0f / (avg_interval * MIDI_CLOCKS_PER_BEAT);
                
                // Clamp to reasonable range
                instant_bpm = fmaxf(40.0f, fminf(300.0f, instant_bpm));
                
                // Smooth with EMA filter
                midi->detected_bpm = midi->detected_bpm * (1.0f - BPM_FILTER_ALPHA) +
                                     instant_bpm * BPM_FILTER_ALPHA;
            }
        }
    }
    
    midi->last_clock_time_us = now;
    midi->clock_count++;
    
    // Calculate phase (0.0-1.0 within beat)
    midi->clock_phase = (float)(midi->clock_count % MIDI_CLOCKS_PER_BEAT) / 
                        (float)MIDI_CLOCKS_PER_BEAT;
    
    // Count beats
    if ((midi->clock_count % MIDI_CLOCKS_PER_BEAT) == 0) {
        midi->beat_count++;
        
        // Call clock callback on beat boundary
        if (midi->config.clock_cb) {
            midi->config.clock_cb(midi->detected_bpm, midi->clock_phase,
                                 midi->beat_count, midi->config.callback_arg);
        }
    }
}

/**
 * @brief Handle transport event
 */
static void handle_transport(midi_in_t *midi, midi_transport_event_t event) {
    if (midi->config.transport_cb) {
        midi->config.transport_cb(event, MIDI_DECK_MASTER, 
                                  midi->config.callback_arg);
    }
}
