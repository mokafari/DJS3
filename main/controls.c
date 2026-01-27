/**
 * @file controls.c
 * @brief DJ deck control inputs implementation
 */

#include "controls.h"
#include "board_config.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

static const char *TAG = "controls";

static button_event_cb_t button_callback = NULL;
static void *button_callback_arg = NULL;

// Button debounce state
#define DEBOUNCE_TIME_MS 50
static uint32_t button_last_change[BUTTON_COUNT] = {0};
static bool button_state[BUTTON_COUNT] = {0};
static bool button_last_state[BUTTON_COUNT] = {0};

// Jog wheel state
static int8_t jog_last_a = 0;
static int8_t jog_last_b = 0;
static int8_t jog_delta = 0;

// Pitch encoder state
static int8_t pitch_last_a = 0;
static int8_t pitch_last_b = 0;
static int8_t pitch_delta = 0;

// GPIO pin mappings
static const int button_pins[BUTTON_COUNT] = {
    [BUTTON_CUE] = BUTTON_CUE_PIN,
    [BUTTON_PLAY_PAUSE] = BUTTON_PLAY_PAUSE_PIN,
    [BUTTON_SYNC] = BUTTON_SYNC_PIN,
    [BUTTON_LOOP_IN] = BUTTON_LOOP_IN_PIN,
    [BUTTON_LOOP_OUT] = BUTTON_LOOP_OUT_PIN,
    [BUTTON_HOT_CUE_1] = BUTTON_HOT_CUE_1_PIN,
    [BUTTON_HOT_CUE_2] = BUTTON_HOT_CUE_2_PIN,
    [BUTTON_HOT_CUE_3] = BUTTON_HOT_CUE_3_PIN,
    [BUTTON_HOT_CUE_4] = BUTTON_HOT_CUE_4_PIN,
};

static void IRAM_ATTR gpio_isr_handler(void* arg) {
    uint32_t gpio_num = (uint32_t) arg;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    // Note: Actual debouncing handled in update function
    xHigherPriorityTaskWoken = pdFALSE;
}

bool controls_init(button_event_cb_t button_cb, void *arg) {
    ESP_LOGI(TAG, "Initializing controls");
    
    button_callback = button_cb;
    button_callback_arg = arg;
    
    // Configure button GPIOs
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_ANYEDGE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = 0,
        .pull_down_en = 0,
        .pull_up_en = 1, // Enable pull-up (buttons connect to GND when pressed)
    };
    
    // Add all button pins to bit mask
    for (int i = 0; i < BUTTON_COUNT; i++) {
        if (PIN_IS_VALID(button_pins[i])) {
            io_conf.pin_bit_mask |= (1ULL << button_pins[i]);
        }
    }
    
    gpio_config(&io_conf);
    
    // Install GPIO ISR service
    gpio_install_isr_service(0);
    
    // Attach ISR handlers
    for (int i = 0; i < BUTTON_COUNT; i++) {
        if (PIN_IS_VALID(button_pins[i])) {
            gpio_set_intr_type(button_pins[i], GPIO_INTR_ANYEDGE);
            gpio_isr_handler_add(button_pins[i], gpio_isr_handler, (void*)button_pins[i]);
        }
    }
    
    // Configure jog wheel encoder pins
    uint64_t jog_mask = 0;
    if (PIN_IS_VALID(JOG_WHEEL_A_PIN)) jog_mask |= (1ULL << JOG_WHEEL_A_PIN);
    if (PIN_IS_VALID(JOG_WHEEL_B_PIN)) jog_mask |= (1ULL << JOG_WHEEL_B_PIN);
    if (PIN_IS_VALID(JOG_WHEEL_TOUCH_PIN)) jog_mask |= (1ULL << JOG_WHEEL_TOUCH_PIN);

    gpio_config_t jog_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = jog_mask,
        .pull_down_en = 0,
        .pull_up_en = 1,
    };
    if (jog_mask != 0) {
        gpio_config(&jog_conf);
    }
    
    // Configure pitch encoder pins
    uint64_t pitch_mask = 0;
    if (PIN_IS_VALID(PITCH_ENCODER_A_PIN)) pitch_mask |= (1ULL << PITCH_ENCODER_A_PIN);
    if (PIN_IS_VALID(PITCH_ENCODER_B_PIN)) pitch_mask |= (1ULL << PITCH_ENCODER_B_PIN);

    gpio_config_t pitch_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = pitch_mask,
        .pull_down_en = 0,
        .pull_up_en = 1,
    };
    if (pitch_mask != 0) {
        gpio_config(&pitch_conf);
    }
    
    // Initialize state
    for (int i = 0; i < BUTTON_COUNT; i++) {
        if (PIN_IS_VALID(button_pins[i])) {
            button_state[i] = !gpio_get_level(button_pins[i]); // Inverted (pull-up)
        } else {
            button_state[i] = false;
        }
        button_last_state[i] = button_state[i];
        button_last_change[i] = 0;
    }
    
    jog_last_a = PIN_IS_VALID(JOG_WHEEL_A_PIN) ? gpio_get_level(JOG_WHEEL_A_PIN) : 0;
    jog_last_b = PIN_IS_VALID(JOG_WHEEL_B_PIN) ? gpio_get_level(JOG_WHEEL_B_PIN) : 0;
    pitch_last_a = PIN_IS_VALID(PITCH_ENCODER_A_PIN) ? gpio_get_level(PITCH_ENCODER_A_PIN) : 0;
    pitch_last_b = PIN_IS_VALID(PITCH_ENCODER_B_PIN) ? gpio_get_level(PITCH_ENCODER_B_PIN) : 0;
    
    ESP_LOGI(TAG, "Controls initialized");
    return true;
}

void controls_deinit(void) {
    // Remove ISR handlers
    for (int i = 0; i < BUTTON_COUNT; i++) {
        gpio_isr_handler_remove(button_pins[i]);
    }
    
    gpio_uninstall_isr_service();
    ESP_LOGI(TAG, "Controls deinitialized");
}

void controls_update(void) {
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    // Update buttons with debouncing
    for (int i = 0; i < BUTTON_COUNT; i++) {
        if (!PIN_IS_VALID(button_pins[i])) continue;
        
        bool current = !gpio_get_level(button_pins[i]); // Inverted
        
        if (current != button_last_state[i]) {
            button_last_change[i] = now;
        }
        
        if ((now - button_last_change[i]) > DEBOUNCE_TIME_MS) {
            if (current != button_state[i]) {
                button_state[i] = current;
                if (button_callback) {
                    button_callback((button_id_t)i, button_state[i], button_callback_arg);
                }
            }
        }
        
        button_last_state[i] = current;
    }
    
    // Update jog wheel encoder (quadrature decoding)
    int8_t jog_a = PIN_IS_VALID(JOG_WHEEL_A_PIN) ? gpio_get_level(JOG_WHEEL_A_PIN) : 0;
    int8_t jog_b = PIN_IS_VALID(JOG_WHEEL_B_PIN) ? gpio_get_level(JOG_WHEEL_B_PIN) : 0;
    
    if (jog_a != jog_last_a || jog_b != jog_last_b) {
        // Quadrature decoding
        int8_t state = (jog_last_a << 1) | jog_last_b;
        int8_t new_state = (jog_a << 1) | jog_b;
        
        // State transitions: 00->01->11->10->00 = forward, reverse = backward
        if (state == 0 && new_state == 1) jog_delta = 1;
        else if (state == 1 && new_state == 3) jog_delta = 1;
        else if (state == 3 && new_state == 2) jog_delta = 1;
        else if (state == 2 && new_state == 0) jog_delta = 1;
        else if (state == 0 && new_state == 2) jog_delta = -1;
        else if (state == 2 && new_state == 3) jog_delta = -1;
        else if (state == 3 && new_state == 1) jog_delta = -1;
        else if (state == 1 && new_state == 0) jog_delta = -1;
        
        jog_last_a = jog_a;
        jog_last_b = jog_b;
    }
    
    // Update pitch encoder
    int8_t pitch_a = PIN_IS_VALID(PITCH_ENCODER_A_PIN) ? gpio_get_level(PITCH_ENCODER_A_PIN) : 0;
    int8_t pitch_b = PIN_IS_VALID(PITCH_ENCODER_B_PIN) ? gpio_get_level(PITCH_ENCODER_B_PIN) : 0;
    
    if (pitch_a != pitch_last_a || pitch_b != pitch_last_b) {
        int8_t state = (pitch_last_a << 1) | pitch_last_b;
        int8_t new_state = (pitch_a << 1) | pitch_b;
        
        if (state == 0 && new_state == 1) pitch_delta = 1;
        else if (state == 1 && new_state == 3) pitch_delta = 1;
        else if (state == 3 && new_state == 2) pitch_delta = 1;
        else if (state == 2 && new_state == 0) pitch_delta = 1;
        else if (state == 0 && new_state == 2) pitch_delta = -1;
        else if (state == 2 && new_state == 3) pitch_delta = -1;
        else if (state == 3 && new_state == 1) pitch_delta = -1;
        else if (state == 1 && new_state == 0) pitch_delta = -1;
        
        pitch_last_a = pitch_a;
        pitch_last_b = pitch_b;
    }
}

bool controls_get_button(button_id_t button) {
    if (button >= BUTTON_COUNT) {
        return false;
    }
    return button_state[button];
}

int8_t controls_get_jog_delta(void) {
    int8_t delta = jog_delta;
    jog_delta = 0; // Reset after reading
    return delta;
}

bool controls_get_jog_touch(void) {
    if (!PIN_IS_VALID(JOG_WHEEL_TOUCH_PIN)) return false;
    return !gpio_get_level(JOG_WHEEL_TOUCH_PIN); // Inverted (pull-up)
}

int8_t controls_get_pitch_delta(void) {
    int8_t delta = pitch_delta;
    pitch_delta = 0; // Reset after reading
    return delta;
}

