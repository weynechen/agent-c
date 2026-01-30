/**
 * @file trace_exporters.h
 * @brief ArC Trace Exporters for Hosted Environments
 *
 * Provides ready-to-use trace exporters for desktop/server environments.
 * Users can simply call ac_trace_json_exporter_init() to enable tracing
 * without writing custom handlers.
 *
 * Usage:
 * @code
 * #include <arc.h>
 * #include <arc/trace_exporters.h>
 *
 * int main() {
 *     // Initialize JSON file exporter with default settings
 *     // - Output directory: ./logs
 *     // - File naming: {agent_name}_{timestamp}.json
 *     ac_trace_json_exporter_init(NULL);
 *
 *     // Create and run agent...
 *     ac_session_t *session = ac_session_open();
 *     ac_agent_t *agent = ac_agent_create(session, &params);
 *     ac_agent_run(agent, "Hello");
 *
 *     // Cleanup
 *     ac_session_close(session);
 *     ac_trace_json_exporter_cleanup();
 * }
 * @endcode
 */

#ifndef ARC_TRACE_EXPORTERS_H
#define ARC_TRACE_EXPORTERS_H

#include "arc/trace.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * JSON File Exporter Configuration
 *============================================================================*/

/**
 * @brief JSON exporter configuration options
 */
typedef struct {
    const char *output_dir;      /**< Output directory (default: "logs") */
    int pretty_print;            /**< Pretty-print JSON (default: 1) */
    int include_timestamps;      /**< Include ISO timestamps (default: 1) */
    int flush_after_event;       /**< Flush file after each event (default: 0) */
} ac_trace_json_config_t;

/**
 * @brief Default configuration values
 */
#define AC_TRACE_JSON_DEFAULT_DIR       "logs"
#define AC_TRACE_JSON_DEFAULT_PRETTY    1
#define AC_TRACE_JSON_DEFAULT_TIMESTAMPS 1
#define AC_TRACE_JSON_DEFAULT_FLUSH     0

/*============================================================================
 * JSON File Exporter API
 *============================================================================*/

/**
 * @brief Initialize the JSON file exporter
 *
 * Sets up tracing to output JSON files with agent execution traces.
 * Each agent run creates a new file: {agent_name}_{YYYYMMDD_HHMMSS}.json
 *
 * @param config Configuration options (NULL for defaults)
 * @return 0 on success, -1 on error
 *
 * Example output file (logs/MyAgent_20260129_143052.json):
 * @code
 * {
 *   "trace_id": "tr_18d5a2b3c4d5e6f7_a1b2c3d4",
 *   "agent_name": "MyAgent",
 *   "start_time": "2026-01-29T14:30:52.123Z",
 *   "events": [
 *     {
 *       "type": "agent_start",
 *       "timestamp": "2026-01-29T14:30:52.123Z",
 *       "sequence": 1,
 *       "data": {
 *         "message": "Hello",
 *         "instructions": "You are a helpful assistant.",
 *         "max_iterations": 10,
 *         "tool_count": 5
 *       }
 *     },
 *     ...
 *   ]
 * }
 * @endcode
 */
int ac_trace_json_exporter_init(const ac_trace_json_config_t *config);

/**
 * @brief Cleanup the JSON file exporter
 *
 * Flushes any pending data and closes open files.
 * Should be called before program exit.
 */
void ac_trace_json_exporter_cleanup(void);

/**
 * @brief Get the current trace output file path
 *
 * Returns the path to the currently active trace file.
 * Returns NULL if no trace is in progress.
 *
 * @return File path (static buffer, valid until next call)
 */
const char *ac_trace_json_exporter_get_path(void);

/*============================================================================
 * Console Exporter API (for development/debugging)
 *============================================================================*/

/**
 * @brief Console exporter configuration
 */
typedef struct {
    int colorized;               /**< Use ANSI colors (default: 1) */
    int compact;                 /**< Compact output (default: 0) */
    int show_json_data;          /**< Show full JSON data (default: 0) */
} ac_trace_console_config_t;

/**
 * @brief Initialize the console exporter
 *
 * Outputs trace events to stderr with optional colors.
 * Useful for development and debugging.
 *
 * @param config Configuration options (NULL for defaults)
 * @return 0 on success, -1 on error
 *
 * Example output:
 * @code
 * [TRACE] agent_start      | Agent: MyAgent | Message: Hello
 * [TRACE] react_iter_start | Iteration: 1/10
 * [TRACE] llm_request      | Model: gpt-4o | Messages: 2
 * [TRACE] llm_response     | Tokens: 150 (50 + 100) | tool_calls
 * [TRACE] tool_call        | calculator({"a":1,"b":2})
 * [TRACE] tool_result      | calculator -> {"result":3} (5ms)
 * [TRACE] react_iter_end   | Iteration: 1/10
 * [TRACE] llm_request      | Model: gpt-4o | Messages: 4
 * [TRACE] llm_response     | Tokens: 80 (60 + 20) | stop
 * [TRACE] agent_end        | Result: 3 | Iterations: 2 | 1234ms
 * @endcode
 */
int ac_trace_console_exporter_init(const ac_trace_console_config_t *config);

/**
 * @brief Cleanup the console exporter
 */
void ac_trace_console_exporter_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* ARC_TRACE_EXPORTERS_H */
