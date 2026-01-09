/**
 * @file chat_markdown.c
 * @brief Demo program for terminal Markdown rendering with streaming support
 * 
 * This demonstrates both batch and streaming Markdown rendering.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Include the markdown library */
#include "render/markdown/md.h"

/* Sample Markdown content for demonstration */
static const char* DEMO_MARKDOWN = 
"# Terminal Markdown Demo\n"
"\n"
"This is a **demonstration** of the terminal Markdown renderer.\n"
"\n"
"## Features\n"
"\n"
"### Inline Formatting\n"
"\n"
"- **Bold text** for emphasis\n"
"- *Italic text* for style\n"
"- ***Bold and italic*** combined\n"
"- `inline code` for commands\n"
"- [Links](https://example.com) with clickable URLs\n"
"\n"
"### Code Blocks\n"
"\n"
"```c\n"
"#include <stdio.h>\n"
"\n"
"int main() {\n"
"    printf(\"Hello, World!\\n\");\n"
"    return 0;\n"
"}\n"
"```\n"
"\n"
"### Lists\n"
"\n"
"Unordered list:\n"
"- First item\n"
"- Second item\n"
"  - Nested item 1\n"
"  - Nested item 2\n"
"- Third item\n"
"\n"
"Ordered list:\n"
"1. Step one\n"
"2. Step two\n"
"3. Step three\n"
"\n"
"### Block Quote\n"
"\n"
"> This is a block quote.\n"
"> It can span multiple lines.\n"
"\n"
"---\n"
"\n"
"### Table\n"
"\n"
"| Name     | Type    | Description          |\n"
"|:---------|:-------:|---------------------:|\n"
"| id       | int     | Primary key          |\n"
"| name     | string  | User's display name  |\n"
"| email    | string  | Contact email        |\n"
"\n"
"### Chinese Characters (CJK Support)\n"
"\n"
"中文测试：这是一段中文文本，用于测试宽字符的渲染效果。\n"
"\n"
"| 姓名   | 年龄 | 城市   |\n"
"|:-------|:----:|-------:|\n"
"| 张三   | 25   | 北京   |\n"
"| 李四   | 30   | 上海   |\n"
"\n"
"---\n"
"\n"
"## End of Demo\n"
"\n"
"That's all folks! 🎉\n"
;

/* Streaming demo: simulate character-by-character input */
static void demo_streaming(void) {
    printf("\n\n=== Streaming Demo ===\n\n");
    
    const char* streaming_md = 
        "# Streaming Mode\n"
        "\n"
        "This content is being rendered **incrementally**...\n"
        "\n"
        "```python\n"
        "def hello():\n"
        "    print(\"Hello from streaming!\")\n"
        "```\n"
        "\n"
        "- Item 1\n"
        "- Item 2\n"
        "- Item 3\n"
        "\n"
        "Done!\n";
    
    md_stream_t* stream = md_stream_new();
    if (!stream) {
        fprintf(stderr, "Failed to create stream\n");
        return;
    }
    
    /* Simulate streaming input - feed characters one at a time with delay */
    size_t len = strlen(streaming_md);
    for (size_t i = 0; i < len; i++) {
        md_stream_feed(stream, &streaming_md[i], 1);
        /* Small delay to show streaming effect */
        usleep(5000);  /* 5ms */
    }
    
    md_stream_finish(stream);
    md_stream_free(stream);
}

static void print_usage(const char* program) {
    printf("Usage: %s [options]\n", program);
    printf("\n");
    printf("Options:\n");
    printf("  -h, --help      Show this help message\n");
    printf("  -d, --demo      Run the full demo (default)\n");
    printf("  -s, --stream    Run streaming demo only\n");
    printf("  -f, --file FILE Render a Markdown file\n");
    printf("  -t, --text TEXT Render Markdown text\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s                         Run full demo\n", program);
    printf("  %s -s                      Run streaming demo\n", program);
    printf("  %s -f README.md            Render a file\n", program);
    printf("  %s -t '# Hello **World**'  Render inline text\n", program);
}

int main(int argc, char* argv[]) {
    int show_demo = 1;
    int show_stream = 0;
    const char* file_path = NULL;
    const char* text = NULL;
    
    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--demo") == 0) {
            show_demo = 1;
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--stream") == 0) {
            show_stream = 1;
            show_demo = 0;
        } else if ((strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--file") == 0) && i + 1 < argc) {
            file_path = argv[++i];
            show_demo = 0;
        } else if ((strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--text") == 0) && i + 1 < argc) {
            text = argv[++i];
            show_demo = 0;
        }
    }
    
    /* Render file if specified */
    if (file_path) {
        FILE* f = fopen(file_path, "r");
        if (!f) {
            fprintf(stderr, "Error: Cannot open file '%s'\n", file_path);
            return 1;
        }
        
        /* Read entire file */
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        
        char* content = (char*)malloc(size + 1);
        if (!content) {
            fclose(f);
            fprintf(stderr, "Error: Out of memory\n");
            return 1;
        }
        
        size_t read_size = fread(content, 1, size, f);
        content[read_size] = '\0';
        fclose(f);
        
        md_render(content);
        free(content);
        return 0;
    }
    
    /* Render text if specified */
    if (text) {
        md_render(text);
        return 0;
    }
    
    /* Run demos */
    if (show_demo) {
        printf("=== Batch Rendering Demo ===\n\n");
        md_render(DEMO_MARKDOWN);
    }
    
    if (show_stream || show_demo) {
        demo_streaming();
    }
    
    return 0;
}
