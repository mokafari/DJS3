/**
 * @file gt911.c
 * @brief GT911 capacitive touch controller driver implementation
 */

#include "gt911.h"
#include "board_config.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "gt911";

#define I2C_MASTER_NUM              I2C_NUM_0
#define I2C_MASTER_FREQ_HZ          400000
#define I2C_MASTER_TX_BUF_DISABLE   0
#define I2C_MASTER_RX_BUF_DISABLE   0

#define GT911_ADDR                  0x5D
#define GT911_READ_COORD_ADDR       0x814E

esp_err_t gt911_init(void) {
    ESP_LOGI(TAG, "Initializing GT911 on I2C pins SCL:%d SDA:%d", TOUCH_SCL_PIN, TOUCH_SDA_PIN);

    // 1. Reset GT911 and set I2C address
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << TOUCH_RES_PIN) | (1ULL << TOUCH_INT_PIN),
        .pull_down_en = 0,
        .pull_up_en = 0,
    };
    gpio_config(&io_conf);

    // Reset sequence for 0x5D address:
    // T1: INT low, RESET low
    gpio_set_level(TOUCH_INT_PIN, 0);
    gpio_set_level(TOUCH_RES_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    // T2: RESET high
    gpio_set_level(TOUCH_RES_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    // T3: INT input (or stay low for 0x5D)
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // Set INT to input mode for touch interrupts
    gpio_set_direction(TOUCH_INT_PIN, GPIO_MODE_INPUT);

    // 2. Initialize I2C
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = TOUCH_SDA_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = TOUCH_SCL_PIN,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master = { .clk_speed = I2C_MASTER_FREQ_HZ },
        .clk_flags = 0,
    };

    esp_err_t err = i2c_param_config(I2C_MASTER_NUM, &conf);
    if (err != ESP_OK) return err;
    
    err = i2c_driver_install(I2C_MASTER_NUM, conf.mode, I2C_MASTER_RX_BUF_DISABLE, I2C_MASTER_TX_BUF_DISABLE, 0);
    if (err != ESP_OK) return err;

    ESP_LOGI(TAG, "GT911 initialized successfully at 0x%02X", GT911_ADDR);
    return ESP_OK;
}

esp_err_t gt911_read(uint16_t *x, uint16_t *y, bool *pressed) {
    uint8_t data[7];
    uint8_t reg[2] = { (uint8_t)(GT911_READ_COORD_ADDR >> 8), (uint8_t)(GT911_READ_COORD_ADDR & 0xFF) };

    // Read point info and first touch point
    esp_err_t err = i2c_master_write_read_device(I2C_MASTER_NUM, GT911_ADDR, reg, 2, data, 7, pdMS_TO_TICKS(10));
    if (err != ESP_OK) return err;

    uint8_t touch_points = data[0] & 0x0F;
    bool buffer_ready = data[0] & 0x80;

    if (buffer_ready && touch_points > 0) {
        *pressed = true;
        *x = data[2] | (data[3] << 8);
        *y = data[4] | (data[5] << 8);
        
        // Clear buffer ready flag by writing 0 to the register
        uint8_t clear_reg[3] = { reg[0], reg[1], 0 };
        i2c_master_write_to_device(I2C_MASTER_NUM, GT911_ADDR, clear_reg, 3, pdMS_TO_TICKS(10));
    } else {
        *pressed = false;
        if (buffer_ready) {
            uint8_t clear_reg[3] = { reg[0], reg[1], 0 };
            i2c_master_write_to_device(I2C_MASTER_NUM, GT911_ADDR, clear_reg, 3, pdMS_TO_TICKS(10));
        }
    }

    return ESP_OK;
}
