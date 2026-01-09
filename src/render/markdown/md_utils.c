/**
 * @file md_utils.c
 * @brief Utility functions implementation
 */

#include "md_utils.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/ioctl.h>
#endif

/* ========== Terminal utilities ========== */

int md_get_terminal_width(void) {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        return csbi.srWindow.Right - csbi.srWindow.Left + 1;
    }
    return 80;
#else
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0) {
        return w.ws_col;
    }
    return 80;
#endif
}

int md_supports_hyperlink(void) {
    const char* term_program = getenv("TERM_PROGRAM");
    const char* term = getenv("TERM");
    const char* vte_version = getenv("VTE_VERSION");
    const char* wt_session = getenv("WT_SESSION");
    
    if (term_program) {
        if (strcmp(term_program, "iTerm.app") == 0 ||
            strcmp(term_program, "WezTerm") == 0 ||
            strcmp(term_program, "Hyper") == 0) {
            return 1;
        }
        if (strcmp(term_program, "WarpTerminal") == 0 ||
            strcmp(term_program, "Apple_Terminal") == 0) {
            return 0;
        }
    }
    
    if (term) {
        if (strstr(term, "xterm") || strstr(term, "screen")) {
            return 1;
        }
    }
    
    if (vte_version || wt_session) {
        return 1;
    }
    
    return 0;
}

/* ========== UTF-8 utilities ========== */

uint32_t md_utf8_decode(const char* str, int* bytes_read) {
    const unsigned char* s = (const unsigned char*)str;
    uint32_t codepoint;
    int len;
    
    if (s[0] == 0) {
        *bytes_read = 0;
        return 0;
    }
    
    if ((s[0] & 0x80) == 0) {
        /* ASCII */
        *bytes_read = 1;
        return s[0];
    } else if ((s[0] & 0xE0) == 0xC0) {
        /* 2-byte sequence */
        len = 2;
        codepoint = s[0] & 0x1F;
    } else if ((s[0] & 0xF0) == 0xE0) {
        /* 3-byte sequence */
        len = 3;
        codepoint = s[0] & 0x0F;
    } else if ((s[0] & 0xF8) == 0xF0) {
        /* 4-byte sequence */
        len = 4;
        codepoint = s[0] & 0x07;
    } else {
        /* Invalid UTF-8 */
        *bytes_read = 1;
        return 0xFFFD;
    }
    
    for (int i = 1; i < len; i++) {
        if ((s[i] & 0xC0) != 0x80) {
            /* Invalid continuation byte */
            *bytes_read = i;
            return 0xFFFD;
        }
        codepoint = (codepoint << 6) | (s[i] & 0x3F);
    }
    
    *bytes_read = len;
    return codepoint;
}

int md_char_width(uint32_t codepoint) {
    /* Zero-width characters */
    if (codepoint == 0 ||
        (codepoint >= 0x0300 && codepoint <= 0x036F) ||  /* Combining diacritical */
        (codepoint >= 0x200B && codepoint <= 0x200F) ||  /* Zero-width space, etc. */
        (codepoint >= 0x2060 && codepoint <= 0x206F) ||  /* Word joiner, etc. */
        (codepoint >= 0xFE00 && codepoint <= 0xFE0F) ||  /* Variation selectors */
        (codepoint >= 0xFE20 && codepoint <= 0xFE2F)) {  /* Combining half marks */
        return 0;
    }
    
    /* Wide characters (CJK, fullwidth forms, etc.) */
    if ((codepoint >= 0x1100 && codepoint <= 0x115F) ||   /* Hangul Jamo */
        (codepoint >= 0x2E80 && codepoint <= 0x9FFF) ||   /* CJK */
        (codepoint >= 0xAC00 && codepoint <= 0xD7A3) ||   /* Hangul syllables */
        (codepoint >= 0xF900 && codepoint <= 0xFAFF) ||   /* CJK Compatibility */
        (codepoint >= 0xFE10 && codepoint <= 0xFE1F) ||   /* Vertical forms */
        (codepoint >= 0xFE30 && codepoint <= 0xFE6F) ||   /* CJK Compatibility Forms */
        (codepoint >= 0xFF00 && codepoint <= 0xFF60) ||   /* Fullwidth forms */
        (codepoint >= 0xFFE0 && codepoint <= 0xFFE6) ||   /* Fullwidth signs */
        (codepoint >= 0x20000 && codepoint <= 0x2FFFD) || /* CJK Extension B-F */
        (codepoint >= 0x30000 && codepoint <= 0x3FFFD)) { /* CJK Extension G+ */
        return 2;
    }
    
    /* Emoji (most are wide) */
    if ((codepoint >= 0x1F300 && codepoint <= 0x1F9FF) || /* Misc Symbols, Emoticons */
        (codepoint >= 0x1FA00 && codepoint <= 0x1FAFF)) { /* Chess, Extended-A */
        return 2;
    }
    
    return 1;
}

int md_utf8_display_width(const char* str) {
    if (!str) return 0;
    
    int width = 0;
    int bytes;
    
    while (*str) {
        uint32_t cp = md_utf8_decode(str, &bytes);
        if (bytes == 0) break;
        width += md_char_width(cp);
        str += bytes;
    }
    
    return width;
}

/* ========== String utilities ========== */

char* md_strdup(const char* str) {
    if (!str) return NULL;
    size_t len = strlen(str);
    char* copy = (char*)malloc(len + 1);
    if (copy) {
        memcpy(copy, str, len + 1);
    }
    return copy;
}

char* md_strndup(const char* str, size_t n) {
    if (!str) return NULL;
    size_t len = strlen(str);
    if (len > n) len = n;
    char* copy = (char*)malloc(len + 1);
    if (copy) {
        memcpy(copy, str, len);
        copy[len] = '\0';
    }
    return copy;
}

const char* md_ltrim(const char* str) {
    if (!str) return NULL;
    while (*str && isspace((unsigned char)*str)) {
        str++;
    }
    return str;
}

char* md_rtrim(char* str) {
    if (!str) return NULL;
    char* end = str + strlen(str) - 1;
    while (end >= str && isspace((unsigned char)*end)) {
        *end-- = '\0';
    }
    return str;
}

int md_count_indent(const char* str) {
    if (!str) return 0;
    int indent = 0;
    while (*str) {
        if (*str == ' ') {
            indent++;
        } else if (*str == '\t') {
            indent += 4;
        } else {
            break;
        }
        str++;
    }
    return indent;
}

/* ========== Buffer utilities ========== */

int md_buffer_append(char** buf, size_t* buf_size, size_t* buf_len, const char* str) {
    if (!str) return 0;
    size_t str_len = strlen(str);
    
    /* Ensure capacity */
    size_t needed = *buf_len + str_len + 1;
    if (needed > *buf_size) {
        size_t new_size = *buf_size * 2;
        if (new_size < needed) new_size = needed;
        if (new_size < 256) new_size = 256;
        
        char* new_buf = (char*)realloc(*buf, new_size);
        if (!new_buf) return -1;
        
        *buf = new_buf;
        *buf_size = new_size;
    }
    
    memcpy(*buf + *buf_len, str, str_len + 1);
    *buf_len += str_len;
    return 0;
}

int md_buffer_append_char(char** buf, size_t* buf_size, size_t* buf_len, char c) {
    char str[2] = {c, '\0'};
    return md_buffer_append(buf, buf_size, buf_len, str);
}
