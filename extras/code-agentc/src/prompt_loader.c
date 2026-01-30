/**
 * @file prompt_loader.c
 * @brief Prompt Loading and Rendering Implementation
 */

#include "prompt_loader.h"
#include "prompts_gen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Platform-specific includes */
#ifdef _WIN32
    #include <windows.h>
    #include <direct.h>
    #define getcwd _getcwd
#else
    #include <unistd.h>
    #include <pwd.h>
    #include <sys/utsname.h>
#endif

/*============================================================================
 * Prompt Access
 *============================================================================*/

const char *prompt_get_system(const char *name) {
    if (!name) return NULL;
    
    for (int i = 0; i < SYSTEM_PROMPTS_COUNT; i++) {
        if (SYSTEM_PROMPTS[i].name && strcmp(SYSTEM_PROMPTS[i].name, name) == 0) {
            return SYSTEM_PROMPTS[i].content;
        }
    }
    
    return NULL;
}

const char *prompt_get_tool(const char *name) {
    if (!name) return NULL;
    
    for (int i = 0; i < TOOL_PROMPTS_COUNT; i++) {
        if (TOOL_PROMPTS[i].name && strcmp(TOOL_PROMPTS[i].name, name) == 0) {
            return TOOL_PROMPTS[i].content;
        }
    }
    
    return NULL;
}

/*============================================================================
 * Variable Substitution Helper
 *============================================================================*/

/**
 * @brief Replace all occurrences of a pattern in a string
 */
static char *string_replace(const char *str, const char *pattern, const char *replacement) {
    if (!str || !pattern || !replacement) return NULL;
    
    size_t pattern_len = strlen(pattern);
    size_t replacement_len = strlen(replacement);
    
    if (pattern_len == 0) {
        return strdup(str);
    }
    
    /* Count occurrences */
    int count = 0;
    const char *p = str;
    while ((p = strstr(p, pattern)) != NULL) {
        count++;
        p += pattern_len;
    }
    
    if (count == 0) {
        return strdup(str);
    }
    
    /* Allocate result */
    size_t str_len = strlen(str);
    size_t result_len = str_len + count * (replacement_len - pattern_len);
    char *result = malloc(result_len + 1);
    if (!result) return NULL;
    
    /* Build result */
    char *dst = result;
    const char *src = str;
    
    while (*src) {
        if (strncmp(src, pattern, pattern_len) == 0) {
            memcpy(dst, replacement, replacement_len);
            dst += replacement_len;
            src += pattern_len;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
    
    return result;
}

/*============================================================================
 * Prompt Rendering
 *============================================================================*/

char *prompt_render_system(const char *name, const char *workspace) {
    const char *content = prompt_get_system(name);
    if (!content) return NULL;
    
    /* Replace ${workspace} */
    const char *ws = workspace ? workspace : ".";
    char *result = string_replace(content, "${workspace}", ws);
    
    return result;
}

char *prompt_render_tool(const char *name, const char *workspace) {
    const char *content = prompt_get_tool(name);
    if (!content) return NULL;
    
    /* Replace ${workspace} and ${directory} */
    const char *ws = workspace ? workspace : ".";
    
    char *temp = string_replace(content, "${workspace}", ws);
    if (!temp) return NULL;
    
    char *result = string_replace(temp, "${directory}", ws);
    free(temp);
    
    return result;
}

/*============================================================================
 * Context Initialization
 *============================================================================*/

void prompt_context_init(prompt_context_t *ctx, const char *workspace) {
    if (!ctx) return;
    
    memset(ctx, 0, sizeof(*ctx));
    
    /* Set workspace and directory (alias) */
    ctx->workspace = workspace ? workspace : ".";
    ctx->directory = ctx->workspace;
    
    /* Get current working directory */
    static char cwd_buf[4096];
    if (getcwd(cwd_buf, sizeof(cwd_buf)) != NULL) {
        ctx->cwd = cwd_buf;
    } else {
        ctx->cwd = ".";
    }
    
#ifdef _WIN32
    /* Windows: Detect OS */
    ctx->os = "Windows";
    
    /* Windows: Get shell (typically cmd or powershell) */
    const char *comspec = getenv("COMSPEC");
    if (comspec) {
        const char *slash = strrchr(comspec, '\\');
        ctx->shell = slash ? slash + 1 : comspec;
    } else {
        ctx->shell = "cmd.exe";
    }
    
    /* Windows: Get username */
    const char *user_env = getenv("USERNAME");
    ctx->user = user_env ? user_env : "unknown";
    
#else
    /* POSIX: Detect OS */
    struct utsname uts;
    static char os_buf[128];
    if (uname(&uts) == 0) {
        snprintf(os_buf, sizeof(os_buf), "%s", uts.sysname);
        ctx->os = os_buf;
    } else {
        ctx->os = "unknown";
    }
    
    /* POSIX: Detect shell from environment */
    const char *shell_env = getenv("SHELL");
    if (shell_env) {
        /* Extract basename */
        const char *slash = strrchr(shell_env, '/');
        ctx->shell = slash ? slash + 1 : shell_env;
    } else {
        ctx->shell = "sh";
    }
    
    /* POSIX: Get username */
    struct passwd *pw = getpwuid(getuid());
    if (pw) {
        ctx->user = pw->pw_name;
    } else {
        const char *user_env = getenv("USER");
        ctx->user = user_env ? user_env : "unknown";
    }
#endif
    
    /* Default settings */
    ctx->safe_mode = 1;
    ctx->sandbox_enabled = 1;
}

/*============================================================================
 * Context-based Rendering
 *============================================================================*/

char *prompt_render(const char *template, const prompt_context_t *ctx) {
    if (!template) return NULL;
    
    /* Use defaults if no context */
    prompt_context_t default_ctx;
    if (!ctx) {
        prompt_context_init(&default_ctx, ".");
        ctx = &default_ctx;
    }
    
    /* Apply substitutions in sequence */
    char *result = strdup(template);
    if (!result) return NULL;
    
    /* Define placeholder mappings */
    struct {
        const char *placeholder;
        const char *value;
    } mappings[] = {
        { "${workspace}", ctx->workspace },
        { "${cwd}", ctx->cwd },
        { "${directory}", ctx->directory },
        { "${os}", ctx->os },
        { "${shell}", ctx->shell },
        { "${user}", ctx->user },
        { "${safe_mode}", ctx->safe_mode ? "enabled" : "disabled" },
        { "${sandbox}", ctx->sandbox_enabled ? "enabled" : "disabled" },
        { NULL, NULL }
    };
    
    /* Apply each substitution */
    for (int i = 0; mappings[i].placeholder != NULL; i++) {
        if (!mappings[i].value) continue;
        
        char *temp = string_replace(result, mappings[i].placeholder, mappings[i].value);
        if (temp) {
            free(result);
            result = temp;
        }
    }
    
    return result;
}

char *prompt_render_system_ctx(const char *name, const prompt_context_t *ctx) {
    const char *content = prompt_get_system(name);
    if (!content) return NULL;
    
    return prompt_render(content, ctx);
}

char *prompt_render_tool_ctx(const char *name, const prompt_context_t *ctx) {
    const char *content = prompt_get_tool(name);
    if (!content) return NULL;
    
    return prompt_render(content, ctx);
}

/*============================================================================
 * Prompt Enumeration
 *============================================================================*/

int prompt_system_count(void) {
    return SYSTEM_PROMPTS_COUNT;
}

int prompt_tool_count(void) {
    return TOOL_PROMPTS_COUNT;
}

const char *prompt_system_name(int index) {
    if (index < 0 || index >= SYSTEM_PROMPTS_COUNT) {
        return NULL;
    }
    return SYSTEM_PROMPTS[index].name;
}

const char *prompt_tool_name(int index) {
    if (index < 0 || index >= TOOL_PROMPTS_COUNT) {
        return NULL;
    }
    return TOOL_PROMPTS[index].name;
}
