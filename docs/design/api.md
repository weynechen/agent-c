# AgentC API 设计文档

## 设计理念

AgentC 采用**生产者-消费者模型**，提供简洁的流式 API：

- **后台线程**：自动接收 HTTP/SSE 流，不阻塞用户代码
- **事件队列**：解耦网络 I/O 和用户逻辑
- **主动拉取**：用户从结果流中持续获取事件，直到完成
- **零内存负担**：用户不需要管理内存，`agentc_stream_free()` 自动清理所有资源
- **事件分发由用户实现**：库只负责生产事件，用户自主选择如何处理（UI、存储、日志等）

### 架构图

```
┌────────────────────────────────────────────────────────┐
│  User Thread (主线程)                                   │
│  ┌──────────────────────────────────────┐              │
│  │ while (agentc_stream_next(...)) {    │              │
│  │   switch (event.type) {              │              │
│  │     case TEXT_DELTA:                 │              │
│  │       ui_display(event.data);        │  ← 可以慢    │
│  │       storage_save(event.data);      │              │
│  │       logger_log(event.data);        │              │
│  │   }                                  │              │
│  │ }                                    │              │
│  └──────────────────────────────────────┘              │
│                   ↑                                     │
│                   │ 事件队列 (1024 events)               │
│                   │                                     │
│  ┌────────────────┴─────────────────────┐              │
│  │ Background Thread (后台线程)          │              │
│  │ - 接收 HTTP/SSE 流                    │              │
│  │ - 解析事件                            │  ← 不阻塞    │
│  │ - 快速入队                            │              │
│  │ - 执行工具调用                        │              │
│  └──────────────────────────────────────┘              │
└────────────────────────────────────────────────────────┘
```

---

## 核心 API

### Agent 配置与创建

```c
/**
 * Initialize AgentC with automatic .env loading
 * @param dotenv_path Path to .env file directory (NULL for current dir)
 * @return AGENTC_OK on success
 */
agentc_err_t agentc_init_env(const char* dotenv_path);

/**
 * Agent configuration parameters
 */
typedef struct {
    // Required
    const char* model;                  // Model name
    const char* api_key;                // API key
    
    // Optional
    const char* name;                   // Agent name
    const char* instructions;           // System instructions
    const char* base_url;               // Custom endpoint (auto-detected if NULL)
    agentc_tools_t* tools;              // Tool registry (NULL if no tools)
    
    // LLM parameters
    float temperature;                  // Default: 0.7
    int max_tokens;                     // Default: 0 (no limit)
    int max_iterations;                 // Max ReACT loops, default: 10
    
    // Tool behavior
    const char* tool_choice;            // "auto", "none", "required"
    int parallel_tool_calls;            // Default: 1 (enabled)
    
    // Internal queue size
    size_t event_queue_size;            // Default: 1024
    
    // Timeout
    uint32_t timeout_ms;                // Default: 120000 (2 minutes)
} agentc_agent_params_t;

/**
 * Create an agent with named parameters
 * Uses C99 designated initializers for clean syntax
 */
#define agentc_agent(...) \
    agentc_agent_create(&(agentc_agent_params_t){ \
        .temperature = 0.7f, \
        .max_iterations = 10, \
        .tool_choice = "auto", \
        .parallel_tool_calls = 1, \
        .event_queue_size = 1024, \
        .timeout_ms = 120000, \
        __VA_ARGS__ \
    })

/**
 * Convenience macro to get env var
 */
#define AGENTC_ENV(name) getenv(name)

/**
 * Free agent
 */
void agentc_agent_free(agentc_agent_t* agent);
```

### 流式运行 API

```c
/**
 * Event types
 */
typedef enum {
    AGENTC_EVENT_TEXT_DELTA,           // Text chunk received
    AGENTC_EVENT_TOOL_CALL_START,      // Tool call requested
    AGENTC_EVENT_TOOL_CALL_RESULT,     // Tool execution result
    AGENTC_EVENT_ITERATION,            // ReACT iteration completed
    AGENTC_EVENT_DONE,                 // Generation complete
    AGENTC_EVENT_ERROR                 // Error occurred
} agentc_event_type_t;

/**
 * Event data
 * Note: All string pointers are valid only until next agentc_stream_next() call.
 *       If you need to keep the data, make a copy.
 */
typedef struct {
    agentc_event_type_t type;
    union {
        struct {
            const char* delta;         // Text chunk (not null-terminated, use len)
            size_t len;                // Chunk length
        } text;
        
        struct {
            const char* name;          // Tool name
            const char* args;          // Tool arguments (JSON string)
            const char* call_id;       // Tool call ID
        } tool_call;
        
        struct {
            const char* name;          // Tool name
            const char* result;        // Tool result
            const char* call_id;       // Tool call ID
        } tool_result;
        
        struct {
            int iteration;             // Current iteration number
            int max_iterations;        // Max iterations limit
        } iteration;
        
        struct {
            const char* output;        // Final complete output
            int total_tokens;          // Total tokens used
            int iterations;            // Total iterations executed
        } done;
        
        struct {
            agentc_err_t code;         // Error code
            const char* message;       // Error message
        } error;
    } data;
} agentc_event_t;

/**
 * Stream handle (opaque)
 */
typedef struct agentc_stream agentc_stream_t;

/**
 * Start agent run and return stream handle
 * 
 * This function:
 * - Creates an internal event queue
 * - Starts a background thread to handle HTTP/SSE streaming
 * - Returns immediately with a stream handle
 * 
 * @param agent Agent handle
 * @param input User input message
 * @return Stream handle (must call agentc_stream_free when done)
 */
agentc_stream_t* agentc_run(
    agentc_agent_t* agent,
    const char* input
);

/**
 * Get next event from stream (blocking)
 * 
 * This function:
 * - Blocks until next event is available
 * - Returns false when stream is complete (DONE or ERROR event)
 * - User can take as long as needed to process the event
 * - Background thread continues receiving data during processing
 * 
 * @param stream Stream handle
 * @param event Output event (pointer must be valid)
 * @return true if event received, false if stream ended
 */
bool agentc_stream_next(
    agentc_stream_t* stream,
    agentc_event_t* event
);

/**
 * Free stream and all associated resources
 * 
 * This function:
 * - Stops background thread if still running
 * - Frees event queue and all buffered events
 * - Releases all memory allocated during the run
 * - User does NOT need to free individual event data
 * 
 * @param stream Stream handle
 */
void agentc_stream_free(agentc_stream_t* stream);

/**
 * Cleanup AgentC runtime
 * Call before program exit to release global resources
 */
void agentc_cleanup(void);
```

---

## 使用示例

### 示例 1: 基础文本生成

```c
#include "agentc.h"
#include <stdio.h>

int main(void) {
    // Initialize
    agentc_init_env(".");
    
    // Create agent
    agentc_agent_t* agent = agentc_agent(
        .model = "deepseek/deepseek-chat",
        .api_key = AGENTC_ENV("DEEPSEEK_API_KEY"),
        .instructions = "You are a helpful assistant"
    );
    
    // Start run
    agentc_stream_t* stream = agentc_run(agent, "写一个秋天为主题的三行诗");
    
    // Process events
    agentc_event_t event;
    printf("Assistant: ");
    
    while (agentc_stream_next(stream, &event)) {
        switch (event.type) {
            case AGENTC_EVENT_TEXT_DELTA:
                // Print as text arrives
                fwrite(event.data.text.delta, 1, event.data.text.len, stdout);
                fflush(stdout);
                break;
                
            case AGENTC_EVENT_DONE:
                printf("\n[%d tokens, %d iterations]\n",
                       event.data.done.total_tokens,
                       event.data.done.iterations);
                break;
                
            case AGENTC_EVENT_ERROR:
                fprintf(stderr, "\nError: %s\n", event.data.error.message);
                break;
                
            default:
                break;
        }
    }
    
    // Cleanup (frees all resources)
    agentc_stream_free(stream);
    agentc_agent_free(agent);
    agentc_cleanup();
    
    return 0;
}
```

### 示例 2: 带工具调用

```c
#include "agentc.h"
#include "tools.gen.h"  // Auto-generated by agentc-moc

int main(void) {
    agentc_init_env(".");
    
    // Register tools
    agentc_tools_t* tools = agentc_tools_auto_register();
    
    // Create agent with tools
    agentc_agent_t* agent = agentc_agent(
        .model = "deepseek/deepseek-chat",
        .api_key = AGENTC_ENV("DEEPSEEK_API_KEY"),
        .instructions = "You are a helpful assistant. Use tools when needed.",
        .tools = tools,
        .max_iterations = 20
    );
    
    // Start run
    agentc_stream_t* stream = agentc_run(agent, "今天北京天气怎么样？");
    
    // Process events
    agentc_event_t event;
    printf("Assistant: ");
    
    while (agentc_stream_next(stream, &event)) {
        switch (event.type) {
            case AGENTC_EVENT_TEXT_DELTA:
                fwrite(event.data.text.delta, 1, event.data.text.len, stdout);
                fflush(stdout);
                break;
                
            case AGENTC_EVENT_TOOL_CALL_START:
                printf("\n🔧 Calling %s(%s)\n", 
                       event.data.tool_call.name,
                       event.data.tool_call.args);
                break;
                
            case AGENTC_EVENT_TOOL_CALL_RESULT:
                printf("📤 Result: %s\n", event.data.tool_result.result);
                break;
                
            case AGENTC_EVENT_ITERATION:
                printf("\n[Iteration %d/%d]\n",
                       event.data.iteration.iteration,
                       event.data.iteration.max_iterations);
                break;
                
            case AGENTC_EVENT_DONE:
                printf("\n✅ Done [%d tokens]\n", 
                       event.data.done.total_tokens);
                break;
                
            case AGENTC_EVENT_ERROR:
                fprintf(stderr, "\n❌ Error: %s\n", event.data.error.message);
                break;
        }
    }
    
    // Cleanup
    agentc_stream_free(stream);
    agentc_agent_free(agent);
    agentc_tools_free(tools);
    agentc_cleanup();
    
    return 0;
}
```

### 示例 3: 多订阅者模式（用户自己实现事件分发）

```c
#include "agentc.h"
#include <stdio.h>

// 用户自己的事件分发逻辑
typedef struct {
    FILE* log_file;
    char* accumulated_text;
    size_t text_len;
    size_t text_capacity;
} user_context_t;

void handle_text_for_ui(const char* delta, size_t len) {
    // 显示到 UI
    fwrite(delta, 1, len, stdout);
    fflush(stdout);
}

void handle_text_for_storage(user_context_t* ctx, const char* delta, size_t len) {
    // 累积到缓冲区，稍后存储
    if (ctx->text_len + len > ctx->text_capacity) {
        ctx->text_capacity = (ctx->text_len + len) * 2;
        ctx->accumulated_text = realloc(ctx->accumulated_text, ctx->text_capacity);
    }
    memcpy(ctx->accumulated_text + ctx->text_len, delta, len);
    ctx->text_len += len;
}

void handle_text_for_log(user_context_t* ctx, const char* delta, size_t len) {
    // 写入日志文件
    if (ctx->log_file) {
        fwrite(delta, 1, len, ctx->log_file);
        fflush(ctx->log_file);
    }
}

int main(void) {
    agentc_init_env(".");
    
    // 用户上下文
    user_context_t ctx = {
        .log_file = fopen("agent.log", "w"),
        .accumulated_text = malloc(4096),
        .text_len = 0,
        .text_capacity = 4096
    };
    
    agentc_agent_t* agent = agentc_agent(
        .model = "deepseek/deepseek-chat",
        .api_key = AGENTC_ENV("DEEPSEEK_API_KEY")
    );
    
    agentc_stream_t* stream = agentc_run(agent, "写一首诗");
    agentc_event_t event;
    
    printf("Assistant: ");
    
    while (agentc_stream_next(stream, &event)) {
        if (event.type == AGENTC_EVENT_TEXT_DELTA) {
            // 用户自己分发给多个订阅者
            handle_text_for_ui(event.data.text.delta, event.data.text.len);
            handle_text_for_storage(&ctx, event.data.text.delta, event.data.text.len);
            handle_text_for_log(&ctx, event.data.text.delta, event.data.text.len);
        } else if (event.type == AGENTC_EVENT_DONE) {
            printf("\n[Done]\n");
            
            // 保存到数据库
            save_to_database(ctx.accumulated_text, ctx.text_len);
        }
    }
    
    // Cleanup
    if (ctx.log_file) fclose(ctx.log_file);
    free(ctx.accumulated_text);
    
    agentc_stream_free(stream);
    agentc_agent_free(agent);
    agentc_cleanup();
    
    return 0;
}
```

### 示例 4: 带 Markdown 渲染

```c
#include "agentc.h"
#include "render/markdown/md.h"

int main(void) {
    agentc_init_env(".");
    
    agentc_agent_t* agent = agentc_agent(
        .model = "deepseek/deepseek-chat",
        .api_key = AGENTC_ENV("DEEPSEEK_API_KEY")
    );
    
    // Create markdown renderer
    md_stream_t* md_stream = md_stream_new();
    
    agentc_stream_t* stream = agentc_run(agent, "用 Markdown 格式介绍 C 语言");
    agentc_event_t event;
    
    printf("Assistant:\n");
    
    while (agentc_stream_next(stream, &event)) {
        switch (event.type) {
            case AGENTC_EVENT_TEXT_DELTA:
                // Feed to markdown renderer
                md_stream_feed(md_stream, 
                             event.data.text.delta,
                             event.data.text.len);
                break;
                
            case AGENTC_EVENT_DONE:
                md_stream_finish(md_stream);
                printf("\n[%d tokens]\n", event.data.done.total_tokens);
                break;
                
            case AGENTC_EVENT_ERROR:
                fprintf(stderr, "Error: %s\n", event.data.error.message);
                break;
                
            default:
                break;
        }
    }
    
    // Cleanup
    md_stream_free(md_stream);
    agentc_stream_free(stream);
    agentc_agent_free(agent);
    agentc_cleanup();
    
    return 0;
}
```

---

## 工具定义（Tool Definition）

### 使用 agentc-moc 自动生成工具注册代码

AgentC 提供类似 Qt MOC 的代码生成工具 `agentc-moc`，通过扫描 Doxygen 风格的注释标记，自动生成工具包装和注册代码。

### 工具定义规范

```c
/**
 * @agentc_tool TOOL_NAME
 * @description Tool description
 * @param PARAM_NAME TYPE Description (required|optional, [default=VALUE])
 * @param ...
 * @return TYPE
 */
RETURN_TYPE tool_TOOL_NAME(PARAMS...) {
    // Implementation
}
```

### 支持的类型

| 注解类型 | C 类型 | JSON Schema 类型 |
|---------|--------|-----------------|
| `string` | `const char*` | `"string"` |
| `int` | `int` | `"integer"` |
| `float` | `float` / `double` | `"number"` |
| `bool` | `int` (0/1) | `"boolean"` |

### 示例

```c
// tools.c
#include "agentc.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/**
 * @agentc_tool get_weather
 * @description Get the current weather for a city
 * @param city string City name (required)
 * @return string Weather description
 */
char* tool_get_weather(const char* city) {
    char* result = malloc(256);
    snprintf(result, 256, "The weather in %s is sunny with 25°C.", city);
    return result;
}

/**
 * @agentc_tool calculate
 * @description Perform arithmetic calculation
 * @param x float First number (required)
 * @param y float Second number (required)
 * @param op string Operation: add, sub, mul, div (required)
 * @return string Calculation result as string
 */
char* tool_calculate(float x, float y, const char* op) {
    char* result = malloc(128);
    float res = 0;
    
    if (strcmp(op, "add") == 0) res = x + y;
    else if (strcmp(op, "sub") == 0) res = x - y;
    else if (strcmp(op, "mul") == 0) res = x * y;
    else if (strcmp(op, "div") == 0) res = (y != 0) ? x / y : 0;
    
    snprintf(result, 128, "%.2f", res);
    return result;
}

/**
 * @agentc_tool search
 * @description Search the web
 * @param query string Search query (required)
 * @param limit int Maximum number of results (optional, default=10)
 * @return string Search results
 */
char* tool_search(const char* query, int limit) {
    char* result = malloc(512);
    snprintf(result, 512, "Found %d results for: %s", limit, query);
    return result;
}
```

### 生成的头文件（tools.gen.h）

```c
// tools.gen.h - Auto-generated by agentc-moc
#ifndef TOOLS_GEN_H
#define TOOLS_GEN_H

#include "agentc.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Auto-register all tools defined in tools.c
 * @return Tool registry (caller must free with agentc_tools_free)
 */
agentc_tools_t* agentc_tools_auto_register(void);

#ifdef __cplusplus
}
#endif

#endif /* TOOLS_GEN_H */
```

`agentc-moc` 会自动生成对应的 `tools.gen.c`，包含：
- 参数解析包装函数
- JSON schema 生成
- 工具注册函数

详细的生成代码示例见附录。

---

## 内部实现机制

### 线程模型

```c
// 简化的内部实现示意

typedef struct {
    // Background thread
    pthread_t thread;
    volatile bool running;
    
    // Event queue
    event_queue_t* queue;  // Thread-safe FIFO queue
    
    // Agent context
    agentc_agent_t* agent;
    char* user_input;
    
    // Final result
    char* final_output;
    int total_tokens;
    int iterations;
    
    // Error state
    agentc_err_t error_code;
    char* error_message;
} agentc_stream_t;

// Background thread function
void* stream_thread_func(void* arg) {
    agentc_stream_t* stream = (agentc_stream_t*)arg;
    
    // Execute ReACT loop
    while (stream->running && stream->iterations < stream->agent->max_iterations) {
        // Call LLM API (streaming)
        curl_easy_perform(...);  // In curl write callback:
                                  // -> Parse SSE
                                  // -> Create event
                                  // -> event_queue_push(stream->queue, event)
        
        // If tool calls, execute them
        if (has_tool_calls) {
            execute_tools(...);
            // Push TOOL_CALL_RESULT events
        }
        
        stream->iterations++;
    }
    
    // Push DONE event
    agentc_event_t done_event = { .type = AGENTC_EVENT_DONE, ... };
    event_queue_push(stream->queue, &done_event);
    
    // Mark queue as closed
    event_queue_close(stream->queue);
    
    return NULL;
}

// User calls this
bool agentc_stream_next(agentc_stream_t* stream, agentc_event_t* event) {
    // Blocking pop from queue
    return event_queue_pop(stream->queue, event);  // Returns false when closed
}

// User calls this
void agentc_stream_free(agentc_stream_t* stream) {
    // Stop thread
    stream->running = false;
    event_queue_close(stream->queue);
    pthread_join(stream->thread, NULL);
    
    // Free all resources
    event_queue_free(stream->queue);
    free(stream->final_output);
    free(stream->error_message);
    free(stream);
}
```

### 事件队列（线程安全）

```c
typedef struct {
    agentc_event_t* events;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t count;
    bool closed;
    
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} event_queue_t;

// Producer (background thread)
void event_queue_push(event_queue_t* q, const agentc_event_t* event) {
    pthread_mutex_lock(&q->mutex);
    
    // Wait if queue is full
    while (q->count == q->capacity && !q->closed) {
        pthread_cond_wait(&q->not_full, &q->mutex);
    }
    
    if (!q->closed) {
        // Deep copy event data
        q->events[q->tail] = deep_copy_event(event);
        q->tail = (q->tail + 1) % q->capacity;
        q->count++;
        pthread_cond_signal(&q->not_empty);
    }
    
    pthread_mutex_unlock(&q->mutex);
}

// Consumer (user thread)
bool event_queue_pop(event_queue_t* q, agentc_event_t* event) {
    pthread_mutex_lock(&q->mutex);
    
    // Wait if queue is empty
    while (q->count == 0 && !q->closed) {
        pthread_cond_wait(&q->not_empty, &q->mutex);
    }
    
    bool has_event = false;
    if (q->count > 0) {
        *event = q->events[q->head];
        q->head = (q->head + 1) % q->capacity;
        q->count--;
        pthread_cond_signal(&q->not_full);
        has_event = true;
    }
    
    pthread_mutex_unlock(&q->mutex);
    return has_event;
}
```

---

## 内存管理

### 用户不需要关心的内存

所有通过 `agentc_event_t` 传递给用户的数据，都由库管理：

```c
while (agentc_stream_next(stream, &event)) {
    // event.data.text.delta 指向库内部的缓冲区
    // 用户不需要 free
    // 但只在下一次 agentc_stream_next() 调用前有效
    
    if (event.type == AGENTC_EVENT_TEXT_DELTA) {
        // ✅ Good: 立即使用
        fwrite(event.data.text.delta, 1, event.data.text.len, stdout);
        
        // ✅ Good: 如果需要保存，复制一份
        char* saved = strndup(event.data.text.delta, event.data.text.len);
        
        // ❌ Bad: 存储指针，稍后使用（可能已失效）
        saved_ptr = event.data.text.delta;  // Dangling pointer!
    }
}

// 最后一次性清理所有资源
agentc_stream_free(stream);
```

### 用户需要管理的内存

只有显式分配的资源才需要用户释放：

```c
// 用户创建的
agentc_agent_t* agent = agentc_agent(...);
agentc_tools_t* tools = agentc_tools_auto_register();
agentc_stream_t* stream = agentc_run(agent, "...");

// 用户需要释放
agentc_stream_free(stream);     // 释放流和所有事件
agentc_agent_free(agent);       // 释放 agent
agentc_tools_free(tools);       // 释放工具
agentc_cleanup();               // 释放全局资源
```

---

## 错误处理

### 创建阶段错误

```c
agentc_agent_t* agent = agentc_agent(
    .model = "invalid-model",
    .api_key = NULL  // Missing API key
);

if (!agent) {
    fprintf(stderr, "Failed to create agent: %s\n", agentc_last_error());
    return 1;
}
```

### 运行时错误

```c
agentc_stream_t* stream = agentc_run(agent, "input");
if (!stream) {
    fprintf(stderr, "Failed to start run: %s\n", agentc_last_error());
    return 1;
}

agentc_event_t event;
while (agentc_stream_next(stream, &event)) {
    if (event.type == AGENTC_EVENT_ERROR) {
        // 运行时错误（网络、超时、模型错误等）
        fprintf(stderr, "Runtime error: %s (code: %d)\n",
                event.data.error.message,
                event.data.error.code);
        break;
    }
}

agentc_stream_free(stream);
```

---

## 环境变量配置

### .env 文件

```bash
# API Keys
DEEPSEEK_API_KEY=sk-xxx
OPENAI_API_KEY=sk-xxx
ANTHROPIC_API_KEY=sk-xxx

# Custom Endpoints (optional)
DEEPSEEK_BASE_URL=https://api.deepseek.com/v1
OPENAI_BASE_URL=https://api.openai.com/v1
ANTHROPIC_BASE_URL=https://api.anthropic.com/v1
```

### 模型名称推断

| 模型字符串 | 推断 API Key | 推断 Base URL |
|-----------|-------------|--------------|
| `"deepseek/deepseek-chat"` | `DEEPSEEK_API_KEY` | `DEEPSEEK_BASE_URL` 或默认 |
| `"openai/gpt-4"` | `OPENAI_API_KEY` | `OPENAI_BASE_URL` 或默认 |
| `"gpt-3.5-turbo"` | `OPENAI_API_KEY` | `OPENAI_BASE_URL` 或默认 |
| `"anthropic/claude-3"` | `ANTHROPIC_API_KEY` | `ANTHROPIC_BASE_URL` 或默认 |

自定义格式：`"provider/model"` → `{PROVIDER}_API_KEY` + `{PROVIDER}_BASE_URL`

---

## CMake 集成

### FindAgentC.cmake

```cmake
# cmake/FindAgentC.cmake

# Find agentc-moc executable
find_program(AGENTC_MOC_EXECUTABLE
    NAMES agentc-moc
    HINTS ${AGENTC_ROOT}/bin
    DOC "AgentC Meta-Object Compiler"
)

if(AGENTC_MOC_EXECUTABLE)
    set(AGENTC_MOC_FOUND TRUE)
    message(STATUS "Found agentc-moc: ${AGENTC_MOC_EXECUTABLE}")
else()
    set(AGENTC_MOC_FOUND FALSE)
    message(WARNING "agentc-moc not found. Tool auto-registration disabled.")
endif()

# Function to wrap tools with agentc-moc
function(agentc_wrap_tools target)
    if(NOT AGENTC_MOC_FOUND)
        message(WARNING "agentc_wrap_tools: agentc-moc not found, skipping")
        return()
    endif()
    
    set(sources ${ARGN})
    set(generated_files)
    
    foreach(src ${sources})
        get_filename_component(abs_src ${src} ABSOLUTE)
        get_filename_component(basename ${src} NAME_WE)
        
        set(gen_c "${CMAKE_CURRENT_BINARY_DIR}/${basename}.gen.c")
        set(gen_h "${CMAKE_CURRENT_BINARY_DIR}/${basename}.gen.h")
        
        add_custom_command(
            OUTPUT ${gen_c} ${gen_h}
            COMMAND ${AGENTC_MOC_EXECUTABLE}
                ${abs_src}
                --output-c ${gen_c}
                --output-h ${gen_h}
            DEPENDS ${abs_src} ${AGENTC_MOC_EXECUTABLE}
            COMMENT "Running agentc-moc on ${src}"
            VERBATIM
        )
        
        list(APPEND generated_files ${gen_c} ${gen_h})
    endforeach()
    
    # Add generated files to target
    target_sources(${target} PRIVATE ${generated_files})
    
    # Add binary dir to include path (for .gen.h files)
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
endfunction()
```

### 用户的 CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.15)
project(my_agent_app C)

# Find AgentC
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
find_package(AgentC REQUIRED)

# Create executable
add_executable(myapp
    main.c
    tools.c
)

# Auto-generate tool wrappers
agentc_wrap_tools(myapp tools.c)

# Link AgentC
target_link_libraries(myapp PRIVATE agentc pthread)

# Enable C99
set_property(TARGET myapp PROPERTY C_STANDARD 99)
```

---

## 设计原则

1. **简洁性**：单一流式 API，不分层
2. **零内存负担**：用户只需调用 `agentc_stream_free()`
3. **非阻塞**：后台线程处理 I/O，用户主动拉取
4. **用户控制分发**：事件如何处理（UI、存储、日志）由用户决定
5. **资源透明**：明确哪些资源需要释放
6. **线程安全**：内部队列和状态管理线程安全
7. **标准 C**：遵循 C99，依赖 pthread（跨平台）

---

## 性能考量

### 队列大小

默认队列大小为 1024 个事件，可以通过 `event_queue_size` 配置：

```c
agentc_agent_t* agent = agentc_agent(
    .model = "deepseek/deepseek-chat",
    .api_key = AGENTC_ENV("DEEPSEEK_API_KEY"),
    .event_queue_size = 2048  // 更大的缓冲
);
```

**建议：**
- 快速处理（< 10ms/event）：1024 足够
- 中速处理（10-100ms/event）：2048-4096
- 慢速处理（> 100ms/event）：考虑异步处理或增大到 8192

### TCP 缓冲区

即使用户处理慢，TCP 层还有 64KB-256KB 的缓冲，加上事件队列，可以容忍：

```
总缓冲 = TCP buffer (~128KB) + Event Queue (1024 events × ~100 bytes) ≈ 230KB
容忍延迟 ≈ 230KB / 100KB/s ≈ 2.3 秒
```

### 内存占用

```
每个 agentc_stream_t:
- Thread stack: ~8KB (系统默认)
- Event queue: event_queue_size × sizeof(agentc_event_t) ≈ 1024 × 100 bytes = 100KB
- Other state: ~10KB
Total: ~120KB per stream
```

---

## 附录：agentc-moc 命令行

```bash
# Basic usage
agentc-moc tools.c

# Custom output
agentc-moc tools.c --output-c tools.gen.c --output-h tools.gen.h

# Multiple files
agentc-moc tools1.c tools2.c --output-dir generated/

# Verbose mode
agentc-moc tools.c --verbose

# Dry run (show what would be generated)
agentc-moc tools.c --dry-run
```

---

## 附录：完整的生成代码示例

### tools.gen.c

```c
// tools.gen.c - Auto-generated by agentc-moc
#include "agentc.h"
#include <stdlib.h>
#include <string.h>

// Forward declarations
char* tool_get_weather(const char* city);
char* tool_calculate(float x, float y, const char* op);
char* tool_search(const char* query, int limit);

// Wrapper functions
static char* _wrap_get_weather(const cJSON* args, void* ctx) {
    (void)ctx;
    const char* city = cJSON_GetStringValue(cJSON_GetObjectItem(args, "city"));
    if (!city) return strdup("Error: missing required parameter 'city'");
    return tool_get_weather(city);
}

static char* _wrap_calculate(const cJSON* args, void* ctx) {
    (void)ctx;
    cJSON* x_obj = cJSON_GetObjectItem(args, "x");
    cJSON* y_obj = cJSON_GetObjectItem(args, "y");
    const char* op = cJSON_GetStringValue(cJSON_GetObjectItem(args, "op"));
    
    if (!x_obj || !y_obj || !op) {
        return strdup("Error: missing required parameters");
    }
    
    return tool_calculate(x_obj->valuedouble, y_obj->valuedouble, op);
}

static char* _wrap_search(const cJSON* args, void* ctx) {
    (void)ctx;
    const char* query = cJSON_GetStringValue(cJSON_GetObjectItem(args, "query"));
    if (!query) return strdup("Error: missing required parameter 'query'");
    
    cJSON* limit_obj = cJSON_GetObjectItem(args, "limit");
    int limit = limit_obj ? limit_obj->valueint : 10;
    
    return tool_search(query, limit);
}

// Auto-registration function
agentc_tools_t* agentc_tools_auto_register(void) {
    agentc_tools_t* tools = agentc_tools_new();
    
    agentc_tools_add(tools, &(agentc_tool_def_t){
        .name = "get_weather",
        .description = "Get the current weather for a city",
        .function = _wrap_get_weather,
        .params_json = 
            "{\"type\":\"object\","
            "\"properties\":{"
                "\"city\":{\"type\":\"string\",\"description\":\"City name\"}"
            "},"
            "\"required\":[\"city\"]}"
    });
    
    agentc_tools_add(tools, &(agentc_tool_def_t){
        .name = "calculate",
        .description = "Perform arithmetic calculation",
        .function = _wrap_calculate,
        .params_json = 
            "{\"type\":\"object\","
            "\"properties\":{"
                "\"x\":{\"type\":\"number\",\"description\":\"First number\"},"
                "\"y\":{\"type\":\"number\",\"description\":\"Second number\"},"
                "\"op\":{\"type\":\"string\",\"description\":\"Operation: add, sub, mul, div\"}"
            "},"
            "\"required\":[\"x\",\"y\",\"op\"]}"
    });
    
    agentc_tools_add(tools, &(agentc_tool_def_t){
        .name = "search",
        .description = "Search the web",
        .function = _wrap_search,
        .params_json = 
            "{\"type\":\"object\","
            "\"properties\":{"
                "\"query\":{\"type\":\"string\",\"description\":\"Search query\"},"
                "\"limit\":{\"type\":\"integer\",\"description\":\"Maximum number of results\",\"default\":10}"
            "},"
            "\"required\":[\"query\"]}"
    });
    
    return tools;
}
```

---

## 参考文献

- Qt Meta-Object System: https://doc.qt.io/qt-6/metaobjects.html
- OpenAI Agents SDK (Python): https://github.com/openai/swarm
- Producer-Consumer Pattern: https://en.wikipedia.org/wiki/Producer%E2%80%93consumer_problem
- POSIX Threads: https://pubs.opengroup.org/onlinepubs/9699919799/
- C99 Standard: ISO/IEC 9899:1999
