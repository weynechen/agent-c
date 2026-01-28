/**
 * @file moc_utils.c
 * @brief Utility functions for MOC
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "moc.h"

/*============================================================================
 * File Reading
 *============================================================================*/

/**
 * Read entire file into memory
 */
static char *read_file(const char *path, size_t *out_len) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }

    /* Get file size */
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (size < 0) {
        fclose(file);
        return NULL;
    }

    /* Allocate buffer */
    char *buffer = malloc(size + 1);
    if (!buffer) {
        fclose(file);
        return NULL;
    }

    /* Read file */
    size_t read_size = fread(buffer, 1, size, file);
    fclose(file);

    buffer[read_size] = '\0';
    if (out_len) {
        *out_len = read_size;
    }

    return buffer;
}

/*============================================================================
 * Initialization and Cleanup
 *============================================================================*/

int moc_init(moc_ctx_t *ctx, const char *input_file, const char *output_base) {
    if (!ctx || !input_file) {
        fprintf(stderr, "Error: Invalid arguments to moc_init\n");
        return -1;
    }

    memset(ctx, 0, sizeof(moc_ctx_t));
    ctx->input_file = input_file;
    ctx->output_base = output_base;

    /* Read input file */
    size_t source_len;
    char *source = read_file(input_file, &source_len);
    if (!source) {
        fprintf(stderr, "Error: Failed to read file: %s\n", input_file);
        return -1;
    }

    ctx->source_code = source;
    ctx->source_len = source_len;

    return 0;
}

void moc_cleanup(moc_ctx_t *ctx) {
    if (ctx) {
        if (ctx->source_code) {
            free((void *)ctx->source_code);
        }
        memset(ctx, 0, sizeof(moc_ctx_t));
    }
}

/*============================================================================
 * Utility Functions
 *============================================================================*/

void moc_extract_text(const char *source, size_t start, size_t end,
                      char *dest, size_t dest_sz) {
    if (!source || !dest || dest_sz == 0) {
        return;
    }

    size_t len = end - start;
    if (len >= dest_sz) {
        len = dest_sz - 1;
    }

    memcpy(dest, source + start, len);
    dest[len] = '\0';
}

void moc_print_tool(const moc_tool_t *tool) {
    if (!tool) return;

    printf("  Name: %s\n", tool->name);
    printf("  Description: %s\n", tool->description);
    printf("  Return type: %s (category: %d)\n", 
           tool->return_type, tool->return_type_cat);
    printf("  Parameters (%d):\n", tool->param_count);

    for (int i = 0; i < tool->param_count; i++) {
        const moc_param_t *param = &tool->params[i];
        printf("    [%d] %s %s - %s\n",
               i, param->type_str, param->name, param->description);
    }
    printf("\n");
}
