# Memory 模块设计

## 1. 概述

Memory 模块负责管理 Agent 的对话历史和上下文记忆。它是连接消息结构（`ac_message_t`）与 LLM Provider 之间的桥梁，提供统一的消息存储、检索和管理能力。

### 1.1 设计目标

- **会话内存管理**：存储当前会话的消息历史
- **Token 预算控制**：防止上下文超出模型限制
- **Provider 无关性**：Memory 不关心消息如何序列化，保持 Provider 的灵活性
- **嵌入式友好**：低内存占用，可配置的存储策略

### 1.2 核心概念

```
消息 (Message)     ：单条对话记录，包含角色、内容、工具调用等
记忆 (Memory)      ：消息的集合，提供存储和检索能力
上下文窗口 (Context)：可发送给 LLM 的消息子集，受 token 限制
```

## 2. 架构层次

```
┌─────────────────────────────────────────────────────────────────┐
│                      Application Layer                          │
│                   Agent / User Application                      │
├─────────────────────────────────────────────────────────────────┤
│                      Memory Module                              │
│   - 消息存储与检索                                                │
│   - Token 预算管理                                               │
│   - 截断策略                                                     │
│   - 提供 "可发送消息窗口" 的计算                                    │
├─────────────────────────────────────────────────────────────────┤
│                      Message Layer                              │
│   - ac_message_t 结构体定义                                      │
│   - 消息创建、追加、遍历                                           │
│   - Arena 内存管理                                               │
├─────────────────────────────────────────────────────────────────┤
│                 Message Serialization Layer                     │
│   - message_json.c (OpenAI 格式)                                │
│   - message_anthropic.c (Anthropic 格式，预留)                   │
│   - 单条消息 ←→ cJSON 转换                                       │
├─────────────────────────────────────────────────────────────────┤
│                      Provider Layer                             │
│   - 构建完整 API 请求体                                           │
│   - 调用 Memory 获取消息列表                                      │
│   - 使用对应的序列化层转换消息                                      │
│   - 处理 Provider 特定参数                                        │
└─────────────────────────────────────────────────────────────────┘
```

### 2.1 各层职责说明

| 层级 | 模块 | 职责 | 位置 |
|------|------|------|------|
| Memory | `memory.h/c` | 消息存储、检索、截断策略 | `src/memory/` |
| Message | `message.h/c` | 消息结构定义和基本操作 | `src/memory/` |
| Serialization | `message_json.h/c` | 消息序列化（OpenAI 格式） | `src/llm/message/` |
| Provider | `openai.c` 等 | 构建 API 请求，调用 HTTP | `src/llm/providers/` |

### 2.2 数据流

```
用户输入
    │
    ▼
┌─────────────┐
│   Agent     │  ac_memory_add(memory, user_message)
└─────────────┘
    │
    ▼
┌─────────────┐
│   Memory    │  存储消息，管理历史
└─────────────┘
    │
    │ ac_memory_get_messages_within_budget(max_tokens)
    ▼
┌─────────────┐
│   LLM       │  ac_llm_chat(llm, messages)
└─────────────┘
    │
    ▼
┌─────────────┐
│  Provider   │  遍历消息，调用 ac_message_to_json()
└─────────────┘
    │
    ▼
┌─────────────┐
│ HTTP Client │  发送请求
└─────────────┘
```

## 3. Memory 模块设计

### 3.1 数据结构

```c
/**
 * @brief Memory 配置
 */
typedef struct {
    const char *session_id;          /* 会话标识（可选） */
    size_t max_messages;             /* 最大消息数量（0 = 无限制） */
    size_t max_tokens;               /* 最大 token 数量（0 = 无限制） */
    size_t context_window;           /* 模型上下文窗口大小 */
    float reserve_ratio;             /* 预留给回复的比例（如 0.25） */
    ac_truncate_strategy_t strategy; /* 截断策略 */
    
    /* 持久存储（预留） */
    const char *db_path;
    int enable_persistence;
} ac_memory_config_t;

/**
 * @brief 截断策略
 */
typedef enum {
    AC_TRUNCATE_OLDEST,           /* 删除最早的消息（保留 system） */
    AC_TRUNCATE_SLIDING_WINDOW,   /* 滑动窗口（保留最近 N 条） */
    AC_TRUNCATE_SUMMARIZE         /* 压缩旧消息为摘要（预留） */
} ac_truncate_strategy_t;

/**
 * @brief Memory 内部结构
 */
struct ac_memory {
    arena_t *arena;                  /* 内存分配器 */
    ac_memory_config_t config;       /* 配置 */
    
    ac_message_t *messages;          /* 消息链表头 */
    ac_message_t *tail;              /* 消息链表尾（优化追加） */
    size_t count;                    /* 消息数量 */
    size_t estimated_tokens;         /* 估算的 token 总数 */
    
    ac_message_t *system_message;    /* 系统消息（始终保留） */
};
```

### 3.2 核心接口

```c
/*============================================================================
 * 生命周期管理
 *============================================================================*/

/**
 * @brief 创建 Memory 实例
 */
ac_memory_t *ac_memory_create(arena_t *arena, const ac_memory_config_t *config);

/**
 * @brief 销毁 Memory 实例
 */
void ac_memory_destroy(ac_memory_t *memory);

/*============================================================================
 * 消息管理
 *============================================================================*/

/**
 * @brief 添加消息到 Memory
 *
 * 添加后自动检查是否超出限制，必要时触发截断策略。
 */
arc_err_t ac_memory_add(ac_memory_t *memory, const ac_message_t *message);

/**
 * @brief 设置系统消息
 *
 * 系统消息始终保留，不受截断策略影响。
 */
arc_err_t ac_memory_set_system(ac_memory_t *memory, const char *content);

/**
 * @brief 获取所有消息
 *
 * 返回完整的消息链表，包括系统消息。
 */
const ac_message_t *ac_memory_get_messages(ac_memory_t *memory);

/**
 * @brief 获取 token 预算内的消息
 *
 * 根据 context_window 和 reserve_ratio 计算可用预算，
 * 返回不超过预算的消息子集。
 *
 * @param memory     Memory 实例
 * @param max_tokens 最大 token 数（0 = 使用配置的默认值）
 * @return 消息链表（从 system 开始，按时间顺序）
 */
const ac_message_t *ac_memory_get_messages_within_budget(
    ac_memory_t *memory, 
    size_t max_tokens
);

/**
 * @brief 获取消息数量
 */
size_t ac_memory_count(ac_memory_t *memory);

/**
 * @brief 获取估算的 token 数量
 */
size_t ac_memory_get_token_count(ac_memory_t *memory);

/**
 * @brief 清空所有消息（保留系统消息）
 */
void ac_memory_clear(ac_memory_t *memory);

/*============================================================================
 * Token 管理
 *============================================================================*/

/**
 * @brief 估算消息的 token 数量
 *
 * 使用近似算法：字符数 / 4（适用于英文）
 * 或 字符数 / 2（适用于中文）
 */
size_t ac_memory_estimate_tokens(const ac_message_t *message);

/**
 * @brief 手动触发截断
 *
 * 将消息列表截断到指定的 token 预算内。
 */
arc_err_t ac_memory_truncate(ac_memory_t *memory, size_t target_tokens);
```

### 3.3 Token 估算策略

由于精确的 token 计算需要 tokenizer（如 tiktoken），在嵌入式环境中不可行，采用近似算法：

```c
/**
 * Token 估算规则：
 * - 英文：约 4 字符 = 1 token
 * - 中文：约 2 字符 = 1 token（每个汉字约 1-2 token）
 * - 混合文本：取保守估计
 */
size_t ac_memory_estimate_tokens(const ac_message_t *message) {
    if (!message || !message->content) {
        return 0;
    }
    
    size_t len = strlen(message->content);
    size_t cjk_count = count_cjk_characters(message->content);
    size_t ascii_count = len - cjk_count * 3;  // UTF-8 中文约 3 字节
    
    // CJK: 1 字符 ≈ 1.5 token, ASCII: 4 字符 ≈ 1 token
    return (cjk_count * 3 / 2) + (ascii_count / 4) + 4;  // +4 为消息结构开销
}
```

### 3.4 截断策略实现

#### 策略 1：删除最早的消息 (AC_TRUNCATE_OLDEST)

```c
/**
 * 保留：
 * 1. System message（始终保留）
 * 2. 最近的 N 条消息（直到满足 token 预算）
 *
 * 删除：
 * - 最早的非 system 消息
 */
static void truncate_oldest(ac_memory_t *memory, size_t target_tokens) {
    // 计算需要保留的消息
    size_t current_tokens = memory->estimated_tokens;
    ac_message_t *msg = memory->messages;
    
    // 跳过 system message
    if (msg && msg->role == AC_ROLE_SYSTEM) {
        msg = msg->next;
    }
    
    // 从最早的消息开始删除
    while (msg && current_tokens > target_tokens) {
        current_tokens -= ac_memory_estimate_tokens(msg);
        msg = msg->next;
        memory->count--;
    }
    
    // 更新链表头
    if (memory->system_message) {
        memory->system_message->next = msg;
    } else {
        memory->messages = msg;
    }
    
    memory->estimated_tokens = current_tokens;
}
```

#### 策略 2：滑动窗口 (AC_TRUNCATE_SLIDING_WINDOW)

```c
/**
 * 保留最近 N 条消息，N 由 max_messages 配置决定。
 * 同时确保不超过 token 预算。
 */
static void truncate_sliding_window(ac_memory_t *memory, size_t target_tokens) {
    size_t max_messages = memory->config.max_messages;
    if (max_messages == 0) {
        max_messages = SIZE_MAX;
    }
    
    // 从尾部开始计算需要保留的消息
    // ...
}
```

## 4. 与 Provider 的交互

### 4.1 Provider 职责

Provider 保持构建 API 请求的自由度，因为不同的 LLM API 有不同的格式要求：

| Provider | 消息格式差异 |
|----------|------------|
| OpenAI | `{ "role": "user", "content": "..." }` |
| Anthropic | `{ "role": "user", "content": [{ "type": "text", "text": "..." }] }` |
| 其他 | 可能有更多差异 |

### 4.2 Provider 使用 Memory 的方式

```c
// openai.c 中的使用方式

static arc_err_t openai_chat(
    void* priv_data,
    const ac_llm_params_t* params,
    const ac_message_t* messages,  // 由调用方提供，可来自 Memory
    const char* tools,
    ac_chat_response_t* response
) {
    // 构建请求
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", params->model);
    
    cJSON* msgs_arr = cJSON_AddArrayToObject(root, "messages");
    
    // 遍历消息，使用序列化层转换
    for (const ac_message_t* msg = messages; msg; msg = msg->next) {
        cJSON* msg_obj = ac_message_to_json(msg);  // 调用序列化层
        cJSON_AddItemToArray(msgs_arr, msg_obj);
    }
    
    // ... 发送请求
}
```

### 4.3 Agent 层的集成

```c
// agent.c 中的使用方式

char* ac_agent_run(ac_agent_t* agent, const char* input) {
    // 1. 添加用户消息到 Memory
    ac_message_t* user_msg = ac_message_create(
        agent->arena, AC_ROLE_USER, input
    );
    ac_memory_add(agent->memory, user_msg);
    
    // 2. 获取 token 预算内的消息
    const ac_message_t* messages = ac_memory_get_messages_within_budget(
        agent->memory, 0  // 使用默认预算
    );
    
    // 3. 调用 LLM
    ac_chat_response_t response;
    arc_err_t err = ac_llm_chat_with_tools(
        agent->llm, messages, agent->tools_json, &response
    );
    
    // 4. 处理响应，添加 assistant 消息到 Memory
    if (response.content) {
        ac_message_t* assistant_msg = ac_message_create(
            agent->arena, AC_ROLE_ASSISTANT, response.content
        );
        ac_memory_add(agent->memory, assistant_msg);
    }
    
    // ...
}
```

## 5. 大文本处理

### 5.1 问题场景

当消息历史过长时，可能遇到以下问题：

1. **内存峰值**：原始消息 + cJSON 对象 + 序列化字符串 = 约 3 倍内存
2. **请求体过大**：超过 HTTP client 或 API 限制
3. **响应超时**：处理大上下文需要更长时间

### 5.2 解决方案

#### 方案 1：预算控制（推荐）

在 Memory 层进行预算控制，确保发送给 LLM 的消息不超过限制：

```c
// 配置示例
ac_memory_config_t config = {
    .context_window = 128000,    // GPT-4 Turbo 的上下文窗口
    .reserve_ratio = 0.25,       // 预留 25% 给回复
    .strategy = AC_TRUNCATE_OLDEST
};

// 实际可用预算
size_t available = config.context_window * (1 - config.reserve_ratio);
// = 128000 * 0.75 = 96000 tokens
```

#### 方案 2：请求大小检查

在序列化后检查请求大小，超过阈值时自动截断重试：

```c
// provider 中的保护逻辑
char* body = cJSON_PrintUnformatted(root);
size_t body_len = strlen(body);

if (body_len > MAX_REQUEST_SIZE) {
    AC_LOG_WARN("Request too large (%zu bytes), truncating...", body_len);
    // 重新获取更少的消息
    // ...
}
```

#### 方案 3：流式序列化（预留）

对于极端情况，可以考虑流式序列化，避免一次性生成完整 JSON：

```c
// 预留接口，暂不实现
typedef struct {
    ac_message_t* current;
    char buffer[4096];
    size_t offset;
} ac_message_stream_t;

// 逐块生成 JSON
size_t ac_message_stream_next(ac_message_stream_t* stream, char* buf, size_t len);
```

## 6. 实现优先级

### 6.1 第一阶段：基础功能

1. `ac_memory_create` / `ac_memory_destroy`
2. `ac_memory_add` / `ac_memory_get_messages`
3. `ac_memory_count` / `ac_memory_clear`
4. 基本的 token 估算

### 6.2 第二阶段：预算控制

1. `ac_memory_get_messages_within_budget`
2. `AC_TRUNCATE_OLDEST` 策略
3. `AC_TRUNCATE_SLIDING_WINDOW` 策略

### 6.3 第三阶段：高级特性（预留）

1. 持久存储（SQLite）
2. `AC_TRUNCATE_SUMMARIZE` 策略
3. 流式序列化
4. 精确 token 计算（可选 tokenizer 后端）

## 7. 设计决策记录

### 7.1 为什么不在 Memory 中处理序列化？

**决策**：Memory 模块不负责 JSON 序列化，保持与 Provider 的解耦。

**原因**：
1. 不同 Provider 有不同的消息格式（OpenAI vs Anthropic）
2. 序列化是"如何发送"的问题，不是"如何存储"的问题
3. 保持 Memory 模块的单一职责

### 7.2 为什么使用近似 token 计算？

**决策**：使用字符数近似估算 token 数量，而非精确计算。

**原因**：
1. 精确计算需要 tokenizer，增加依赖和代码体积
2. 近似算法在大多数情况下足够准确
3. 嵌入式环境资源受限
4. 可以通过配置 `reserve_ratio` 预留安全余量

### 7.3 System Message 的特殊处理

**决策**：System message 始终保留，不受截断策略影响。

**原因**：
1. System message 定义了 Agent 的行为和人格
2. 丢失 system message 会导致行为不一致
3. System message 通常较短，影响不大

## 8. 与现有代码的关系

### 8.1 现有结构

| 文件 | 当前状态 | 职责 |
|------|----------|------|
| `include/arc/memory.h` | 接口已定义 | Memory 公共 API |
| `include/arc/message.h` | 已实现 | Message 结构和 API |
| `src/memory/message.c` | 已实现 | Message 创建和操作 |
| `src/llm/message/message_json.c` | 已实现 | Message ↔ JSON 转换 |

### 8.2 需要新增/修改

| 文件 | 操作 | 说明 |
|------|------|------|
| `src/memory/memory.c` | 新增 | Memory 模块实现 |
| `include/arc/memory.h` | 修改 | 添加新接口（token 预算相关） |
| `src/llm/providers/openai.c` | 保持 | 无需修改，继续使用现有方式 |

## 9. 总结

本设计采用**分层解耦**的架构：

- **Memory 层**：专注于消息存储和预算管理
- **Serialization 层**：处理 Provider 特定的格式转换
- **Provider 层**：保持构建请求的灵活性

这种设计平衡了以下需求：

1. **简单性**：Memory 模块职责单一
2. **灵活性**：Provider 可以自由适配不同 API
3. **可扩展性**：易于添加新的截断策略或 Provider
4. **嵌入式友好**：低内存占用，可配置的限制
