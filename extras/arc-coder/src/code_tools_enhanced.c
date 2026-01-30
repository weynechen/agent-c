/**
 * @file code_tools_enhanced.c
 * @brief Enhanced Tool Registration with Prompt Integration
 *
 * Merges MOC-generated tool schemas with rich prompt descriptions.
 */

#include "code_tools_enhanced.h"
#include "code_tools_gen.h"
#include "prompt_loader.h"
#include <stdlib.h>
#include <string.h>

/*============================================================================
 * Tool Name Mapping
 *============================================================================*/

/**
 * @brief Mapping between MOC tool names and prompt file names
 *
 * MOC generates tools with function-based names, but prompt files
 * may use different naming conventions.
 */
static const code_tool_name_map_t TOOL_NAME_MAP[] = {
    { "bash",       "bash" },
    { "read_file",  "read" },
    { "write_file", "write" },
    { "edit_file",  "edit" },
    { "ls",         "ls" },
    { "grep",       "grep" },
    { "glob_files", "glob" },
    { NULL, NULL }
};

const char *code_tool_get_prompt_name(const char *moc_name) {
    if (!moc_name) return NULL;

    for (int i = 0; TOOL_NAME_MAP[i].moc_name != NULL; i++) {
        if (strcmp(TOOL_NAME_MAP[i].moc_name, moc_name) == 0) {
            return TOOL_NAME_MAP[i].prompt_name;
        }
    }

    /* No mapping found, use MOC name as-is */
    return moc_name;
}

/*============================================================================
 * Enhanced Tool Creation
 *============================================================================*/

ac_tool_t **code_tools_enhanced_create(
    const prompt_context_t *ctx,
    size_t *out_count
) {
    if (!out_count) return NULL;
    *out_count = 0;

    /* Get count from MOC-generated tools */
    size_t count = ALL_TOOLS_COUNT;
    if (count == 0) return NULL;

    /* Allocate tool pointer array */
    ac_tool_t **tools = calloc(count, sizeof(ac_tool_t *));
    if (!tools) return NULL;

    /* Allocate and populate each tool */
    for (size_t i = 0; i < count; i++) {
        const ac_tool_t *moc_tool = ALL_TOOLS[i];
        if (!moc_tool) continue;

        /* Allocate new tool structure */
        ac_tool_t *tool = malloc(sizeof(ac_tool_t));
        if (!tool) {
            /* Cleanup on failure */
            code_tools_enhanced_free(tools, i);
            return NULL;
        }

        /* Copy base structure from MOC */
        *tool = *moc_tool;

        /* Try to find enhanced description from prompt files */
        const char *prompt_name = code_tool_get_prompt_name(moc_tool->name);
        char *rendered_desc = prompt_render_tool_ctx(prompt_name, ctx);

        if (rendered_desc) {
            /* Use rendered prompt as description */
            tool->description = rendered_desc;
        }
        /* else: keep MOC-generated description */

        tools[i] = tool;
    }

    *out_count = count;
    return tools;
}

void code_tools_enhanced_free(ac_tool_t **tools, size_t count) {
    if (!tools) return;

    for (size_t i = 0; i < count; i++) {
        if (tools[i]) {
            /* Free rendered description if it was allocated */
            /* Note: We can't easily distinguish between static MOC desc
             * and dynamic rendered desc, so we check if it differs from MOC */
            const ac_tool_t *moc_tool = ALL_TOOLS[i];
            if (moc_tool && tools[i]->description != moc_tool->description) {
                free((void *)tools[i]->description);
            }
            free(tools[i]);
        }
    }

    free(tools);
}

int code_tools_register_enhanced(
    ac_tool_registry_t *registry,
    const prompt_context_t *ctx
) {
    if (!registry) return -1;

    size_t count;
    ac_tool_t **tools = code_tools_enhanced_create(ctx, &count);
    if (!tools) return -1;

    int registered = 0;
    for (size_t i = 0; i < count; i++) {
        if (tools[i]) {
            if (ac_tool_registry_add(registry, tools[i]) == ARC_OK) {
                registered++;
            }
        }
    }

    /* Free the temporary tool structures */
    /* Note: registry makes a copy, so we can free ours */
    code_tools_enhanced_free(tools, count);

    return registered;
}
