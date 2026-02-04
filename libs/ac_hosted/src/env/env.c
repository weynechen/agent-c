/**
 * @file env.c
 * @brief Environment Configuration Loader Implementation
 *
 * Multi-level .env loading with XDG Base Directory support.
 */

#include "arc/env.h"
#include "arc/log.h"
#include "dotenv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <shlobj.h>
#define PATH_SEP '\\'
#define mkdir(path, mode) _mkdir(path)
#else
#include <unistd.h>
#include <pwd.h>
#define PATH_SEP '/'
#endif

/*============================================================================
 * Internal Helpers
 *============================================================================*/

/**
 * @brief Get home directory path
 */
static const char *get_home_dir(void)
{
#ifdef _WIN32
    static char home[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_PROFILE, NULL, 0, home))) {
        return home;
    }
    return getenv("USERPROFILE");
#else
    const char *home = getenv("HOME");
    if (home && home[0] != '\0') {
        return home;
    }

    /* Fallback to passwd entry */
    struct passwd *pw = getpwuid(getuid());
    if (pw) {
        return pw->pw_dir;
    }

    return NULL;
#endif
}

/**
 * @brief Check if directory exists
 */
static bool dir_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/**
 * @brief Create directory if not exists
 */
static bool ensure_dir(const char *path)
{
    if (dir_exists(path)) {
        return true;
    }

#ifdef _WIN32
    return mkdir(path) == 0;
#else
    return mkdir(path, 0755) == 0;
#endif
}

/**
 * @brief Load .env file from path (not overwriting existing env vars)
 */
static bool load_env_file(const char *path, bool verbose)
{
    if (env_load(path, false) == 0) {
        if (verbose) {
            printf("[Loaded %s%c.env]\n", path, PATH_SEP);
        }
        return true;
    }
    return false;
}

/*============================================================================
 * Configuration Paths
 *============================================================================*/

char *ac_env_get_config_dir(char *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size < 64) {
        return NULL;
    }

    /* Check XDG_CONFIG_HOME first */
    const char *xdg_config = getenv("XDG_CONFIG_HOME");
    if (xdg_config && xdg_config[0] != '\0') {
        snprintf(buffer, buffer_size, "%s%carc", xdg_config, PATH_SEP);
    } else {
        /* Default to ~/.config/arc */
        const char *home = get_home_dir();
        if (!home) {
            return NULL;
        }
        snprintf(buffer, buffer_size, "%s%c.config%carc", home, PATH_SEP, PATH_SEP);
    }

    /* Ensure parent .config exists */
    char parent[512];
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0] != '\0') {
        snprintf(parent, sizeof(parent), "%s", xdg);
    } else {
        const char *home = get_home_dir();
        if (home) {
            snprintf(parent, sizeof(parent), "%s%c.config", home, PATH_SEP);
        }
    }
    ensure_dir(parent);

    /* Ensure arc config dir exists */
    ensure_dir(buffer);

    return buffer;
}

/*============================================================================
 * Environment Loading
 *============================================================================*/

static int ac_env_load_internal(const char *app_name, bool verbose)
{
    int loaded = 0;
    char path[512];

    /* 1. Load from user config directory: ~/.config/arc/.env */
    if (ac_env_get_config_dir(path, sizeof(path))) {
        if (load_env_file(path, verbose)) {
            loaded++;
        }

        /* 2. Load from app-specific config: ~/.config/arc/<app_name>/.env */
        if (app_name && app_name[0] != '\0') {
            char app_path[512];
            snprintf(app_path, sizeof(app_path), "%s%c%s", path, PATH_SEP, app_name);
            if (load_env_file(app_path, verbose)) {
                loaded++;
            }
        }
    }

    /* 3. Load from current directory: ./.env */
    if (load_env_file(".", verbose)) {
        loaded++;
    }

    return loaded;
}

int ac_env_load(const char *app_name)
{
    return ac_env_load_internal(app_name, false);
}

int ac_env_load_verbose(const char *app_name)
{
    return ac_env_load_internal(app_name, true);
}

/*============================================================================
 * Environment Access
 *============================================================================*/

const char *ac_env_get(const char *name, const char *default_value)
{
    if (!name) {
        return default_value;
    }

    const char *value = getenv(name);
    if (!value || value[0] == '\0') {
        return default_value;
    }

    return value;
}

const char *ac_env_require(const char *name)
{
    if (!name) {
        AC_LOG_ERROR("Environment variable name is NULL");
        return NULL;
    }

    const char *value = getenv(name);
    if (!value || value[0] == '\0') {
        AC_LOG_ERROR("%s not set", name);
        return NULL;
    }

    return value;
}

bool ac_env_isset(const char *name)
{
    if (!name) {
        return false;
    }

    const char *value = getenv(name);
    return value && value[0] != '\0';
}

/*============================================================================
 * Help Output
 *============================================================================*/

void ac_env_print_help(const char *app_name)
{
    char config_dir[512];

    printf("\n");
    printf("Environment Configuration\n");
    printf("=========================\n\n");

    printf("This application requires environment variables to be set.\n");
    printf("You can set them in any of the following locations:\n\n");

    printf("1. Shell environment (highest priority):\n");
    printf("   export OPENAI_API_KEY=sk-xxx\n\n");

    if (ac_env_get_config_dir(config_dir, sizeof(config_dir))) {
        printf("2. User config directory:\n");
        printf("   %s%c.env\n\n", config_dir, PATH_SEP);

        if (app_name && app_name[0] != '\0') {
            printf("3. App-specific config:\n");
            printf("   %s%c%s%c.env\n\n", config_dir, PATH_SEP, app_name, PATH_SEP);
        }
    }

    printf("4. Current working directory:\n");
    printf("   ./.env\n\n");

    printf("Example .env file contents:\n");
    printf("---------------------------\n");
    printf("OPENAI_API_KEY=sk-xxx\n");
    printf("OPENAI_MODEL=gpt-4o-mini\n");
    printf("OPENAI_BASE_URL=https://api.openai.com/v1\n");
    printf("\n");
}
