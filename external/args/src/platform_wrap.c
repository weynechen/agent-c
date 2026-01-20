/**
 * @file platform_wrap.c
 * @brief Platform-specific wrapper layer implementation
 */

#include "platform_wrap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Platform detection - internal use only */
#ifdef _WIN32
    #include <windows.h>
    #define PLATFORM_WINDOWS 1
#elif defined(__APPLE__) && defined(__MACH__)
    #include <unistd.h>
    #define PLATFORM_MACOS 1
#elif defined(__linux__)
    #include <unistd.h>
    #define PLATFORM_LINUX 1
#else
    #define PLATFORM_UNKNOWN 1
#endif

/* Global state for cleanup */
#ifdef PLATFORM_WINDOWS
static UINT g_original_output_cp = 0;
static UINT g_original_input_cp = 0;
#endif

platform_init_config_t platform_init_get_defaults(void) {
    platform_init_config_t config = {
        .enable_colors = -1,  /* Auto-detect */
        .enable_utf8 = -1,    /* Auto-detect */
    };
    return config;
}

int platform_init_terminal(const platform_init_config_t *config) {
    platform_init_config_t cfg;
    
    /* Use defaults if no config provided */
    if (config == NULL) {
        cfg = platform_init_get_defaults();
    } else {
        cfg = *config;
    }

#ifdef PLATFORM_WINDOWS
    /* Windows-specific initialization */
    
    /* Save original code pages for cleanup */
    g_original_output_cp = GetConsoleOutputCP();
    g_original_input_cp = GetConsoleCP();
    
    /* Enable UTF-8 if requested or auto-detect */
    if (cfg.enable_utf8 != 0) {
        /* Set console to UTF-8 mode for proper Unicode display */
        if (!SetConsoleOutputCP(65001)) {
            fprintf(stderr, "Warning: Failed to set console output to UTF-8\n");
        }
        if (!SetConsoleCP(65001)) {
            fprintf(stderr, "Warning: Failed to set console input to UTF-8\n");
        }
    }
    
    /* Enable ANSI escape sequences on Windows 10+ */
    if (cfg.enable_colors != 0) {
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        if (hOut != INVALID_HANDLE_VALUE) {
            DWORD dwMode = 0;
            if (GetConsoleMode(hOut, &dwMode)) {
                dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
                SetConsoleMode(hOut, dwMode);
            }
        }
    }
    
    return 0;
    
#elif defined(PLATFORM_LINUX) || defined(PLATFORM_MACOS)
    /* Unix-like systems: check if stdout is a terminal */
    
    if (cfg.enable_colors == -1) {
        /* Auto-detect: colors enabled if output is a TTY */
        if (!isatty(STDOUT_FILENO)) {
            /* Output is redirected, disable colors */
            /* Note: This is informational only, apps can check isatty() themselves */
        }
    }
    
    /* UTF-8 is typically default on modern Unix systems */
    /* We assume the locale is already set correctly */
    
    return 0;
    
#else
    /* Unknown platform: no-op */
    (void)cfg;
    return 0;
#endif
}

void platform_cleanup_terminal(void) {
#ifdef PLATFORM_WINDOWS
    /* Restore original code pages */
    if (g_original_output_cp != 0) {
        SetConsoleOutputCP(g_original_output_cp);
    }
    if (g_original_input_cp != 0) {
        SetConsoleCP(g_original_input_cp);
    }
#endif
    /* Other platforms: no cleanup needed */
}

char **platform_get_argv_utf8(int argc, char *argv[]) {
#ifdef PLATFORM_WINDOWS
    /* 
     * On Windows, argv[] uses system default encoding (usually GBK/CP936).
     * Use Windows API to get UTF-8 encoded arguments.
     */
    (void)argv;  /* Original argv not used on Windows */
    
    int wargc;
    LPWSTR *wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    if (!wargv || wargc != argc) {
        /* Fallback to original argv on error */
        return argv;
    }
    
    /* Allocate array of string pointers */
    char **utf8_argv = (char**)malloc(sizeof(char*) * argc);
    if (!utf8_argv) {
        LocalFree(wargv);
        return argv;
    }
    
    /* Convert each wide-char argument to UTF-8 */
    for (int i = 0; i < argc; i++) {
        int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, NULL, 0, NULL, NULL);
        if (utf8_len > 0) {
            utf8_argv[i] = (char*)malloc(utf8_len);
            if (utf8_argv[i]) {
                WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, utf8_argv[i], utf8_len, NULL, NULL);
            } else {
                /* Allocation failed, use empty string */
                utf8_argv[i] = strdup("");
            }
        } else {
            /* Conversion failed, use empty string */
            utf8_argv[i] = strdup("");
        }
    }
    
    LocalFree(wargv);
    return utf8_argv;
    
#else
    /* On Unix-like systems, argv is already in UTF-8 (or should be) */
    (void)argc;
    return argv;
#endif
}

void platform_free_argv_utf8(char **utf8_argv, int argc) {
#ifdef PLATFORM_WINDOWS
    /* Free allocated memory on Windows */
    if (utf8_argv) {
        for (int i = 0; i < argc; i++) {
            free(utf8_argv[i]);
        }
        free(utf8_argv);
    }
#else
    /* On Unix-like systems, argv is not allocated by us */
    (void)utf8_argv;
    (void)argc;
#endif
}
