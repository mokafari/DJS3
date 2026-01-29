/**
 * @file id3_parser.c
 * @brief ID3v2 tag parser implementation
 * 
 * Supports ID3v2.3 and ID3v2.4 tags
 * Parses common frames: TIT2, TPE1, TALB, TCON, TYER, TRCK
 */

#include "id3_parser.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "id3_parser";

// ID3v2 header structure
typedef struct {
    char identifier[3];      // "ID3"
    uint8_t version_major;
    uint8_t version_minor;
    uint8_t flags;
    uint32_t size;           // Synchsafe integer
} id3v2_header_t;

// ID3v2 frame header
typedef struct {
    char frame_id[4];
    uint32_t size;           // Synchsafe integer (v2.4) or regular (v2.3)
    uint16_t flags;
} id3v2_frame_header_t;

/**
 * @brief Read synchsafe integer (7 bits per byte)
 */
static uint32_t read_synchsafe(const uint8_t *data) {
    return (data[0] << 21) | (data[1] << 14) | (data[2] << 7) | data[3];
}

/**
 * @brief Read regular 32-bit integer
 */
static uint32_t read_uint32(const uint8_t *data) {
    return (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
}

/**
 * @brief Parse ID3v2 header
 */
static bool parse_id3v2_header(const uint8_t *buffer, size_t size, id3v2_header_t *header) {
    if (size < 10) return false;
    
    if (memcmp(buffer, "ID3", 3) != 0) {
        return false;
    }
    
    memcpy(header->identifier, buffer, 3);
    header->version_major = buffer[3];
    header->version_minor = buffer[4];
    header->flags = buffer[5];
    header->size = read_synchsafe(&buffer[6]);
    
    return true;
}

/**
 * @brief Parse text frame (TIT2, TPE1, TALB, TCON)
 */
static bool parse_text_frame(const uint8_t *data, size_t size, char *output, size_t output_size) {
    if (size < 2) return false;
    
    uint8_t encoding = data[0];
    size_t text_start = 1;
    size_t text_size = size - 1;
    
    // Convert encoding
    if (encoding == 0 || encoding == 3) {
        // ISO-8859-1 or UTF-8
        size_t copy_size = (text_size < output_size - 1) ? text_size : output_size - 1;
        size_t out_idx = 0;
        for (size_t i = 0; i < copy_size; i++) {
            uint8_t c = data[text_start + i];
            if (c >= 32 && c < 127) { // Only printable ASCII
                output[out_idx++] = c;
            } else if (out_idx > 0 && output[out_idx-1] != ' ') {
                output[out_idx++] = ' '; // Replace non-printable with space
            }
        }
        output[out_idx] = '\0';
        return true;
    } else if (encoding == 1 || encoding == 2) {
        // UTF-16 with BOM (1) or without BOM (2)
        // Skip BOM if present
        if (text_size >= 2) {
            if ((data[1] == 0xFF && data[2] == 0xFE) || (data[1] == 0xFE && data[2] == 0xFF)) {
                text_start = 3;
                text_size -= 2;
            }
        }
        
        // Simplified UTF-16 to ASCII conversion
        size_t out_idx = 0;
        for (size_t i = 0; i < text_size && out_idx < output_size - 1; i += 2) {
            // In UTF-16, characters are 2 bytes. For ASCII range, one byte is the char, other is 0.
            // We check both bytes and take the non-zero one if it's printable.
            uint8_t c1 = data[text_start + i];
            uint8_t c2 = (text_start + i + 1 < size) ? data[text_start + i + 1] : 0;
            uint8_t c = (c1 != 0) ? c1 : c2;
            
            if (c >= 32 && c < 127) {
                output[out_idx++] = c;
            }
        }
        output[out_idx] = '\0';
        
        // Trim trailing spaces
        while (out_idx > 0 && output[out_idx-1] == ' ') {
            output[--out_idx] = '\0';
        }
        
        return true;
    }
    
    return false;
}

/**
 * @brief Parse numeric frame (TYER, TRCK)
 */
static uint16_t parse_numeric_frame(const uint8_t *data, size_t size) {
    if (size < 2) return 0;
    
    // Skip encoding byte
    if (size > 1) {
        char num_str[16] = {0};
        size_t num_len = (size - 1 < sizeof(num_str) - 1) ? size - 1 : sizeof(num_str) - 1;
        memcpy(num_str, &data[1], num_len);
        num_str[num_len] = '\0';
        
        // Extract first number (may be "2003" or "1/12")
        char *slash = strchr(num_str, '/');
        if (slash) *slash = '\0';
        
        return (uint16_t)atoi(num_str);
    }
    
    return 0;
}

/**
 * @brief Parse ID3v2 frames
 */
static bool parse_id3v2_frames(const uint8_t *buffer, size_t size, id3_tag_t *tag, uint8_t version) {
    size_t offset = 0; // Buffer starts at frame data (after header)
    size_t remaining = size;
    
    memset(tag, 0, sizeof(id3_tag_t));
    
    while (remaining >= 10) {
        id3v2_frame_header_t frame;
        memcpy(frame.frame_id, &buffer[offset], 4);
        
        if (frame.frame_id[0] == 0) {
            // Padding, end of frames
            break;
        }
        
        // Read frame size
        // ID3v2.4 uses synchsafe (7-bit) integers for frame size
        // ID3v2.3 uses regular 32-bit integers
        if (version >= 4) {
            frame.size = read_synchsafe(&buffer[offset + 4]);
        } else {
            frame.size = read_uint32(&buffer[offset + 4]);
        }
        
        frame.flags = (buffer[offset + 8] << 8) | buffer[offset + 9];
        
        char id_str[5] = {0};
        memcpy(id_str, frame.frame_id, 4);
        ESP_LOGD(TAG, "Found frame: %s, size: %lu", id_str, frame.size);

        if (frame.size == 0 || frame.size > remaining - 10) {
            ESP_LOGW(TAG, "Invalid frame size for %s: %lu (remaining: %zu)", id_str, frame.size, remaining);
            break;
        }
        
        const uint8_t *frame_data = &buffer[offset + 10];
        size_t frame_data_size = frame.size;
        
        // Parse known frames
        if (memcmp(frame.frame_id, "TIT2", 4) == 0) {
            parse_text_frame(frame_data, frame_data_size, tag->title, sizeof(tag->title));
            ESP_LOGI(TAG, "Found title: %s", tag->title);
        } else if (memcmp(frame.frame_id, "TPE1", 4) == 0) {
            parse_text_frame(frame_data, frame_data_size, tag->artist, sizeof(tag->artist));
        } else if (memcmp(frame.frame_id, "TALB", 4) == 0) {
            parse_text_frame(frame_data, frame_data_size, tag->album, sizeof(tag->album));
        } else if (memcmp(frame.frame_id, "TCON", 4) == 0) {
            parse_text_frame(frame_data, frame_data_size, tag->genre, sizeof(tag->genre));
        } else if (memcmp(frame.frame_id, "TYER", 4) == 0 || memcmp(frame.frame_id, "TDRC", 4) == 0) {
            tag->year = parse_numeric_frame(frame_data, frame_data_size);
        } else if (memcmp(frame.frame_id, "TRCK", 4) == 0) {
            tag->track = parse_numeric_frame(frame_data, frame_data_size);
        }
        
        offset += 10 + frame.size;
        remaining = size - offset;
    }
    
    tag->has_tag = true;
    return true;
}

bool id3_parse_file(const char *filepath, id3_tag_t *tag) {
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        return false;
    }
    
    // Read ID3v2 header
    uint8_t header_buf[10];
    if (fread(header_buf, 1, 10, f) != 10) {
        fclose(f);
        return false;
    }
    
    id3v2_header_t header;
    if (!parse_id3v2_header(header_buf, 10, &header)) {
        fclose(f);
        return false;
    }
    
    // Read tag data
    uint32_t tag_size = header.size;
    if (tag_size > 16384) { // Sanity check: max 16KB tag for metadata view
        tag_size = 16384;
    }
    
    uint8_t *tag_data = (uint8_t*)malloc(tag_size);
    if (!tag_data) {
        fclose(f);
        return false;
    }
    
    if (fread(tag_data, 1, tag_size, f) != tag_size) {
        free(tag_data);
        fclose(f);
        return false;
    }
    
    fclose(f);
    
    // Parse frames
    bool result = parse_id3v2_frames(tag_data, tag_size, tag, header.version_major);
    if (result) {
        ESP_LOGI(TAG, "Parsed title: %s", tag->title);
    }
    
    free(tag_data);
    return result;
}

bool id3_parse_buffer(const uint8_t *buffer, size_t size, id3_tag_t *tag) {
    if (size < 10) return false;
    
    id3v2_header_t header;
    if (!parse_id3v2_header(buffer, size, &header)) {
        return false;
    }
    
    uint32_t tag_size = header.size;
    if (tag_size + 10 > size) {
        tag_size = size - 10;
    }
    
    return parse_id3v2_frames(buffer, 10 + tag_size, tag, header.version_major);
}

