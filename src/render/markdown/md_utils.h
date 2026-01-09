/**
 * @file md_utils.h
 * @brief Utility functions for Markdown parsing and rendering
 */

#ifndef MD_UTILS_H
#define MD_UTILS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Get terminal width
 * @return Terminal width in columns, defaults to 80 if unable to detect
 */
int md_get_terminal_width(void);

/**
 * Check if terminal supports OSC 8 hyperlinks
 * @return 1 if supported, 0 otherwise
 */
int md_supports_hyperlink(void);

/**
 * Decode a UTF-8 character and return its Unicode code point
 * @param str Pointer to UTF-8 string
 * @param bytes_read Output: number of bytes consumed
 * @return Unicode code point, or 0xFFFD on error
 */
uint32_t md_utf8_decode(const char* str, int* bytes_read);

/**
 * Check if a Unicode code point is a wide character (e.g., CJK)
 * @param codepoint Unicode code point
 * @return 2 if wide, 1 if normal, 0 if zero-width
 */
int md_char_width(uint32_t codepoint);

/**
 * Calculate display width of a UTF-8 string
 * @param str UTF-8 string
 * @return Display width in terminal columns
 */
int md_utf8_display_width(const char* str);

/**
 * Duplicate a string
 * @param str Source string
 * @return Newly allocated copy, or NULL on failure
 */
char* md_strdup(const char* str);

/**
 * Duplicate a string with length limit
 * @param str Source string
 * @param n Maximum length
 * @return Newly allocated copy, or NULL on failure
 */
char* md_strndup(const char* str, size_t n);

/**
 * Trim leading whitespace (returns pointer into original string)
 * @param str Source string
 * @return Pointer to first non-whitespace character
 */
const char* md_ltrim(const char* str);

/**
 * Trim trailing whitespace (modifies string in place)
 * @param str String to modify
 * @return Same pointer as input
 */
char* md_rtrim(char* str);

/**
 * Count leading spaces in a string
 * @param str Source string
 * @return Number of leading spaces (tabs count as 4 spaces)
 */
int md_count_indent(const char* str);

/**
 * Append string to a dynamic buffer
 * @param buf Pointer to buffer pointer
 * @param buf_size Pointer to buffer size
 * @param buf_len Pointer to current content length
 * @param str String to append
 * @return 0 on success, -1 on failure
 */
int md_buffer_append(char** buf, size_t* buf_size, size_t* buf_len, const char* str);

/**
 * Append character to a dynamic buffer
 * @param buf Pointer to buffer pointer
 * @param buf_size Pointer to buffer size
 * @param buf_len Pointer to current content length
 * @param c Character to append
 * @return 0 on success, -1 on failure
 */
int md_buffer_append_char(char** buf, size_t* buf_size, size_t* buf_len, char c);

#ifdef __cplusplus
}
#endif

#endif /* MD_UTILS_H */
