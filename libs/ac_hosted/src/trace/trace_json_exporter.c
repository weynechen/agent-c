/**
 * @file trace_json_exporter.c
 * @brief JSON file exporter for ArC traces
 */

#include "arc/trace_exporters.h"
#include "arc/trace.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define mkdir_p(path) _mkdir(path)
#else
#include <sys/time.h>
#define mkdir_p(path) mkdir(path, 0755)
#endif

/*============================================================================
 * Static State
 *============================================================================*/

typedef struct {
    ac_trace_json_config_t config;
    FILE *file;
    char current_path[512];
    char current_trace_id[64];
    int event_count;
    int initialized;
} json_exporter_state_t;

static json_exporter_state_t s_state = {0};

/*============================================================================
 * Helper Functions
 *============================================================================*/

static int ensure_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode) ? 0 : -1;
    }
    if (mkdir_p(path) != 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

static void format_iso_timestamp(uint64_t ts_ms, char *buf, size_t size) {
    time_t secs = (time_t)(ts_ms / 1000);
    int ms = (int)(ts_ms % 1000);
    struct tm *tm_info = gmtime(&secs);

    snprintf(buf, size, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
             tm_info->tm_year + 1900,
             tm_info->tm_mon + 1,
             tm_info->tm_mday,
             tm_info->tm_hour,
             tm_info->tm_min,
             tm_info->tm_sec,
             ms);
}

static void format_file_timestamp(char *buf, size_t size) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);

    snprintf(buf, size, "%04d%02d%02d_%02d%02d%02d",
             tm_info->tm_year + 1900,
             tm_info->tm_mon + 1,
             tm_info->tm_mday,
             tm_info->tm_hour,
             tm_info->tm_min,
             tm_info->tm_sec);
}

static void write_json_string(FILE *f, const char *str) {
    if (!str) {
        fprintf(f, "null");
        return;
    }

    fputc('"', f);
    for (const char *p = str; *p; p++) {
        switch (*p) {
            case '"':  fputs("\\\"", f); break;
            case '\\': fputs("\\\\", f); break;
            case '\n': fputs("\\n", f); break;
            case '\r': fputs("\\r", f); break;
            case '\t': fputs("\\t", f); break;
            default:
                if ((unsigned char)*p < 0x20) {
                    fprintf(f, "\\u%04x", (unsigned char)*p);
                } else {
                    fputc(*p, f);
                }
        }
    }
    fputc('"', f);
}

static void write_indent(FILE *f, int level, int pretty) {
    if (!pretty) return;
    for (int i = 0; i < level; i++) {
        fputs("  ", f);
    }
}

static void write_newline(FILE *f, int pretty) {
    if (pretty) fputc('\n', f);
}

/*============================================================================
 * Event Data Writers
 *============================================================================*/

static void write_agent_start(FILE *f, const ac_trace_agent_start_t *data, int pretty) {
    int indent = pretty ? 4 : 0;

    write_indent(f, indent, pretty);
    fputs("\"message\": ", f);
    write_json_string(f, data->message);
    fputs(",", f);
    write_newline(f, pretty);

    write_indent(f, indent, pretty);
    fputs("\"instructions\": ", f);
    write_json_string(f, data->instructions);
    fputs(",", f);
    write_newline(f, pretty);

    write_indent(f, indent, pretty);
    fprintf(f, "\"max_iterations\": %d,", data->max_iterations);
    write_newline(f, pretty);

    write_indent(f, indent, pretty);
    fprintf(f, "\"tool_count\": %zu", data->tool_count);
}

static void write_agent_end(FILE *f, const ac_trace_agent_end_t *data, int pretty) {
    int indent = pretty ? 4 : 0;

    write_indent(f, indent, pretty);
    fputs("\"content\": ", f);
    write_json_string(f, data->content);
    fputs(",", f);
    write_newline(f, pretty);

    write_indent(f, indent, pretty);
    fprintf(f, "\"iterations\": %d,", data->iterations);
    write_newline(f, pretty);

    write_indent(f, indent, pretty);
    fprintf(f, "\"total_prompt_tokens\": %d,", data->total_prompt_tokens);
    write_newline(f, pretty);

    write_indent(f, indent, pretty);
    fprintf(f, "\"total_completion_tokens\": %d,", data->total_completion_tokens);
    write_newline(f, pretty);

    write_indent(f, indent, pretty);
    fprintf(f, "\"duration_ms\": %llu", (unsigned long long)data->duration_ms);
}

static void write_iter(FILE *f, const ac_trace_iter_t *data, int pretty) {
    int indent = pretty ? 4 : 0;

    write_indent(f, indent, pretty);
    fprintf(f, "\"iteration\": %d,", data->iteration);
    write_newline(f, pretty);

    write_indent(f, indent, pretty);
    fprintf(f, "\"max_iterations\": %d", data->max_iterations);
}

static void write_llm_request(FILE *f, const ac_trace_llm_request_t *data, int pretty) {
    int indent = pretty ? 4 : 0;

    write_indent(f, indent, pretty);
    fputs("\"model\": ", f);
    write_json_string(f, data->model);
    fputs(",", f);
    write_newline(f, pretty);

    write_indent(f, indent, pretty);
    fprintf(f, "\"message_count\": %zu,", data->message_count);
    write_newline(f, pretty);

    write_indent(f, indent, pretty);
    fputs("\"messages\": ", f);
    if (data->messages_json) {
        fputs(data->messages_json, f);
    } else {
        fputs("null", f);
    }
    fputs(",", f);
    write_newline(f, pretty);

    write_indent(f, indent, pretty);
    fputs("\"tools\": ", f);
    if (data->tools_json) {
        fputs(data->tools_json, f);
    } else {
        fputs("null", f);
    }
}

static void write_llm_response(FILE *f, const ac_trace_llm_response_t *data, int pretty) {
    int indent = pretty ? 4 : 0;

    write_indent(f, indent, pretty);
    fputs("\"content\": ", f);
    write_json_string(f, data->content);
    fputs(",", f);
    write_newline(f, pretty);

    write_indent(f, indent, pretty);
    fprintf(f, "\"tool_call_count\": %d,", data->tool_call_count);
    write_newline(f, pretty);

    write_indent(f, indent, pretty);
    fputs("\"tool_calls\": ", f);
    if (data->tool_calls_json) {
        fputs(data->tool_calls_json, f);
    } else {
        fputs("null", f);
    }
    fputs(",", f);
    write_newline(f, pretty);

    write_indent(f, indent, pretty);
    fprintf(f, "\"prompt_tokens\": %d,", data->prompt_tokens);
    write_newline(f, pretty);

    write_indent(f, indent, pretty);
    fprintf(f, "\"completion_tokens\": %d,", data->completion_tokens);
    write_newline(f, pretty);

    write_indent(f, indent, pretty);
    fprintf(f, "\"total_tokens\": %d,", data->total_tokens);
    write_newline(f, pretty);

    write_indent(f, indent, pretty);
    fputs("\"finish_reason\": ", f);
    write_json_string(f, data->finish_reason);
    fputs(",", f);
    write_newline(f, pretty);

    write_indent(f, indent, pretty);
    fprintf(f, "\"duration_ms\": %llu", (unsigned long long)data->duration_ms);
}

static void write_tool_start(FILE *f, const ac_trace_tool_start_t *data, int pretty) {
    int indent = pretty ? 4 : 0;

    write_indent(f, indent, pretty);
    fputs("\"id\": ", f);
    write_json_string(f, data->id);
    fputs(",", f);
    write_newline(f, pretty);

    write_indent(f, indent, pretty);
    fputs("\"name\": ", f);
    write_json_string(f, data->name);
    fputs(",", f);
    write_newline(f, pretty);

    write_indent(f, indent, pretty);
    fputs("\"arguments\": ", f);
    if (data->arguments) {
        fputs(data->arguments, f);
    } else {
        fputs("{}", f);
    }
}

static void write_tool_end(FILE *f, const ac_trace_tool_end_t *data, int pretty) {
    int indent = pretty ? 4 : 0;

    write_indent(f, indent, pretty);
    fputs("\"id\": ", f);
    write_json_string(f, data->id);
    fputs(",", f);
    write_newline(f, pretty);

    write_indent(f, indent, pretty);
    fputs("\"name\": ", f);
    write_json_string(f, data->name);
    fputs(",", f);
    write_newline(f, pretty);

    write_indent(f, indent, pretty);
    fputs("\"result\": ", f);
    if (data->result) {
        fputs(data->result, f);
    } else {
        fputs("null", f);
    }
    fputs(",", f);
    write_newline(f, pretty);

    write_indent(f, indent, pretty);
    fprintf(f, "\"duration_ms\": %llu,", (unsigned long long)data->duration_ms);
    write_newline(f, pretty);

    write_indent(f, indent, pretty);
    fprintf(f, "\"success\": %s", data->success ? "true" : "false");
}

/*============================================================================
 * Trace Handler
 *============================================================================*/

static void json_trace_handler(const ac_trace_event_t *event, void *user_data) {
    (void)user_data;

    if (!event) return;

    json_exporter_state_t *state = &s_state;
    int pretty = state->config.pretty_print;

    /* Handle agent_start - open new file */
    if (event->type == AC_TRACE_AGENT_START) {
        if (state->file) {
            write_newline(state->file, pretty);
            write_indent(state->file, 1, pretty);
            fputs("]", state->file);
            write_newline(state->file, pretty);
            fputs("}", state->file);
            write_newline(state->file, pretty);
            fclose(state->file);
            state->file = NULL;
        }

        char ts_buf[32];
        format_file_timestamp(ts_buf, sizeof(ts_buf));

        const char *agent_name = event->agent_name ? event->agent_name : "agent";
        snprintf(state->current_path, sizeof(state->current_path),
                 "%s/%s_%s.json",
                 state->config.output_dir,
                 agent_name,
                 ts_buf);

        snprintf(state->current_trace_id, sizeof(state->current_trace_id),
                 "%s", event->trace_id ? event->trace_id : "");

        state->file = fopen(state->current_path, "w");
        if (!state->file) {
            fprintf(stderr, "[TRACE] Failed to open %s: %s\n",
                    state->current_path, strerror(errno));
            return;
        }

        state->event_count = 0;

        fputs("{", state->file);
        write_newline(state->file, pretty);

        write_indent(state->file, 1, pretty);
        fputs("\"trace_id\": ", state->file);
        write_json_string(state->file, event->trace_id);
        fputs(",", state->file);
        write_newline(state->file, pretty);

        write_indent(state->file, 1, pretty);
        fputs("\"agent_name\": ", state->file);
        write_json_string(state->file, event->agent_name);
        fputs(",", state->file);
        write_newline(state->file, pretty);

        if (state->config.include_timestamps) {
            char iso_ts[64];
            format_iso_timestamp(event->timestamp_ms, iso_ts, sizeof(iso_ts));
            write_indent(state->file, 1, pretty);
            fputs("\"start_time\": ", state->file);
            write_json_string(state->file, iso_ts);
            fputs(",", state->file);
            write_newline(state->file, pretty);
        }

        write_indent(state->file, 1, pretty);
        fputs("\"events\": [", state->file);
    }

    if (!state->file) return;

    if (state->event_count > 0) {
        fputs(",", state->file);
    }
    write_newline(state->file, pretty);
    state->event_count++;

    write_indent(state->file, 2, pretty);
    fputs("{", state->file);
    write_newline(state->file, pretty);

    write_indent(state->file, 3, pretty);
    fputs("\"type\": ", state->file);
    write_json_string(state->file, ac_trace_event_name(event->type));
    fputs(",", state->file);
    write_newline(state->file, pretty);

    if (state->config.include_timestamps) {
        char iso_ts[64];
        format_iso_timestamp(event->timestamp_ms, iso_ts, sizeof(iso_ts));
        write_indent(state->file, 3, pretty);
        fputs("\"timestamp\": ", state->file);
        write_json_string(state->file, iso_ts);
        fputs(",", state->file);
        write_newline(state->file, pretty);
    }

    write_indent(state->file, 3, pretty);
    fprintf(state->file, "\"timestamp_ms\": %llu,", (unsigned long long)event->timestamp_ms);
    write_newline(state->file, pretty);

    write_indent(state->file, 3, pretty);
    fprintf(state->file, "\"sequence\": %d,", event->sequence);
    write_newline(state->file, pretty);

    write_indent(state->file, 3, pretty);
    fputs("\"data\": {", state->file);
    write_newline(state->file, pretty);

    switch (event->type) {
        case AC_TRACE_AGENT_START:
            write_agent_start(state->file, &event->data.agent_start, pretty);
            break;
        case AC_TRACE_AGENT_END:
            write_agent_end(state->file, &event->data.agent_end, pretty);
            break;
        case AC_TRACE_ITER_START:
        case AC_TRACE_ITER_END:
            write_iter(state->file, &event->data.iter, pretty);
            break;
        case AC_TRACE_LLM_REQUEST:
            write_llm_request(state->file, &event->data.llm_request, pretty);
            break;
        case AC_TRACE_LLM_RESPONSE:
            write_llm_response(state->file, &event->data.llm_response, pretty);
            break;
        case AC_TRACE_TOOL_START:
            write_tool_start(state->file, &event->data.tool_start, pretty);
            break;
        case AC_TRACE_TOOL_END:
            write_tool_end(state->file, &event->data.tool_end, pretty);
            break;
    }

    write_newline(state->file, pretty);
    write_indent(state->file, 3, pretty);
    fputs("}", state->file);
    write_newline(state->file, pretty);

    write_indent(state->file, 2, pretty);
    fputs("}", state->file);

    if (event->type == AC_TRACE_AGENT_END) {
        write_newline(state->file, pretty);
        write_indent(state->file, 1, pretty);
        fputs("]", state->file);
        write_newline(state->file, pretty);
        fputs("}", state->file);
        write_newline(state->file, pretty);
        fclose(state->file);
        state->file = NULL;
    } else if (state->config.flush_after_event) {
        fflush(state->file);
    }
}

/*============================================================================
 * Public API
 *============================================================================*/

int ac_trace_json_exporter_init(const ac_trace_json_config_t *config) {
    memset(&s_state, 0, sizeof(s_state));

    if (config) {
        s_state.config = *config;
        if (!s_state.config.output_dir) {
            s_state.config.output_dir = AC_TRACE_JSON_DEFAULT_DIR;
        }
    } else {
        s_state.config.output_dir = AC_TRACE_JSON_DEFAULT_DIR;
        s_state.config.pretty_print = AC_TRACE_JSON_DEFAULT_PRETTY;
        s_state.config.include_timestamps = AC_TRACE_JSON_DEFAULT_TIMESTAMPS;
        s_state.config.flush_after_event = AC_TRACE_JSON_DEFAULT_FLUSH;
    }

    if (ensure_dir(s_state.config.output_dir) != 0) {
        fprintf(stderr, "[TRACE] Failed to create directory: %s\n",
                s_state.config.output_dir);
        return -1;
    }

    /* Enable tracing with our handler */
    ac_trace_enable(json_trace_handler, NULL);

    s_state.initialized = 1;

    return 0;
}

void ac_trace_json_exporter_cleanup(void) {
    if (s_state.file) {
        int pretty = s_state.config.pretty_print;
        write_newline(s_state.file, pretty);
        write_indent(s_state.file, 1, pretty);
        fputs("]", s_state.file);
        write_newline(s_state.file, pretty);
        fputs("}", s_state.file);
        write_newline(s_state.file, pretty);
        fclose(s_state.file);
        s_state.file = NULL;
    }

    ac_trace_disable();

    memset(&s_state, 0, sizeof(s_state));
}

const char *ac_trace_json_exporter_get_path(void) {
    if (s_state.current_path[0]) {
        return s_state.current_path;
    }
    return NULL;
}

/*============================================================================
 * Console Exporter
 *============================================================================*/

static ac_trace_console_config_t s_console_config = {0};

#define ANSI_RESET   "\033[0m"
#define ANSI_BOLD    "\033[1m"
#define ANSI_DIM     "\033[2m"
#define ANSI_CYAN    "\033[36m"
#define ANSI_GREEN   "\033[32m"
#define ANSI_YELLOW  "\033[33m"
#define ANSI_BLUE    "\033[34m"
#define ANSI_MAGENTA "\033[35m"

static const char *get_event_color(ac_trace_event_type_t type) {
    switch (type) {
        case AC_TRACE_AGENT_START:
        case AC_TRACE_AGENT_END:
            return ANSI_BOLD ANSI_GREEN;
        case AC_TRACE_ITER_START:
        case AC_TRACE_ITER_END:
            return ANSI_CYAN;
        case AC_TRACE_LLM_REQUEST:
        case AC_TRACE_LLM_RESPONSE:
            return ANSI_BLUE;
        case AC_TRACE_TOOL_START:
        case AC_TRACE_TOOL_END:
            return ANSI_MAGENTA;
        default:
            return "";
    }
}

static void console_trace_handler(const ac_trace_event_t *event, void *user_data) {
    (void)user_data;

    if (!event) return;

    int color = s_console_config.colorized;
    const char *type_name = ac_trace_event_name(event->type);

    if (color) {
        fprintf(stderr, "%s[TRACE]%s %s%-18s%s | ",
                ANSI_DIM, ANSI_RESET,
                get_event_color(event->type), type_name, ANSI_RESET);
    } else {
        fprintf(stderr, "[TRACE] %-18s | ", type_name);
    }

    switch (event->type) {
        case AC_TRACE_AGENT_START:
            fprintf(stderr, "Agent: %s | Message: %.50s%s",
                    event->agent_name ? event->agent_name : "unnamed",
                    event->data.agent_start.message ? event->data.agent_start.message : "",
                    (event->data.agent_start.message && strlen(event->data.agent_start.message) > 50) ? "..." : "");
            break;

        case AC_TRACE_AGENT_END:
            fprintf(stderr, "Iterations: %d | Tokens: %d | %.50s%s | %llums",
                    event->data.agent_end.iterations,
                    event->data.agent_end.total_prompt_tokens + event->data.agent_end.total_completion_tokens,
                    event->data.agent_end.content ? event->data.agent_end.content : "",
                    (event->data.agent_end.content && strlen(event->data.agent_end.content) > 50) ? "..." : "",
                    (unsigned long long)event->data.agent_end.duration_ms);
            break;

        case AC_TRACE_ITER_START:
        case AC_TRACE_ITER_END:
            fprintf(stderr, "Iteration: %d/%d",
                    event->data.iter.iteration,
                    event->data.iter.max_iterations);
            break;

        case AC_TRACE_LLM_REQUEST:
            fprintf(stderr, "Model: %s | Messages: %zu | Tools: %s",
                    event->data.llm_request.model ? event->data.llm_request.model : "?",
                    event->data.llm_request.message_count,
                    event->data.llm_request.tools_json ? "yes" : "no");
            break;

        case AC_TRACE_LLM_RESPONSE:
            fprintf(stderr, "Tokens: %d (%d + %d) | %s | %llums",
                    event->data.llm_response.total_tokens,
                    event->data.llm_response.prompt_tokens,
                    event->data.llm_response.completion_tokens,
                    event->data.llm_response.finish_reason ? event->data.llm_response.finish_reason : "?",
                    (unsigned long long)event->data.llm_response.duration_ms);
            break;

        case AC_TRACE_TOOL_START:
            fprintf(stderr, "%s(%.60s%s)",
                    event->data.tool_start.name ? event->data.tool_start.name : "?",
                    event->data.tool_start.arguments ? event->data.tool_start.arguments : "{}",
                    (event->data.tool_start.arguments && strlen(event->data.tool_start.arguments) > 60) ? "..." : "");
            break;

        case AC_TRACE_TOOL_END:
            fprintf(stderr, "%s -> %.60s%s (%llums)",
                    event->data.tool_end.name ? event->data.tool_end.name : "?",
                    event->data.tool_end.result ? event->data.tool_end.result : "null",
                    (event->data.tool_end.result && strlen(event->data.tool_end.result) > 60) ? "..." : "",
                    (unsigned long long)event->data.tool_end.duration_ms);
            break;
    }

    fprintf(stderr, "\n");
}

int ac_trace_console_exporter_init(const ac_trace_console_config_t *config) {
    if (config) {
        s_console_config = *config;
    } else {
        s_console_config.colorized = 1;
        s_console_config.compact = 0;
        s_console_config.show_json_data = 0;
    }

    ac_trace_enable(console_trace_handler, NULL);

    return 0;
}

void ac_trace_console_exporter_cleanup(void) {
    ac_trace_disable();
    memset(&s_console_config, 0, sizeof(s_console_config));
}
