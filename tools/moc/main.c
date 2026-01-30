/**
 * @file main.c
 * @brief MOC (Meta-Object Compiler) Entry Point
 *
 * MOC parses C header files with AC_TOOL_META markers and generates
 * wrapper functions and tool registration code.
 *
 * Usage:
 *   moc [options] <input.h>
 *
 * Options:
 *   -o <basename>   Output file base name (generates basename.h and basename.c)
 *   -v              Verbose output
 *   -h              Show help
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#include "moc.h"

/*============================================================================
 * Usage and Help
 *============================================================================*/

static void print_usage(const char *prog_name) {
    printf("Usage: %s [options] <input.h>\n", prog_name);
    printf("\n");
    printf("MOC (Meta-Object Compiler) for ArC Tool Generation\n");
    printf("\n");
    printf("Parses C header files with AC_TOOL_META markers and Doxygen-style\n");
    printf("comments, generating wrapper functions and tool registration code.\n");
    printf("\n");
    printf("Options:\n");
    printf("  -o <basename>   Output file base name (generates basename.h and basename.c)\n");
    printf("                  If not specified, outputs to stdout\n");
    printf("  -v              Verbose output (show parsed tools)\n");
    printf("  -h              Show this help message\n");
    printf("\n");
    printf("Example:\n");
    printf("  %s -o tools_gen tools.h\n", prog_name);
    printf("  This generates tools_gen.h and tools_gen.c from tools.h\n");
    printf("\n");
    printf("Input file format:\n");
    printf("  /**\n");
    printf("   * @description: Get weather for a city\n");
    printf("   * @param: place  The city name\n");
    printf("   */\n");
    printf("  AC_TOOL_META const char* get_weather(const char* place);\n");
}

static void print_version(void) {
    printf("MOC (Meta-Object Compiler) version 1.0.0\n");
    printf("Part of ArC - C-native AI Agent Framework\n");
}

/*============================================================================
 * Main Entry Point
 *============================================================================*/

int main(int argc, char *argv[]) {
    const char *output_base = NULL;
    const char *input_file = NULL;
    bool verbose = false;
    int opt;

    /* Parse command line options */
    while ((opt = getopt(argc, argv, "o:vhV")) != -1) {
        switch (opt) {
            case 'o':
                output_base = optarg;
                break;
            case 'v':
                verbose = true;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            case 'V':
                print_version();
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    /* Get input file */
    if (optind >= argc) {
        fprintf(stderr, "Error: No input file specified\n\n");
        print_usage(argv[0]);
        return 1;
    }

    input_file = argv[optind];

    /* Initialize MOC context */
    moc_ctx_t ctx;
    if (moc_init(&ctx, input_file, output_base) != 0) {
        return 1;
    }

    ctx.verbose = verbose;

    if (verbose) {
        printf("MOC: Processing %s\n", input_file);
    }

    /* Parse input file */
    if (moc_parse(&ctx) != 0) {
        fprintf(stderr, "Error: Failed to parse %s\n", input_file);
        moc_cleanup(&ctx);
        return 1;
    }

    if (ctx.tool_count == 0) {
        fprintf(stderr, "Warning: No AC_TOOL_META functions found in %s\n", input_file);
        moc_cleanup(&ctx);
        return 0;
    }

    if (verbose) {
        printf("Found %d tool(s)\n\n", ctx.tool_count);
    }

    /* Generate output */
    if (moc_generate(&ctx) != 0) {
        fprintf(stderr, "Error: Failed to generate output\n");
        moc_cleanup(&ctx);
        return 1;
    }

    if (verbose && output_base) {
        printf("\nGeneration complete:\n");
        printf("  %s.h - Header with wrapper declarations\n", output_base);
        printf("  %s.c - Source with wrappers and registration table\n", output_base);
    }

    /* Cleanup */
    moc_cleanup(&ctx);

    return 0;
}
