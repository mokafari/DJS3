/**
 * @file display.c
 * @brief NV3041A display driver implementation
 * 
 * NV3041A is a 480x272 RGB565 display controller with QSPI interface.
 * This implementation uses the QSPI protocol with embe
 dded command/data.
 * Based on Arduino_ESP32QSPI from Arduino_GFX library.
 */

#include "display.h"
#include "board_config.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/gpio_reg.h"
#include "soc/gpio_struct.h"
#include <string.h>

static const char *TAG = "display";
static spi_device_handle_t spi_handle = NULL;
static uint16_t *framebuffer = NULL;
static bool display_initialized = false;

// QSPI protocol constants (matching Arduino_ESP32QSPI)
#define QSPI_FREQUENCY 80000000  // 80MHz
#define QSPI_SPI_MODE 0  // SPI_MODE0 = 0
#define QSPI_SPI_HOST SPI2_HOST
#define QSPI_DMA_CHANNEL SPI_DMA_CH_AUTO
#define MAX_PIXELS_AT_ONCE 1024

// MSB byte order macros (from Arduino_GFX)
// MSB_16 swaps bytes: ((val)&0xFF00) >> 8) | (((val)&0xFF) << 8)
#define MSB_16(val) (((val)&0xFF00) >> 8) | (((val)&0xFF) << 8)
#define MSB_16_SET(var, val) \
    (var) = MSB_16(val);

#define MSB_32_16_16_SET(var, v1, v2) \
    (var) = (((uint32_t)v2 & 0xff00) << 8) | (((uint32_t)v2 & 0xff) << 24) | ((v1 & 0xff00) >> 8) | ((v1 & 0xff) << 8);

// CS pin control (manual, using GPIO registers)
static uint32_t cs_pin_mask;
static volatile uint32_t *cs_port_set;
static volatile uint32_t *cs_port_clr;

static inline void CS_LOW(void) {
    *cs_port_clr = cs_pin_mask;
}

static inline void CS_HIGH(void) {
    *cs_port_set = cs_pin_mask;
}

// NV3041A command definitions
#define NV3041A_NOP             0x00
#define NV3041A_SWRESET         0x01
#define NV3041A_SLPOUT          0x11
#define NV3041A_DISPON          0x29
#define NV3041A_CASET           0x2A
#define NV3041A_RASET           0x2B
#define NV3041A_RAMWR           0x2C
#define NV3041A_MADCTL          0x36
#define NV3041A_COLMOD          0x3A

// Initialization sequence operation codes (matching Arduino_DataBus.h)
#define BEGIN_WRITE             0x00
#define WRITE_COMMAND_8         0x01
#define WRITE_COMMAND_16        0x02
#define WRITE_COMMAND_BYTES     0x03
#define WRITE_DATA_8            0x04
#define WRITE_DATA_16           0x05
#define WRITE_BYTES             0x06
#define WRITE_C8_D8             0x07
#define WRITE_C8_D16            0x08
#define WRITE_C8_BYTES          0x09
#define WRITE_C16_D16           0x0A
#define END_WRITE               0x0B
#define DELAY                   0x0C

// Full NV3041A initialization sequence (extracted from Arduino_NV3041A.h)
// This sequence configures power supply, timing, gamma correction, and display mode
static const uint8_t nv3041a_init_operations[] = {
    BEGIN_WRITE,
    WRITE_C8_D8, 0xff, 0xa5,  // Unlock sequence
    
    WRITE_C8_D8, 0x36, 0xc0,  // MADCTL: Memory access control
    
    WRITE_C8_D8, 0x3A, 0x01,  // COLMOD: Pixel format (01=RGB565, 00=RGB666)
    
    WRITE_C8_D8, 0x41, 0x03,  // Interface mode (03=16-bit)
    
    WRITE_C8_D8, 0x44, 0x15,  // VBP (Vertical Back Porch)
    WRITE_C8_D8, 0x45, 0x15,  // VFP (Vertical Front Porch)
    
    // Power supply configuration
    WRITE_C8_D8, 0x7d, 0x03,  // vdds_trim[2:0]
    WRITE_C8_D8, 0xc1, 0xab,  // avdd_clp_en avdd_clp[1:0] avcl_clp_en avcl_clp[1:0]
    WRITE_C8_D8, 0xc2, 0x17,  // vgl_clp_en vgl_clp[2:0]
    WRITE_C8_D8, 0xc3, 0x10,  // vgl_clp_en vgl_clp[2:0]
    WRITE_C8_D8, 0xc6, 0x3a,  // avdd_ratio_sel avcl_ratio_sel vgh_ratio_sel[1:0] vgl_ratio_sel[1:0]
    WRITE_C8_D8, 0xc7, 0x25,  // mv_clk_sel[1:0] avdd_clk_sel[1:0] avcl_clk_sel[1:0]
    WRITE_C8_D8, 0xc8, 0x11,  // VGL_CLK_sel
    WRITE_C8_D8, 0x7a, 0x49,  // user_vgsp
    WRITE_C8_D8, 0x6f, 0x2f,  // user_gvdd
    WRITE_C8_D8, 0x78, 0x4b,  // user_gvcl
    WRITE_C8_D8, 0xc9, 0x00,
    WRITE_C8_D8, 0x67, 0x33,
    
    // Gate timing
    WRITE_C8_D8, 0x51, 0x4b,  // gate_st_o[7:0]
    WRITE_C8_D8, 0x52, 0x7c,  // gate_ed_o[7:0]
    WRITE_C8_D8, 0x53, 0x1c,  // gate_st_e[7:0]
    WRITE_C8_D8, 0x54, 0x77,  // gate_ed_e[7:0]
    
    // Source timing
    WRITE_C8_D8, 0x46, 0x0a,  // fsm_hbp_o[5:0]
    WRITE_C8_D8, 0x47, 0x2a,  // fsm_hfp_o[5:0]
    WRITE_C8_D8, 0x48, 0x0a,  // fsm_hbp_e[5:0]
    WRITE_C8_D8, 0x49, 0x1a,  // fsm_hfp_e[5:0]
    WRITE_C8_D8, 0x56, 0x43,  // src_ld_wd[1:0] src_ld_st[5:0]
    WRITE_C8_D8, 0x57, 0x42,  // pn_cs_en src_cs_st[5:0]
    WRITE_C8_D8, 0x58, 0x3c,  // src_cs_p_wd[6:0]
    WRITE_C8_D8, 0x59, 0x64,  // src_cs_n_wd[6:0]
    WRITE_C8_D8, 0x5a, 0x41,  // src_pchg_st_o[6:0]
    WRITE_C8_D8, 0x5b, 0x3c,  // src_pchg_wd_o[6:0]
    WRITE_C8_D8, 0x5c, 0x02,  // src_pchg_st_e[6:0]
    WRITE_C8_D8, 0x5d, 0x3c,  // src_pchg_wd_e[6:0]
    WRITE_C8_D8, 0x5e, 0x1f,  // src_pol_sw[7:0]
    WRITE_C8_D8, 0x60, 0x80,  // src_op_st_o[7:0]
    WRITE_C8_D8, 0x61, 0x3f,  // src_op_st_e[7:0]
    WRITE_C8_D8, 0x62, 0x21,  // src_op_ed_o[9:8] src_op_ed_e[9:8]
    WRITE_C8_D8, 0x63, 0x07,  // src_op_ed_o[7:0]
    WRITE_C8_D8, 0x64, 0xe0,  // src_op_ed_e[7:0]
    WRITE_C8_D8, 0x65, 0x01,  // chopper
    
    // Mux timing
    WRITE_C8_D8, 0xca, 0x20,  // avdd_mux_st_o[7:0]
    WRITE_C8_D8, 0xcb, 0x52,  // avdd_mux_ed_o[7:0]
    WRITE_C8_D8, 0xcc, 0x10,  // avdd_mux_st_e[7:0]
    WRITE_C8_D8, 0xcD, 0x42,  // avdd_mux_ed_e[7:0]
    WRITE_C8_D8, 0xD0, 0x20,  // avcl_mux_st_o[7:0]
    WRITE_C8_D8, 0xD1, 0x52,  // avcl_mux_ed_o[7:0]
    WRITE_C8_D8, 0xD2, 0x10,  // avcl_mux_st_e[7:0]
    WRITE_C8_D8, 0xD3, 0x42,  // avcl_mux_ed_e[7:0]
    WRITE_C8_D8, 0xD4, 0x0a,  // vgh_mux_st[7:0]
    WRITE_C8_D8, 0xD5, 0x32,  // vgh_mux_ed[7:0]
    
    // Gamma correction (positive)
    WRITE_C8_D8, 0x80, 0x04,  // gam_vrp0
    WRITE_C8_D8, 0x81, 0x07,  // gam_vrp1
    WRITE_C8_D8, 0x82, 0x06,  // gam_vrp2
    WRITE_C8_D8, 0x86, 0x2c,  // gam_prp0
    WRITE_C8_D8, 0x87, 0x46,  // gam_prp1
    WRITE_C8_D8, 0x83, 0x39,  // gam_vrp3
    WRITE_C8_D8, 0x84, 0x3a,  // gam_vrp4
    WRITE_C8_D8, 0x85, 0x3f,  // gam_vrp5
    WRITE_C8_D8, 0x88, 0x08,  // gam_pkp0
    WRITE_C8_D8, 0x89, 0x0f,  // gam_pkp1
    WRITE_C8_D8, 0x8a, 0x17,  // gam_pkp2
    WRITE_C8_D8, 0x8b, 0x10,  // gam_PKP3
    WRITE_C8_D8, 0x8c, 0x16,  // gam_PKP4
    WRITE_C8_D8, 0x8d, 0x14,  // gam_PKP5
    WRITE_C8_D8, 0x8e, 0x11,  // gam_PKP6
    WRITE_C8_D8, 0x8f, 0x14,  // gam_PKP7
    WRITE_C8_D8, 0x90, 0x06,  // gam_PKP8
    WRITE_C8_D8, 0x91, 0x0f,  // gam_PKP9
    WRITE_C8_D8, 0x92, 0x16,  // gam_PKP10
    
    // Gamma correction (negative)
    WRITE_C8_D8, 0xA0, 0x00,  // gam_VRN0
    WRITE_C8_D8, 0xA1, 0x05,  // gam_VRN1
    WRITE_C8_D8, 0xA2, 0x04,  // gam_VRN2
    WRITE_C8_D8, 0xA6, 0x2a,  // gam_PRN0
    WRITE_C8_D8, 0xA7, 0x44,  // gam_PRN1
    WRITE_C8_D8, 0xA3, 0x39,  // gam_VRN3
    WRITE_C8_D8, 0xA4, 0x3a,  // gam_VRN4
    WRITE_C8_D8, 0xA5, 0x3f,  // gam_VRN5
    WRITE_C8_D8, 0xA8, 0x08,  // gam_PKN0
    WRITE_C8_D8, 0xA9, 0x0f,  // gam_PKN1
    WRITE_C8_D8, 0xAA, 0x17,  // gam_PKN2
    WRITE_C8_D8, 0xAB, 0x10,  // gam_PKN3
    WRITE_C8_D8, 0xAC, 0x16,  // gam_PKN4
    WRITE_C8_D8, 0xAD, 0x14,  // gam_PKN5
    WRITE_C8_D8, 0xAE, 0x11,  // gam_PKN6
    WRITE_C8_D8, 0xAF, 0x14,  // gam_PKN7
    WRITE_C8_D8, 0xB0, 0x06,  // gam_PKN8
    WRITE_C8_D8, 0xB1, 0x0f,  // gam_PKN9
    WRITE_C8_D8, 0xB2, 0x16,  // gam_PKN10
    
    WRITE_C8_D8, 0xff, 0x00,  // Lock sequence
    WRITE_C8_D8, 0x11, 0x00,  // SLPOUT (Sleep Out)
    END_WRITE,
    
    DELAY, 120,  // Wait 120ms after SLPOUT
    
    BEGIN_WRITE,
    WRITE_C8_D8, 0x29, 0x00,  // DISPON (Display On)
    END_WRITE,
    
    DELAY, 100   // Wait 100ms after DISPON
};

// Transaction structure for QSPI protocol
static spi_transaction_ext_t spi_tran_ext;
static spi_transaction_t *spi_tran;

// Buffer for pixel data (DMA-aligned)
static union {
    uint8_t *buffer;
    uint16_t *buffer16;
    uint32_t *buffer32;
} pixel_buffer;

/**
 * @brief Send command to display using QSPI protocol
 * Protocol: cmd=0x02, addr=(command<<8) with MULTILINE_CMD|MULTILINE_ADDR
 */
static void display_send_cmd(uint8_t cmd) {
    CS_LOW();
    spi_tran_ext.base.flags = SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR;
    spi_tran_ext.base.cmd = 0x02;
    spi_tran_ext.base.addr = ((uint32_t)cmd) << 8;
    spi_tran_ext.base.tx_buffer = NULL;
    spi_tran_ext.base.length = 0;
    spi_device_polling_start(spi_handle, spi_tran, portMAX_DELAY);
    spi_device_polling_end(spi_handle, portMAX_DELAY);
    CS_HIGH();
}

/**
 * @brief Send command with 8-bit data using QSPI protocol
 * Equivalent to Arduino's writeC8D8 - used for initialization sequence
 */
static void display_send_cmd8_data8(uint8_t cmd, uint8_t data) {
    CS_LOW();
    spi_tran_ext.base.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR;
    spi_tran_ext.base.cmd = 0x02;
    spi_tran_ext.base.addr = ((uint32_t)cmd) << 8;
    spi_tran_ext.base.tx_data[0] = data;
    spi_tran_ext.base.length = 8;
    spi_device_polling_start(spi_handle, spi_tran, portMAX_DELAY);
    spi_device_polling_end(spi_handle, portMAX_DELAY);
    CS_HIGH();
}

/**
 * @brief Send command with 16-bit data using QSPI protocol
 */
static void display_send_cmd16(uint8_t cmd, uint16_t data) {
    CS_LOW();
    spi_tran_ext.base.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR;
    spi_tran_ext.base.cmd = 0x02;
    spi_tran_ext.base.addr = ((uint32_t)cmd) << 8;
    spi_tran_ext.base.tx_data[0] = data >> 8;
    spi_tran_ext.base.tx_data[1] = data & 0xFF;
    spi_tran_ext.base.length = 16;
    spi_device_polling_start(spi_handle, spi_tran, portMAX_DELAY);
    spi_device_polling_end(spi_handle, portMAX_DELAY);
    CS_HIGH();
}

/**
 * @brief Send command with two 16-bit data values using QSPI protocol
 * Equivalent to Arduino's writeC8D16D16 - sends both values in single transaction
 * Used for CASET (x0, x1) and RASET (y0, y1)
 */
static void display_send_cmd16x2(uint8_t cmd, uint16_t d1, uint16_t d2) {
    CS_LOW();
    spi_tran_ext.base.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR;
    spi_tran_ext.base.cmd = 0x02;
    spi_tran_ext.base.addr = ((uint32_t)cmd) << 8;
    spi_tran_ext.base.tx_data[0] = d1 >> 8;
    spi_tran_ext.base.tx_data[1] = d1 & 0xFF;
    spi_tran_ext.base.tx_data[2] = d2 >> 8;
    spi_tran_ext.base.tx_data[3] = d2 & 0xFF;
    spi_tran_ext.base.length = 32;
    spi_device_polling_start(spi_handle, spi_tran, portMAX_DELAY);
    spi_device_polling_end(spi_handle, portMAX_DELAY);
    CS_HIGH();
}

/**
 * @brief Send 16-bit data using QSPI protocol
 * Protocol: cmd=0x32, addr=0x003C00 with MODE_QIO
 */
void display_send_data16(uint16_t data) {
    CS_LOW();
    spi_tran_ext.base.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_MODE_QIO;
    spi_tran_ext.base.cmd = 0x32;
    spi_tran_ext.base.addr = 0x003C00;
    spi_tran_ext.base.tx_data[0] = data >> 8;
    spi_tran_ext.base.tx_data[1] = data & 0xFF;
    spi_tran_ext.base.length = 16;
    spi_device_polling_start(spi_handle, spi_tran, portMAX_DELAY);
    spi_device_polling_end(spi_handle, portMAX_DELAY);
    CS_HIGH();
}

/**
 * @brief Send batch of 16-bit data (optimized for LVGL flush)
 * Uses QSPI protocol with MODE_QIO for efficient pixel writes
 * 
 * Note: Display expects MSB-first (big-endian) bytes for each 16-bit pixel.
 * ESP32 is little-endian, so we need to byte-swap each pixel.
 */
// Debug logging control
static bool spi_logging_enabled = false;
static int spi_log_count = 0;
#define SPI_LOG_MAX 10  // Log first N transactions

void display_send_data_batch(const uint16_t *data, size_t count) {
    if (count == 0 || !data) return;
    
    CS_LOW();
    size_t remaining = count;
    const uint16_t *ptr = data;
    bool first_send = true;
    
    while (remaining > 0) {
        size_t chunk_pixels = (remaining > MAX_PIXELS_AT_ONCE) ? MAX_PIXELS_AT_ONCE : remaining;
        
        if (first_send) {
            spi_tran_ext.base.flags = SPI_TRANS_MODE_QIO;
            spi_tran_ext.base.cmd = 0x32;
            spi_tran_ext.base.addr = 0x003C00;
            first_send = false;
        } else {
            spi_tran_ext.base.flags = SPI_TRANS_MODE_QIO | SPI_TRANS_VARIABLE_CMD |
                                     SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_VARIABLE_DUMMY;
        }
        
        // Pack pixels into 32-bit words for QSPI protocol
        // CRITICAL: With LV_COLOR_16_SWAP=1, LVGL gives us swapped pixels.
        // Arduino Canvas uses draw16bitBeRGBBitmap which un-swaps pixels (MSB_16_SET) before storing in framebuffer.
        // Then writePixels receives non-swapped pixels and uses MSB_32_16_16_SET to swap for QSPI.
        // However, MSB_32_16_16_SET expects non-swapped pixels and swaps them for QSPI.
        // So we need to un-swap first (like draw16bitBeRGBBitmap), then use MSB_32_16_16_SET.
        // BUT: We also need to reverse the byte order of the 32-bit word for QSPI (MSB-first).
        size_t l2 = chunk_pixels >> 1;
        for (size_t i = 0; i < l2; ++i) {
            uint16_t p1_raw = *ptr++;
            uint16_t p2_raw = *ptr++;
            
            // Un-swap bytes (like Arduino's draw16bitBeRGBBitmap does)
            uint16_t p1 = MSB_16(p1_raw);
            uint16_t p2 = MSB_16(p2_raw);
            
            // Use MSB_32_16_16_SET on non-swapped pixels (matching Arduino writePixels)
            uint32_t packed;
            MSB_32_16_16_SET(packed, p1, p2);
            
            // Reverse byte order for QSPI (MSB-first transmission)
            // ESP32 SPI sends bytes in little-endian order, but QSPI expects MSB-first
            pixel_buffer.buffer32[i] = __builtin_bswap32(packed);
            
            // Debug: Log first few pixel packs
            if (spi_logging_enabled && spi_log_count < SPI_LOG_MAX && i == 0) {
                ESP_LOGI(TAG, "SPI[%d] Pack: p1_raw=0x%04X, p2_raw=0x%04X -> p1=0x%04X, p2=0x%04X -> packed=0x%08X -> swapped=0x%08X", 
                         spi_log_count, p1_raw, p2_raw, p1, p2, packed, pixel_buffer.buffer32[i]);
            }
        }
        if (chunk_pixels & 1) {
            uint16_t p1_raw = *ptr++;
            // Un-swap bytes first (like Arduino's draw16bitBeRGBBitmap)
            uint16_t p1 = MSB_16(p1_raw);
            // Then swap for QSPI (matching Arduino writePixels)
            MSB_16_SET(pixel_buffer.buffer16[chunk_pixels - 1], p1);
        }
        
        spi_tran_ext.base.tx_buffer = pixel_buffer.buffer32;
        spi_tran_ext.base.length = chunk_pixels << 4; // 16 bits per pixel
        
        // Debug: Log SPI transaction details
        if (spi_logging_enabled && spi_log_count < SPI_LOG_MAX) {
            ESP_LOGI(TAG, "SPI[%d] TX: flags=0x%08X, cmd=0x%02X, addr=0x%06X, len=%d, pixels=%zu",
                     spi_log_count, spi_tran_ext.base.flags, spi_tran_ext.base.cmd,
                     spi_tran_ext.base.addr, spi_tran_ext.base.length, chunk_pixels);
            if (chunk_pixels > 0) {
                ESP_LOGI(TAG, "SPI[%d] First 4 bytes: %02X %02X %02X %02X",
                         spi_log_count,
                         ((uint8_t*)pixel_buffer.buffer32)[0],
                         ((uint8_t*)pixel_buffer.buffer32)[1],
                         ((uint8_t*)pixel_buffer.buffer32)[2],
                         ((uint8_t*)pixel_buffer.buffer32)[3]);
            }
            spi_log_count++;
        }
        
        spi_device_polling_start(spi_handle, spi_tran, portMAX_DELAY);
        spi_device_polling_end(spi_handle, portMAX_DELAY);
        
        remaining -= chunk_pixels;
    }
    
    CS_HIGH();
}

/**
 * @brief Set display window (column and row addresses)
 * Uses writeC8D16D16 equivalent to send both values in single transaction
 * Matching Arduino's writeC8D16D16Split implementation
 */
void display_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    // Send CASET with both x0 and x1 in single transaction
    display_send_cmd16x2(NV3041A_CASET, x0, x1);
    
    // Send RASET with both y0 and y1 in single transaction
    display_send_cmd16x2(NV3041A_RASET, y0, y1);
    
    // Start RAM write
    display_send_cmd(NV3041A_RAMWR);
}

/**
 * @brief Initialize SPI interface for display (QSPI protocol)
 */
static esp_err_t display_spi_init(void) {
    esp_err_t ret;
    
    ESP_LOGI(TAG, "Initializing QSPI bus for display...");
    ESP_LOGI(TAG, "  SPI Host: SPI2_HOST");
    ESP_LOGI(TAG, "  MOSI (D0): GPIO %d", DISPLAY_D0_PIN);
    ESP_LOGI(TAG, "  SCK: GPIO %d", DISPLAY_SCK_PIN);
    ESP_LOGI(TAG, "  CS: GPIO %d", DISPLAY_CS_PIN);
    
    // Configure CS pin manually (not controlled by SPI driver)
    gpio_config_t cs_cfg = {
        .pin_bit_mask = (1ULL << DISPLAY_CS_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&cs_cfg);
    gpio_set_level(DISPLAY_CS_PIN, 1); // CS high (inactive)
    
    // Setup CS pin mask and port registers for fast GPIO access
    if (DISPLAY_CS_PIN >= 32) {
        cs_pin_mask = (1ULL << (DISPLAY_CS_PIN - 32));
        cs_port_set = (volatile uint32_t*)GPIO_OUT1_W1TS_REG;
        cs_port_clr = (volatile uint32_t*)GPIO_OUT1_W1TC_REG;
    } else {
        cs_pin_mask = (1ULL << DISPLAY_CS_PIN);
        cs_port_set = (volatile uint32_t*)GPIO_OUT_W1TS_REG;
        cs_port_clr = (volatile uint32_t*)GPIO_OUT_W1TC_REG;
    }
    
    // Configure SPI bus for QSPI (4-bit parallel)
    // QSPI requires quadwp (D2) and quadhd (D3) pins for 4-bit mode
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = DISPLAY_D0_PIN,      // D0 (MOSI)
        .miso_io_num = -1,                  // Not used for display
        .sclk_io_num = DISPLAY_SCK_PIN,     // Clock
        .quadwp_io_num = DISPLAY_D2_PIN,    // D2 (WP - Write Protect for QSPI)
        .quadhd_io_num = DISPLAY_D3_PIN,    // D3 (HD - Hold for QSPI)
        .data4_io_num = -1,
        .data5_io_num = -1,
        .data6_io_num = -1,
        .data7_io_num = -1,
        .max_transfer_sz = (MAX_PIXELS_AT_ONCE * 16) + 8,
        .flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_GPIO_PINS,
        .intr_flags = 0
    };
    
    ESP_LOGI(TAG, "QSPI pin configuration:");
    ESP_LOGI(TAG, "  D0 (MOSI): GPIO %d", DISPLAY_D0_PIN);
    ESP_LOGI(TAG, "  D2 (WP):   GPIO %d", DISPLAY_D2_PIN);
    ESP_LOGI(TAG, "  D3 (HD):   GPIO %d", DISPLAY_D3_PIN);
    ESP_LOGI(TAG, "  SCK:       GPIO %d", DISPLAY_SCK_PIN);
    
    ESP_LOGI(TAG, "Calling spi_bus_initialize...");
    ret = spi_bus_initialize(QSPI_SPI_HOST, &bus_cfg, QSPI_DMA_CHANNEL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s (0x%x)", esp_err_to_name(ret), ret);
        return ret;
    }
    ESP_LOGI(TAG, "SPI bus initialized successfully");
    
    // Configure SPI device with QSPI protocol (command_bits=8, address_bits=24)
    spi_device_interface_config_t dev_cfg = {
        .command_bits = 8,
        .address_bits = 24,
        .dummy_bits = 0,
        .mode = QSPI_SPI_MODE,
        .duty_cycle_pos = 0,
        .cs_ena_pretrans = 0,
        .cs_ena_posttrans = 0,
        .clock_speed_hz = QSPI_FREQUENCY,
        .input_delay_ns = 0,
        .spics_io_num = -1, // Manual CS control (avoid system CS control)
        .flags = SPI_DEVICE_HALFDUPLEX,
        .queue_size = 1,
        .pre_cb = NULL,
        .post_cb = NULL
    };
    
    ESP_LOGI(TAG, "Adding SPI device...");
    ret = spi_bus_add_device(QSPI_SPI_HOST, &dev_cfg, &spi_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add SPI device: %s (0x%x)", esp_err_to_name(ret), ret);
        return ret;
    }
    ESP_LOGI(TAG, "SPI device added successfully");
    
    // Acquire bus (exclusive access)
    spi_device_acquire_bus(spi_handle, portMAX_DELAY);
    
    // Initialize transaction structure
    memset(&spi_tran_ext, 0, sizeof(spi_tran_ext));
    spi_tran = (spi_transaction_t*)&spi_tran_ext;
    
    // Allocate DMA-aligned buffer for pixel data
    pixel_buffer.buffer = (uint8_t*)heap_caps_aligned_alloc(
        16,
        MAX_PIXELS_AT_ONCE * 2,
        MALLOC_CAP_DMA | MALLOC_CAP_8BIT
    );
    if (!pixel_buffer.buffer) {
        ESP_LOGE(TAG, "Failed to allocate pixel buffer");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Pixel buffer allocated at %p", pixel_buffer.buffer);
    
    return ESP_OK;
}

/**
 * @brief Process initialization sequence array
 * Processes the nv3041a_init_operations array similar to Arduino's batchOperation
 * 
 * @param operations Pointer to the operations array
 * @param len Length of the operations array in bytes
 */
static void display_process_init_sequence(const uint8_t *operations, size_t len) {
    bool in_write_transaction = false;
    size_t cmd_count = 0;
    size_t delay_count = 0;
    
    ESP_LOGI(TAG, "Processing initialization sequence: %zu bytes", len);
    
    for (size_t i = 0; i < len; ++i) {
        uint8_t op = operations[i];
        
        switch (op) {
            case BEGIN_WRITE:
                // Start write transaction (no-op for now, each command manages CS)
                in_write_transaction = true;
                ESP_LOGD(TAG, "BEGIN_WRITE at index %zu", i);
                break;
                
            case WRITE_C8_D8:
                // Command + 8-bit data
                if (i + 2 < len) {
                    uint8_t cmd = operations[++i];
                    uint8_t data = operations[++i];
                    display_send_cmd8_data8(cmd, data);
                    cmd_count++;
                    if (cmd_count <= 5 || cmd_count % 20 == 0) {
                        ESP_LOGD(TAG, "WRITE_C8_D8[%zu]: cmd=0x%02X, data=0x%02X", cmd_count, cmd, data);
                    }
                } else {
                    ESP_LOGE(TAG, "Invalid WRITE_C8_D8 at index %zu (insufficient data, len=%zu)", i, len);
                    return;
                }
                break;
                
            case END_WRITE:
                // End write transaction (no-op for now)
                in_write_transaction = false;
                ESP_LOGD(TAG, "END_WRITE at index %zu", i);
                break;
                
            case DELAY:
                // Delay in milliseconds
                if (i + 1 < len) {
                    uint8_t delay_ms = operations[++i];
                    vTaskDelay(pdMS_TO_TICKS(delay_ms));
                    delay_count++;
                    ESP_LOGD(TAG, "DELAY[%zu]: %d ms at index %zu", delay_count, delay_ms, i);
                } else {
                    ESP_LOGE(TAG, "Invalid DELAY at index %zu (insufficient data, len=%zu)", i, len);
                    return;
                }
                break;
                
            default:
                ESP_LOGE(TAG, "Unknown operation code 0x%02X at index %zu, aborting", op, i);
                return;  // Abort on unknown operation instead of skipping
        }
    }
    
    ESP_LOGI(TAG, "Initialization sequence complete: %zu commands, %zu delays", cmd_count, delay_count);
}

/**
 * @brief Initialize NV3041A display controller
 * 
 * NOTE: Current implementation uses minimal initialization sequence.
 * Arduino implementation uses comprehensive nv3041a_init_operations array
 * with many register writes for optimal display performance.
 * Full sequence should be implemented if display issues persist.
 */
/**
 * @brief Initialize NV3041A display controller
 * Using full initialization sequence from Arduino_NV3041A.h
 */
static esp_err_t display_controller_init(void) {
    ESP_LOGI(TAG, "Starting display controller initialization with full sequence...");
    
    // Software reset
    ESP_LOGI(TAG, "Sending software reset command...");
    display_send_cmd(NV3041A_SWRESET);
    vTaskDelay(pdMS_TO_TICKS(120));
    
    // Process full initialization sequence
    ESP_LOGI(TAG, "Processing full initialization sequence (%zu bytes)...", sizeof(nv3041a_init_operations));
    display_process_init_sequence(nv3041a_init_operations, sizeof(nv3041a_init_operations));
    
    ESP_LOGI(TAG, "Display controller initialization complete");
    return ESP_OK;
}

bool display_init(void) {
    esp_err_t ret;
    
#ifdef DISPLAY_DISABLE
    ESP_LOGI(TAG, "Display initialization disabled (DISPLAY_DISABLE defined)");
    return false;
#endif
    
    ESP_LOGI(TAG, "Initializing NV3041A display");
    ESP_LOGI(TAG, "Resolution: %dx%d", DISPLAY_WIDTH, DISPLAY_HEIGHT);
    
    // Initialize SPI
    ret = display_spi_init();
    if (ret != ESP_OK) {
        return false;
    }
    
    // Initialize display controller
    ret = display_controller_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize display controller");
        return false;
    }
    
    // NOTE: Framebuffer allocation temporarily disabled
    // LVGL manages its own display buffers via flush callback
    ESP_LOGW(TAG, "Framebuffer allocation disabled - LVGL uses direct pixel writes");
    framebuffer = NULL;
    
    display_initialized = true;
    ESP_LOGI(TAG, "Display initialized successfully");
    
    return true;
}

bool display_is_initialized(void) {
    return display_initialized;
}

void display_clear(uint16_t color) {
    if (!display_initialized || !framebuffer) return;
    
    for (int i = 0; i < DISPLAY_WIDTH * DISPLAY_HEIGHT; i++) {
        framebuffer[i] = color;
    }
}

void display_draw_pixel(int x, int y, uint16_t color) {
    if (!display_initialized) return;
    if (x < 0 || x >= DISPLAY_WIDTH || y < 0 || y >= DISPLAY_HEIGHT) return;
    
    if (framebuffer) {
        framebuffer[y * DISPLAY_WIDTH + x] = color;
    } else {
        // Direct write to display via SPI (for LVGL flush callback)
        display_set_window(x, y, x, y);
        display_send_data16(color);
    }
}

void display_draw_line(int x0, int y0, int x1, int y1, uint16_t color) {
    // Bresenham's line algorithm
    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;
    
    int x = x0, y = y0;
    
    while (1) {
        display_draw_pixel(x, y, color);
        
        if (x == x1 && y == y1) break;
        
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x += sx;
        }
        if (e2 < dx) {
            err += dx;
            y += sy;
        }
    }
}

void display_draw_rect(int x, int y, int w, int h, uint16_t color) {
    display_draw_line(x, y, x + w - 1, y, color);
    display_draw_line(x + w - 1, y, x + w - 1, y + h - 1, color);
    display_draw_line(x + w - 1, y + h - 1, x, y + h - 1, color);
    display_draw_line(x, y + h - 1, x, y, color);
}

void display_fill_rect(int x, int y, int w, int h, uint16_t color) {
    if (!display_initialized || !framebuffer) return;
    
    for (int j = y; j < y + h && j < DISPLAY_HEIGHT; j++) {
        for (int i = x; i < x + w && i < DISPLAY_WIDTH; i++) {
            if (i >= 0 && j >= 0) {
                framebuffer[j * DISPLAY_WIDTH + i] = color;
            }
        }
    }
}

void display_update_track_info(const char *title, const char *artist, 
                                uint32_t position, uint32_t duration) {
    // TODO: Implement text rendering
    (void)title;
    (void)artist;
    (void)position;
    (void)duration;
}

void display_set_brightness(uint8_t brightness) {
    // Backlight is controlled via LEDC in main.c
    (void)brightness;
}

/**
 * @brief Flush framebuffer to display
 */
static void display_flush(void) {
    if (!display_initialized || !framebuffer) return;
    
    display_set_window(0, 0, DISPLAY_WIDTH - 1, DISPLAY_HEIGHT - 1);
    display_send_data_batch(framebuffer, DISPLAY_WIDTH * DISPLAY_HEIGHT);
}

void display_update(void) {
    if (!display_initialized) return;
    
    // Flush framebuffer to display
    display_flush();
}

/**
 * @brief Enable/disable SPI transaction logging
 */
void display_enable_spi_logging(bool enable) {
    spi_logging_enabled = enable;
    spi_log_count = 0;
    ESP_LOGI(TAG, "SPI logging %s", enable ? "enabled" : "disabled");
}

/**
 * @brief Fill entire screen with a single color
 */
void display_fill_screen(uint16_t color) {
    if (!display_initialized) return;
    
    display_set_window(0, 0, DISPLAY_WIDTH - 1, DISPLAY_HEIGHT - 1);
    
    // Fill screen pixel by pixel (simple but slow)
    // For better performance, could allocate buffer and fill in chunks
    for (int y = 0; y < DISPLAY_HEIGHT; y++) {
        for (int x = 0; x < DISPLAY_WIDTH; x++) {
            display_send_data16(color);
        }
    }
}

/**
 * @brief Draw test pattern: solid color
 */
void display_test_single_color(uint16_t color) {
    ESP_LOGI(TAG, "Test: Filling screen with color 0x%04X", color);
    display_fill_screen(color);
}

/**
 * @brief Draw test pattern: RGB color bars
 */
void display_test_color_bars(void) {
    if (!display_initialized) return;
    
    ESP_LOGI(TAG, "Test: Drawing RGB color bars");
    
    const uint16_t RED = 0xF800;
    const uint16_t GREEN = 0x07E0;
    const uint16_t BLUE = 0x001F;
    const uint16_t WHITE = 0xFFFF;
    const uint16_t BLACK = 0x0000;
    
    int bar_width = DISPLAY_WIDTH / 5;
    
    // Red bar
    display_set_window(0, 0, bar_width - 1, DISPLAY_HEIGHT - 1);
    for (int i = 0; i < bar_width * DISPLAY_HEIGHT; i++) {
        display_send_data16(RED);
    }
    
    // Green bar
    display_set_window(bar_width, 0, bar_width * 2 - 1, DISPLAY_HEIGHT - 1);
    for (int i = 0; i < bar_width * DISPLAY_HEIGHT; i++) {
        display_send_data16(GREEN);
    }
    
    // Blue bar
    display_set_window(bar_width * 2, 0, bar_width * 3 - 1, DISPLAY_HEIGHT - 1);
    for (int i = 0; i < bar_width * DISPLAY_HEIGHT; i++) {
        display_send_data16(BLUE);
    }
    
    // White bar
    display_set_window(bar_width * 3, 0, bar_width * 4 - 1, DISPLAY_HEIGHT - 1);
    for (int i = 0; i < bar_width * DISPLAY_HEIGHT; i++) {
        display_send_data16(WHITE);
    }
    
    // Black bar
    display_set_window(bar_width * 4, 0, DISPLAY_WIDTH - 1, DISPLAY_HEIGHT - 1);
    for (int i = 0; i < (DISPLAY_WIDTH - bar_width * 4) * DISPLAY_HEIGHT; i++) {
        display_send_data16(BLACK);
    }
}

/**
 * @brief Draw test pattern: checkerboard
 */
void display_test_checkerboard(void) {
    if (!display_initialized) return;
    
    ESP_LOGI(TAG, "Test: Drawing checkerboard pattern");
    
    const uint16_t WHITE = 0xFFFF;
    const uint16_t BLACK = 0x0000;
    const int square_size = 20;
    
    for (int y = 0; y < DISPLAY_HEIGHT; y += square_size) {
        for (int x = 0; x < DISPLAY_WIDTH; x += square_size) {
            bool is_white = ((x / square_size) + (y / square_size)) % 2 == 0;
            uint16_t color = is_white ? WHITE : BLACK;
            
            int x_end = (x + square_size < DISPLAY_WIDTH) ? x + square_size - 1 : DISPLAY_WIDTH - 1;
            int y_end = (y + square_size < DISPLAY_HEIGHT) ? y + square_size - 1 : DISPLAY_HEIGHT - 1;
            
            display_set_window(x, y, x_end, y_end);
            for (int i = 0; i < (x_end - x + 1) * (y_end - y + 1); i++) {
                display_send_data16(color);
            }
        }
    }
}

/**
 * @brief Draw test pattern: gradient
 */
void display_test_gradient(void) {
    if (!display_initialized) return;
    
    ESP_LOGI(TAG, "Test: Drawing gradient pattern");
    
    for (int x = 0; x < DISPLAY_WIDTH; x++) {
        // Calculate color intensity (0-31 for each RGB component)
        uint8_t intensity = (x * 31) / DISPLAY_WIDTH;
        uint16_t color = (intensity << 11) | (intensity << 6) | intensity; // RGB565
        
        display_set_window(x, 0, x, DISPLAY_HEIGHT - 1);
        for (int y = 0; y < DISPLAY_HEIGHT; y++) {
            display_send_data16(color);
        }
    }
}

/**
 * @brief Byte-order test: send known pixel values and log packed result
 */
void display_test_byte_order(void) {
    if (!display_initialized) return;
    
    ESP_LOGI(TAG, "Test: Byte-order validation");
    
    // Test pixels
    uint16_t p1 = 0x1234;
    uint16_t p2 = 0x5678;
    
    ESP_LOGI(TAG, "Input pixels: p1=0x%04X, p2=0x%04X", p1, p2);
    
    // Pack using MSB_32_16_16_SET macro
    uint32_t packed;
    MSB_32_16_16_SET(packed, p1, p2);
    
    ESP_LOGI(TAG, "Packed 32-bit word: 0x%08X", packed);
    ESP_LOGI(TAG, "Byte order: [%02X][%02X][%02X][%02X]",
             (packed >> 24) & 0xFF,
             (packed >> 16) & 0xFF,
             (packed >> 8) & 0xFF,
             packed & 0xFF);
    
    // Expected: [p1_MSB][p1_LSB][p2_MSB][p2_LSB] = [12][34][56][78]
    // But MSB_32_16_16_SET swaps bytes, so:
    // p1: 0x1234 -> MSB_16(0x1234) = 0x3412
    // p2: 0x5678 -> MSB_16(0x5678) = 0x7856
    // Packed: [p2_MSB][p2_LSB][p1_MSB][p1_LSB] = [78][56][34][12]
    ESP_LOGI(TAG, "Expected (after MSB swap): [78][56][34][12]");
}
