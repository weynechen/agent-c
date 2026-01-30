# ArC 架构设计 v2

## 1. 设计目标

### 1.1 核心原则

- **可移植性优先**：核心库基于C99标准和POSIX接口，可移植到任何支持POSIX的平台
- **清晰的职责分离**：核心功能与平台特性严格分离
- **用户视角设计**：目录结构和命名直观反映使用场景
- **最小依赖**：嵌入式用户只需要核心组件，零外部依赖
- **渐进增强**：根据平台能力选择性启用特性

### 1.2 目标平台

- **Hosted环境**：Linux、Windows、macOS、服务器（需要完整操作系统）
- **Embedded环境**：FreeRTOS、裸机、RTOS（资源受限环境）

## 2. 整体架构

### 2.1 分层架构

```
┌─────────────────────────────────────────────────┐
│           Application Layer                                                                         │
│     (用户应用、Examples)                                                                       │
├─────────────────────────────────────────────────┤
│      Hosted Components Layer (可选)                                                   │
│  - dotenv, args, markdown, unicode                                                    │
│  - 需要完整OS支持                                                                                 │
├─────────────────────────────────────────────────┤
│           Core Layer (必需)                                                                         │
│  - agent, llm, tool, memory                                                                    │
│  - C99 + POSIX                                                                                         │
│  - 平台无关的业务逻辑                                                                           │
├─────────────────────────────────────────────────┤
│       Porting Layer (平台适配)                                                                │
│  - POSIX实现或POSIX shim                                                                  │
│  - Linux: 原生pthread                                                                          │
│  - Windows: pthread-win32                                                                 │
│  - FreeRTOS: FreeRTOS+POSIX                                                              │
└─────────────────────────────────────────────────┘
```

### 2.2 目录结构

```
agent-c/
├── arc_core/              # 核心库（所有平台必需）
│   ├── include/arc/       # 公共API头文件
│   │   ├── agent.h
│   │   ├── llm.h
│   │   ├── tool.h
│   │   ├── memory.h
│   │   ├── http_client.h
│   │   └── log.h            # 日志接口
│   ├── src/                  # 核心实现（C99+POSIX）
│   │   ├── agent.c
│   │   ├── llm/             # LLM组件
│   │   │   ├── llm.c        # LLM主接口
│   │   │   ├── openai_api.c # OpenAI兼容API
│   │   │   ├── anthropic_api.c # Anthropic API
│   │   │   └── providers/   # 扩展provider目录
│   │   ├── tool.c
│   │   ├── memory.c
│   │   ├── http_client.c
│   │   ├── log.c            # 日志核心实现
│   │   └── cjson.c          # 内置JSON库
│   └── port/                 # 移植接口
│       ├── README.md         # 移植指南
│       ├── arc_port.h     # 移植接口定义
│       ├── posix/            # Linux/macOS原生实现
│       │   └── log_posix.c  # POSIX日志实现
│       ├── windows/          # Windows POSIX shim
│       │   └── log_windows.c # Windows日志实现
│       └── freertos/         # FreeRTOS POSIX实现
│           └── log_freertos.c # FreeRTOS日志实现
│
├── arc_hosted/            # 完整OS组件（可选）
│   ├── include/arc/
│   │   ├── dotenv.h          # 环境变量加载
│   │   ├── args.h            # 命令行参数解析
│   │   ├── markdown.h        # Markdown渲染
│   │   └── unicode.h         # Unicode支持
│   ├── src/
│   │   ├── dotenv.c
│   │   ├── args.c
│   │   ├── markdown/         # Markdown渲染实现
│   │   └── unicode.c
│   └── components/           # Hosted特性的依赖组件
│       └── pcre2/            # 正则表达式库
│
├── tools/                    # 开发时工具
│   ├── moc/                  # Meta-Object Compiler
│   │   ├── src/
│   │   ├── include/
│   │   ├── tests/
│   │   ├── CMakeLists.txt
│   │   └── README.md
│   └── scripts/              # 辅助脚本
│
├── examples/                 # 示例程序
│   ├── minimal/              # 最小示例（仅core）
│   ├── hosted/               # 完整示例（桌面/服务器）
│   └── embedded/             # 嵌入式示例
│
├── docs/                     # 文档
│   ├── design/               # 设计文档
│   ├── porting.md            # 移植指南
│   ├── tools.md              # 工具使用说明
│   └── components.md         # 组件说明
│
├── tests/                    # 单元测试
│   ├── core/
│   └── hosted/
│
└── CMakeLists.txt            # 根构建配置
```

## 3. 核心组件 (arc_core)

### 3.1 组件职责

核心组件是整个框架的基础，遵循以下原则：
- 仅依赖C99标准库和POSIX接口
- 不包含平台特定代码
- 内存管理由框架负责
- 所有组件可在任何POSIX兼容平台运行

### 3.2 LLM 组件

**职责**：封装不同LLM提供商的API，提供统一接口

**接口定义**：
```c
typedef struct {
    const char* model;
    const char* api_key;
    const char* api_base;
    const char* instructions;
    
    float temperature;
    int max_tokens;
    // ...
} ac_llm_config_t;

typedef struct ac_llm ac_llm_t;

ac_llm_t* ac_llm_create(const ac_llm_config_t* config);
void ac_llm_destroy(ac_llm_t* llm);
```

**Provider架构**：
- **内置Provider**: `openai_api.c`、`anthropic_api.c` - 主流API协议实现
- **扩展Provider**: `providers/` 目录 - 用户可添加自定义provider
- **自动识别**: 通过model名称前缀或api_base自动选择对应provider
- **兼容性**: 大多数厂商兼容OpenAI API格式，通过配置api_base即可使用

**目录结构**：
```
arc_core/src/llm/
├── llm.c                # LLM主接口和provider路由
├── openai_api.c         # OpenAI兼容API (OpenAI, DeepSeek, 通义千问等)
├── anthropic_api.c      # Anthropic Claude API
└── providers/           # 扩展provider目录（预留）
    └── README.md        # Provider开发指南
```

### 3.3 Tool 组件

**职责**：工具管理、注册、调用和JSON Schema生成

**设计思路**：
- 使用`@arc_tool`注释标记工具函数
- MOC工具扫描并生成注册代码
- 支持工具分组（tools_group）
- 自动生成JSON Schema

**接口定义**：
```c
typedef char* (*ac_tool_func_t)(const char* args_json);

typedef struct {
    const char* name;
    const char* description;
    const char* json_schema;
    ac_tool_func_t func;
} ac_tool_t;

typedef struct ac_tool_registry ac_tool_registry_t;

ac_tool_registry_t* ac_tool_registry_create(void);
int ac_tool_register(ac_tool_registry_t* registry, const ac_tool_t* tool);
const ac_tool_t* ac_tool_find(ac_tool_registry_t* registry, const char* name);
```

### 3.4 Memory 组件

**职责**：对话历史管理

**两种内存模式**：
- **会话内存**：存储在内存中，session结束后释放
- **持久内存**：存储在文件系统（预留接口，暂不实现）

**接口定义**：
```c
typedef enum {
    AC_MSG_SYSTEM,
    AC_MSG_USER,
    AC_MSG_ASSISTANT,
    AC_MSG_TOOL_CALL,
    AC_MSG_TOOL_RESULT
} ac_message_role_t;

typedef struct {
    ac_message_role_t role;
    const char* content;
} ac_message_t;

typedef struct ac_memory ac_memory_t;

ac_memory_t* ac_memory_create(const char* session_id);
int ac_memory_add_message(ac_memory_t* mem, const ac_message_t* msg);
const ac_message_t* ac_memory_get_messages(ac_memory_t* mem, int* count);
void ac_memory_clear(ac_memory_t* mem);
void ac_memory_destroy(ac_memory_t* mem);
```

### 3.5 Agent 组件

**职责**：整合LLM、Tool、Memory，提供Agent运行框架

**特性**：
- 支持同步和流式调用
- 内置ReACT循环（推理-行动循环）
- 生产者-消费者线程模型
- 超时和最大迭代次数控制

**接口定义**：
```c
typedef struct {
    const char* name;
    ac_llm_t* llm;
    ac_tool_registry_t* tools;
    ac_memory_t* memory;
    int max_iterations;
    uint32_t timeout_ms;
} ac_agent_config_t;

typedef struct ac_agent ac_agent_t;

ac_agent_t* ac_agent_create(const ac_agent_config_t* config);

// Synchronous call
char* ac_agent_run_sync(ac_agent_t* agent, const char* input);

// Streaming call
typedef struct ac_stream ac_stream_t;
ac_stream_t* ac_agent_run_stream(ac_agent_t* agent, const char* input);
int ac_stream_next(ac_stream_t* stream, char* buffer, size_t size, uint32_t timeout_ms);
void ac_stream_close(ac_stream_t* stream);

void ac_agent_destroy(ac_agent_t* agent);
```

### 3.6 HTTP Client 组件

**职责**：提供HTTP/HTTPS通信能力

**后端选择**：
- Hosted平台：libcurl
- Embedded平台：FreeRTOS+TCP、lwIP或自定义

**接口定义**：
```c
typedef struct {
    const char* name;
    const char* value;
    struct arc_http_header* next;
} arc_http_header_t;

typedef struct {
    int status_code;
    arc_http_header_t* headers;
    char* body;
    size_t body_size;
    char* error_msg;
} arc_http_response_t;

typedef struct arc_http_client arc_http_client_t;

arc_http_client_t* arc_http_client_create(void);
arc_http_response_t* arc_http_post(
    arc_http_client_t* client,
    const char* url,
    const arc_http_header_t* headers,
    const char* body,
    size_t body_size
);
void arc_http_response_free(arc_http_response_t* response);
void arc_http_client_destroy(arc_http_client_t* client);
```

### 3.7 Log 组件

**职责**：提供统一的日志接口，支持不同平台的日志输出

**设计原则**：
- **Core层**: 提供基础的日志接口和宏定义
- **Port层**: 实现平台相关的日志输出（串口、文件、syslog等）
- **Hosted/应用层**: 可扩展高级特性（日志轮转、远程日志、结构化日志）

**接口定义**：
```c
// Log levels
typedef enum {
    AC_LOG_LEVEL_OFF = 0,
    AC_LOG_LEVEL_ERROR = 1,
    AC_LOG_LEVEL_WARN = 2,
    AC_LOG_LEVEL_INFO = 3,
    AC_LOG_LEVEL_DEBUG = 4
} ac_log_level_t;

// Log handler function type (implemented in port layer)
typedef void (*ac_log_handler_t)(ac_log_level_t level, 
                                  const char* file, 
                                  int line, 
                                  const char* func,
                                  const char* fmt, 
                                  va_list args);

// Core API
void ac_log_set_level(ac_log_level_t level);
ac_log_level_t ac_log_get_level(void);
void ac_log_set_handler(ac_log_handler_t handler);

// Logging macros
#define AC_LOG_ERROR(fmt, ...) ac_log_error(__FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#define AC_LOG_WARN(fmt, ...)  ac_log_warn(__FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#define AC_LOG_INFO(fmt, ...)  ac_log_info(__FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#define AC_LOG_DEBUG(fmt, ...) ac_log_debug(__FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
```

**平台实现**：
- **POSIX (Linux/macOS)**: 输出到stderr，支持颜色
- **Windows**: 输出到控制台，支持颜色
- **FreeRTOS/Embedded**: 输出到串口或自定义缓冲区

**扩展能力**：
用户可通过`ac_log_set_handler()`设置自定义日志处理函数，实现：
- 日志文件轮转
- 远程日志服务
- 结构化日志（JSON）
- 日志过滤和聚合

## 4. 移植层 (Port Layer)

### 4.1 设计原则

- **优先使用POSIX标准**：不自己创造抽象层，使用成熟的POSIX接口
- **最小移植接口**：仅抽象必需的OS功能
- **平台检测**：自动检测平台并选择对应实现

### 4.2 移植接口

核心代码直接使用以下POSIX接口：

**线程接口** (pthread.h)：
```c
pthread_t
pthread_create()
pthread_join()
pthread_detach()
pthread_exit()
```

**同步原语** (pthread.h)：
```c
pthread_mutex_t
pthread_mutex_init/destroy/lock/unlock()

pthread_cond_t
pthread_cond_init/destroy/wait/timedwait/signal/broadcast()
```

**时间接口** (time.h)：
```c
struct timespec
clock_gettime()
nanosleep()
```

### 4.3 各平台实现策略

#### Linux/macOS
- 原生POSIX支持，无需适配层
- 直接链接pthread库

#### Windows
两个选择：
1. 使用pthread-win32库（推荐）
2. 实现POSIX shim层（`port/windows/pthread_win32.c`）

#### FreeRTOS
- 使用官方FreeRTOS+POSIX组件
- 映射pthread到FreeRTOS Task API
- 映射mutex到FreeRTOS Semaphore

### 4.4 移植检查清单

移植到新平台时，需要提供：

1. **POSIX线程支持**
   - 创建、join、detach线程
   - 互斥锁、条件变量

2. **HTTP通信后端**
   - libcurl（hosted）
   - 自定义实现（embedded）

3. **时间函数**
   - 获取当前时间
   - 休眠/延时

4. **内存分配**（可选）
   - 默认使用标准malloc/free
   - 可通过宏覆盖

## 5. Hosted组件 (arc_hosted)

### 5.1 设计目标

为运行在完整操作系统（Linux/Windows/macOS/Server）上的应用提供增强特性。

**特点**：
- 需要文件系统、进程、丰富的标准库
- 不适用于嵌入式平台
- 可选编译，不影响核心库

### 5.2 组件列表

#### dotenv - 环境变量管理
```c
int dotenv_load(const char* filepath);
```
从`.env`文件加载环境变量，简化配置管理。

#### args - 命令行参数解析
```c
typedef struct ac_args ac_args_t;

ac_args_t* ac_args_create(void);
int ac_args_add_option(ac_args_t* args, const char* name, const char* help);
int ac_args_parse(ac_args_t* args, int argc, char** argv);
const char* ac_args_get(ac_args_t* args, const char* name);
```
支持长短选项、帮助信息生成等。

#### markdown - Markdown渲染
```c
typedef struct ac_markdown_renderer ac_markdown_renderer_t;

ac_markdown_renderer_t* ac_markdown_renderer_create(void);
char* ac_markdown_render_terminal(
    ac_markdown_renderer_t* renderer, 
    const char* markdown
);
```
在终端中渲染带颜色、格式的Markdown文本。

#### unicode - Unicode支持
```c
size_t ac_utf8_length(const char* str);
int ac_utf8_validate(const char* str);
char* ac_utf8_substring(const char* str, size_t start, size_t len);
```
UTF-8字符串处理工具函数。

### 5.3 依赖组件

Hosted特性的第三方依赖统一放在`arc_hosted/components/`下：
- **pcre2**：正则表达式（Markdown渲染使用）
- 未来可能添加：readline、ncurses等

## 6. 开发工具 (tools/)

### 6.1 MOC - Meta-Object Compiler

**作用**：扫描`@arc_tool`注释，自动生成工具注册代码和JSON Schema

**工作流程**：
```
用户代码 (*.c)
    ↓
  [MOC扫描]
    ↓
生成代码 (*_generated.c/h)
    ↓
  [编译链接]
    ↓
  可执行文件
```

**用户代码示例**：
```c
/**
 * @arc_tool
 * @tools_group: weather
 * @description: Get current weather for a city
 */
char* get_weather(const char* city);
```

**MOC生成**：
- 工具描述结构体
- JSON Schema定义
- 自动注册函数
- 工具组管理

### 6.2 其他工具

未来可能添加：
- **schema_validator**：验证工具JSON Schema
- **config_generator**：生成配置模板
- **benchmark**：性能测试工具

## 7. 构建系统

### 7.1 构建配置

使用CMake，提供灵活的构建选项：

**构建Profile**：
- `minimal`：仅核心库，最小依赖
- `hosted`：核心库 + Hosted组件
- `embedded`：针对嵌入式优化

**构建选项**：
```cmake
ARC_PROFILE=<minimal|hosted|embedded>  # 构建配置文件
ARC_BUILD_TOOLS=<ON|OFF>               # 是否构建开发工具
ARC_BUILD_EXAMPLES=<ON|OFF>            # 是否构建示例
ARC_BUILD_TESTS=<ON|OFF>               # 是否构建测试
ARC_PORT=<posix|windows|freertos>      # 移植目标
```

### 7.2 平台检测

CMake自动检测平台并选择对应的移植实现：
- Linux/macOS → `port/posix/`
- Windows → `port/windows/`
- Generic → `port/freertos/`（或用户指定）

### 7.3 依赖管理

**核心库**：
- 零外部依赖（cJSON内置）
- 仅需POSIX兼容环境

**Hosted组件**：
- pcre2（内置在components/）
- 其他依赖通过CMake管理

## 8. 内存管理

### 8.1 管理原则

- **框架负责**：所有内存由框架分配和释放
- **用户无负担**：用户不需要手动管理内存
- **生命周期明确**：
  - Agent创建时分配
  - Agent销毁时全部释放
  - 过程中动态增长

### 8.2 内存抽象

支持自定义内存分配器（嵌入式需求）：
```c
#define ARC_MALLOC(size)    custom_malloc(size)
#define ARC_FREE(ptr)       custom_free(ptr)
#define ARC_REALLOC(p, s)   custom_realloc(p, s)
```

## 9. 线程模型

### 9.1 生产者-消费者模型

**目的**：解耦网络通信和业务逻辑，防止用户回调阻塞连接

**实现**：
- **生产者线程**：负责发送/接收LLM数据
- **消费者线程**：调用工具、执行用户回调
- **消息队列**：线程间通信

### 9.2 线程安全

- 核心API线程安全
- 用户回调在消费者线程执行
- 使用pthread mutex/cond保护共享状态

## 10. 错误处理

### 10.1 错误策略

当前策略：
- 错误时记录日志
- 返回错误码或NULL
- 不做自动重试（由用户决定）

### 10.2 日志管理

日志系统采用三层架构：

**1. Core层 (arc_core/src/log.c)**
- 提供基础日志接口和宏
- 管理日志级别
- 路由日志到平台实现

**2. Port层 (arc_core/port/*/log_*.c)**
- 实现平台相关的日志输出
- POSIX: 彩色输出到stderr
- Windows: 控制台彩色输出
- FreeRTOS: 串口输出

**3. 扩展层 (Hosted/应用层)**
- 用户可通过`ac_log_set_handler()`自定义日志处理
- 支持高级特性：日志轮转、远程日志、结构化日志等

**使用示例**：
```c
// Set log level
ac_log_set_level(AC_LOG_LEVEL_DEBUG);

// Use logging macros
AC_LOG_INFO("Agent initialized: %s", agent_name);
AC_LOG_ERROR("HTTP request failed: %d", status_code);

// Custom log handler (optional)
void my_log_handler(ac_log_level_t level, const char* file, 
                    int line, const char* func, 
                    const char* fmt, va_list args) {
    // Custom implementation: write to file, send to server, etc.
}
ac_log_set_handler(my_log_handler);
```

## 11. 使用场景

### 11.1 嵌入式场景（最小配置）

**需要的组件**：
- `arc_core/src/` - 核心代码
- `arc_core/port/freertos/` - 移植层
- HTTP backend实现

**构建示例**：
```bash
cmake -B build \
    -DCMAKE_TOOLCHAIN_FILE=arm-none-eabi.cmake \
    -DARC_PROFILE=minimal \
    -DARC_PORT=freertos
```

### 11.2 桌面应用场景

**需要的组件**：
- `arc_core/` - 核心
- `arc_hosted/` - 增强特性（dotenv、args、markdown）

**构建示例**：
```bash
cmake -B build -DARC_PROFILE=hosted
cmake --build build
```

### 11.3 服务器场景

**需要的组件**：
- `arc_core/` - 核心
- `arc_hosted/` - 部分特性（dotenv、unicode）
- 可能需要添加：日志轮转、监控等

**构建示例**：
```bash
cmake -B build \
    -DARC_PROFILE=hosted \
    -DCMAKE_BUILD_TYPE=Release
```

## 12. 未来扩展

### 12.1 多Agent编排

参考LangGraph的图模型：
- 定义Agent DAG
- 支持条件分支
- 并行执行

### 12.2 持久内存

实现持久化Memory：
- SQLite存储
- 向量数据库集成（RAG）

### 12.3 更多平台

- Zephyr RTOS
- ESP-IDF
- STM32 HAL

## 13. 设计优势

### 13.1 对嵌入式用户

✅ **最小依赖**：只需要`arc_core/`  
✅ **清晰的移植接口**：`port/`目录一目了然  
✅ **内存可控**：支持自定义分配器  
✅ **资源占用小**：仅链接需要的组件  

### 13.2 对桌面/服务器用户

✅ **开箱即用**：CMake自动配置  
✅ **丰富特性**：dotenv、args、markdown等  
✅ **标准化**：基于POSIX，熟悉的API  
✅ **易于集成**：标准C库，无特殊要求  

### 13.3 对贡献者

✅ **职责清晰**：目录结构反映功能边界  
✅ **易于扩展**：添加新特性到`arc_hosted/`  
✅ **工具支持**：MOC自动化工具注册  
✅ **测试友好**：核心与平台分离，易于Mock  

## 14. 总结

ArC v2架构通过以下设计实现了可移植性和可用性的平衡：

1. **C99 + POSIX标准**：不自己创造抽象层，使用成熟标准
2. **清晰分层**：Core（必需）、Hosted（可选）、Port（适配）
3. **用户视角设计**：目录结构直观反映使用场景
4. **渐进增强**：从最小核心到完整特性，按需启用
5. **工具链支持**：MOC等工具简化开发流程

这个架构既能满足嵌入式用户的极简需求，也能为桌面和服务器用户提供丰富特性，同时保持代码的清晰性和可维护性。
