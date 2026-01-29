/**
 * @file id3_parser.h
 * @brief ID3v2 tag parser for MP3 files
 */

#ifndef ID3_PARSER_H
#define ID3_PARSER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief ID3 tag information
 */
typedef struct {
    char title[128];
    char artist[128];
    char album[128];
    char genre[64];
    uint16_t year;
    uint8_t track;
    uint32_t tag_size; // Size of the ID3v2 tag in bytes (including header)
    bool has_tag;
} id3_tag_t;

/**
 * @brief Parse ID3v2 tag from file
 * 
 * @param filepath Path to MP3 file
 * @param tag Pointer to id3_tag_t structure to fill
 * @return true if tag was found and parsed, false otherwise
 */
bool id3_parse_file(const char *filepath, id3_tag_t *tag);

/**
 * @brief Parse ID3v2 tag from buffer
 * 
 * @param buffer Buffer containing file data
 * @param size Buffer size
 * @param tag Pointer to id3_tag_t structure to fill
 * @return true if tag was found and parsed, false otherwise
 */
bool id3_parse_buffer(const uint8_t *buffer, size_t size, id3_tag_t *tag);

#ifdef __cplusplus
}
#endif

#endif /* ID3_PARSER_H */

