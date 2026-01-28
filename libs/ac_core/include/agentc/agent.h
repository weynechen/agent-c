/**
 * @file agent.h
 * @brief AgentC Agent API
 *
 * Provides high-level agent interface with automatic memory management.
 * Agents are created within sessions and use arena allocation internally.
 */

#ifndef AGENTC_AGENT_H
#define AGENTC_AGENT_H

#include "error.h"
#include "session.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Forward Declarations
 *============================================================================*/

typedef struct ac_agent ac_agent_t;

/*============================================================================
 * Forward Declarations - LLM Parameters
 *============================================================================*/

/* Import LLM parameters type */
#include "llm.h"

/*============================================================================
 * Agent Result
 *============================================================================*/

typedef struct {
    const char* content;            /* Response content (owned by agent's arena) */
} ac_agent_result_t;

/*============================================================================
 * Tool Entry (from MOC-generated code)
 *============================================================================*/

#ifndef AC_TOOL_ENTRY_DEFINED
#define AC_TOOL_ENTRY_DEFINED

/**
 * @brief Wrapper function signature for tool execution
 *
 * @param json_args  JSON string containing function arguments
 * @return JSON string result (caller must free)
 */
typedef char* (*ac_tool_wrapper_t)(const char* json_args);

/**
 * @brief Tool registry entry (compatible with MOC-generated G_TOOL_TABLE)
 */
typedef struct {
    const char* name;           /**< Function name */
    const char* schema;         /**< JSON Schema for parameters */
    ac_tool_wrapper_t wrapper;  /**< Wrapper function pointer */
} ac_tool_entry_t;

#endif /* AC_TOOL_ENTRY_DEFINED */

/*============================================================================
 * Tool Selection Macros
 *============================================================================*/

/**
 * @brief Stringify a single function name to tool name string
 *
 * Usage:
 * @code
 * const char* tool = AC_TOOL(get_weather);  // -> "get_weather"
 * @endcode
 */
#define AC_TOOL(func) #func

/**
 * @brief Create a NULL-terminated tool name array
 *
 * Usage (preferred - explicit strings):
 * @code
 * .tools = (const char*[]){ "get_weather", "calculator", NULL }
 * @endcode
 *
 * Or using AC_TOOL macro for each function:
 * @code
 * .tools = (const char*[]){ 
 *     AC_TOOL(get_weather), 
 *     AC_TOOL(calculator), 
 *     NULL 
 * }
 * @endcode
 *
 * Or using the convenience macro for up to 8 tools:
 * @code
 * .tools = AC_TOOLS(get_weather, calculator)
 * @endcode
 */

/* Helper macros for counting arguments */
#define AC_NARG(...) AC_NARG_(__VA_ARGS__, AC_RSEQ_N())
#define AC_NARG_(...) AC_ARG_N(__VA_ARGS__)
#define AC_ARG_N(_1,_2,_3,_4,_5,_6,_7,_8,N,...) N
#define AC_RSEQ_N() 8,7,6,5,4,3,2,1,0

/* AC_TOOLS macro - supports 1 to 8 tools */
#define AC_TOOLS(...) AC_TOOLS_N(AC_NARG(__VA_ARGS__), __VA_ARGS__)
#define AC_TOOLS_N(N, ...) AC_TOOLS_IMPL(N, __VA_ARGS__)
#define AC_TOOLS_IMPL(N, ...) AC_TOOLS_##N(__VA_ARGS__)

#define AC_TOOLS_1(a) ((const char*[]){#a, NULL})
#define AC_TOOLS_2(a,b) ((const char*[]){#a, #b, NULL})
#define AC_TOOLS_3(a,b,c) ((const char*[]){#a, #b, #c, NULL})
#define AC_TOOLS_4(a,b,c,d) ((const char*[]){#a, #b, #c, #d, NULL})
#define AC_TOOLS_5(a,b,c,d,e) ((const char*[]){#a, #b, #c, #d, #e, NULL})
#define AC_TOOLS_6(a,b,c,d,e,f) ((const char*[]){#a, #b, #c, #d, #e, #f, NULL})
#define AC_TOOLS_7(a,b,c,d,e,f,g) ((const char*[]){#a, #b, #c, #d, #e, #f, #g, NULL})
#define AC_TOOLS_8(a,b,c,d,e,f,g,h) ((const char*[]){#a, #b, #c, #d, #e, #f, #g, #h, NULL})

/*============================================================================
 * Agent Configuration
 *============================================================================*/

typedef struct {
    const char* name;               /* Agent name (optional) */
    const char* instructions;       /* Agent instructions (optional) */
    ac_llm_params_t llm_params;     /* LLM configuration */
    
    /* Tool configuration - use one of the following: */
    const char** tools;             /* Tool names array (NULL-terminated), use AC_TOOLS() macro */
    const ac_tool_entry_t* tool_table;  /* Global tool table (MOC-generated G_TOOL_TABLE) */
    
    int max_iterations;             /* Max ReACT loops (default: 10) */
} ac_agent_params_t;

/*============================================================================
 * Agent API
 *============================================================================*/

/**
 * @brief Create an agent within a session
 *
 * Creates an agent with its own arena allocator. The agent automatically
 * creates LLM, tools, and memory managers using the arena.
 *
 * Example:
 * @code
 * #include "tools_gen.h"  // MOC-generated header
 * 
 * ac_session_t* session = ac_session_open();
 * 
 * ac_agent_t* agent = ac_agent_create(session, &(ac_agent_params_t){
 *     .name = "My Agent",
 *     .instructions = "You are a helpful assistant",
 *     .llm_params = {
 *         .model = "gpt-4o-mini",
 *         .api_key = getenv("OPENAI_API_KEY"),
 *     },
 *     .tools = AC_TOOLS(get_weather, calculator),  // Select tools by name
 *     .tool_table = G_TOOL_TABLE,                  // MOC-generated table
 *     .max_iterations = 10
 * });
 * 
 * // Use agent...
 * ac_agent_result_t* result = ac_agent_run_sync(agent, "What's the weather?");
 * printf("%s\n", result->content);
 * 
 * // Close session (automatically destroys agent)
 * ac_session_close(session);
 * @endcode
 *
 * @param session  Session handle
 * @param params   Agent configuration
 * @return Agent handle, NULL on error
 */
ac_agent_t* ac_agent_create(ac_session_t* session, const ac_agent_params_t* params);

/**
 * @brief Run agent synchronously
 *
 * Executes the agent with the given message and returns the result.
 * The result is allocated from the agent's arena and remains valid
 * until the agent is destroyed.
 *
 * @param agent    Agent handle
 * @param message  User message
 * @return Result (owned by agent's arena), NULL on error
 */
ac_agent_result_t* ac_agent_run_sync(ac_agent_t* agent, const char* message);

/**
 * @brief Destroy an agent
 *
 * Destroys the agent and frees its arena.
 * Note: Normally you don't need to call this directly - agents are
 * automatically destroyed when their session is closed.
 *
 * @param agent  Agent handle
 */
void ac_agent_destroy(ac_agent_t* agent);

/*============================================================================
 * Default Values
 *============================================================================*/

#define AC_AGENT_DEFAULT_MAX_ITERATIONS  10

#ifdef __cplusplus
}
#endif

#endif /* AGENTC_AGENT_H */
