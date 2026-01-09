/**
 * @file md_style.h
 * @brief ANSI terminal style codes for Markdown rendering
 */

#ifndef MD_STYLE_H
#define MD_STYLE_H

#ifdef __cplusplus
extern "C" {
#endif

/* ========== Reset ========== */
#define MD_STYLE_RESET       "\033[0m"

/* ========== Text styles ========== */
#define MD_STYLE_BOLD        "\033[1m"
#define MD_STYLE_DIM         "\033[2m"
#define MD_STYLE_ITALIC      "\033[3m"
#define MD_STYLE_UNDERLINE   "\033[4m"
#define MD_STYLE_INVERSE     "\033[7m"
#define MD_STYLE_HIDDEN      "\033[8m"
#define MD_STYLE_STRIKE      "\033[9m"

/* ========== Foreground colors ========== */
#define MD_COLOR_BLACK       "\033[30m"
#define MD_COLOR_RED         "\033[31m"
#define MD_COLOR_GREEN       "\033[32m"
#define MD_COLOR_YELLOW      "\033[33m"
#define MD_COLOR_BLUE        "\033[34m"
#define MD_COLOR_MAGENTA     "\033[35m"
#define MD_COLOR_CYAN        "\033[36m"
#define MD_COLOR_WHITE       "\033[37m"

/* ========== Bright foreground colors ========== */
#define MD_COLOR_BRIGHT_BLACK   "\033[90m"
#define MD_COLOR_BRIGHT_RED     "\033[91m"
#define MD_COLOR_BRIGHT_GREEN   "\033[92m"
#define MD_COLOR_BRIGHT_YELLOW  "\033[93m"
#define MD_COLOR_BRIGHT_BLUE    "\033[94m"
#define MD_COLOR_BRIGHT_MAGENTA "\033[95m"
#define MD_COLOR_BRIGHT_CYAN    "\033[96m"
#define MD_COLOR_BRIGHT_WHITE   "\033[97m"

/* ========== Background colors ========== */
#define MD_BG_BLACK          "\033[40m"
#define MD_BG_RED            "\033[41m"
#define MD_BG_GREEN          "\033[42m"
#define MD_BG_YELLOW         "\033[43m"
#define MD_BG_BLUE           "\033[44m"
#define MD_BG_MAGENTA        "\033[45m"
#define MD_BG_CYAN           "\033[46m"
#define MD_BG_WHITE          "\033[47m"

/* ========== Bright background colors ========== */
#define MD_BG_BRIGHT_BLACK   "\033[100m"
#define MD_BG_BRIGHT_WHITE   "\033[107m"

/* ========== Light gray aliases ========== */
#define MD_COLOR_DARK_GRAY   MD_COLOR_BRIGHT_BLACK
#define MD_COLOR_LIGHT_GRAY  MD_COLOR_WHITE
#define MD_BG_DARK_GRAY      MD_BG_BRIGHT_BLACK
#define MD_BG_LIGHT_GRAY     MD_BG_WHITE

/* ========== Heading colors by level ========== */
#define MD_HEADING1_COLOR    MD_COLOR_MAGENTA
#define MD_HEADING2_COLOR    MD_COLOR_GREEN
#define MD_HEADING3_COLOR    MD_COLOR_BLUE
#define MD_HEADING4_COLOR    MD_COLOR_YELLOW
#define MD_HEADING5_COLOR    MD_COLOR_BRIGHT_GREEN
#define MD_HEADING6_COLOR    MD_COLOR_CYAN

/* ========== OSC 8 hyperlink sequences ========== */
#define MD_HYPERLINK_START   "\033]8;;"
#define MD_HYPERLINK_SEP     "\033\\"
#define MD_HYPERLINK_END     "\033]8;;\033\\"

/* ========== Box drawing characters (UTF-8) ========== */
#define MD_BOX_TOP_LEFT      "┌"
#define MD_BOX_TOP_RIGHT     "┐"
#define MD_BOX_BOTTOM_LEFT   "└"
#define MD_BOX_BOTTOM_RIGHT  "┘"
#define MD_BOX_HORIZONTAL    "─"
#define MD_BOX_VERTICAL      "│"
#define MD_BOX_CROSS         "┼"
#define MD_BOX_T_DOWN        "┬"
#define MD_BOX_T_UP          "┴"
#define MD_BOX_T_RIGHT       "├"
#define MD_BOX_T_LEFT        "┤"

/* ========== List bullets ========== */
#define MD_BULLET_LEVEL0     "•"
#define MD_BULLET_LEVEL1     "◦"
#define MD_BULLET_LEVEL2     "▪"

#ifdef __cplusplus
}
#endif

#endif /* MD_STYLE_H */
