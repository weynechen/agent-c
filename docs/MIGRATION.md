# API重构迁移指南

本文档说明从旧API到新API的主要变化。

## 概述

根据 `docs/design/interface.md` 设计文档，我们完成了以下重构：

1. **API前缀**：`agentc_` → `ac_`
2. **LLM接口**：简化为单一的 `ac_llm_params_t` 结构
3. **新增Memory模块**：`ac_memory_t` 用于会话记忆管理
4. **Agent接口**：简化为 `ac_agent_params_t` 结构
5. **Stream接口**：添加 `ac_stream_t` 相关API（暂未实现）

## API命名变化

### 全局函数
```c
// Old API
agentc_init()
agentc_cleanup()
agentc_version()
agentc_strerror()

// New API
ac_init()
ac_cleanup()
ac_version()
ac_strerror()
```

### LLM模块
```c
// Old API
agentc_llm_create()
agentc_llm_destroy()
agentc_llm_chat()
agentc_llm_complete()
agentc_message_create()
agentc_message_free()

// New API
ac_llm_create()
ac_llm_destroy()
ac_llm_chat()
ac_llm_complete()
ac_message_create()
ac_message_free()
```

### Tool模块
```c
// Old API
agentc_tool_registry_create()
agentc_tool_registry_destroy()
agentc_tool_register()
agentc_tool_execute()

// New API
ac_tools_create()
ac_tools_destroy()
ac_tool_register()
ac_tool_execute()
```

### Agent模块
```c
// Old API
agentc_agent_create()
agentc_agent_destroy()
agentc_agent_run()
agentc_agent_reset()

// New API
ac_agent_create()
ac_agent_destroy()
ac_agent_run_sync()  // 同步调用
ac_agent_run()        // 流式调用 (未实现)
ac_agent_reset()
```

## 结构体变化

### LLM参数

**旧API** - 分离的配置和请求：
```c
// Config
agentc_llm_config_t config = {
    .api_key = "...",
    .base_url = "...",
    .model = "...",
};
agentc_llm_client_t *llm;
agentc_llm_create(&config, &llm);

// Request
agentc_chat_request_t req = {
    .messages = messages,
    .temperature = 0.7,
    .max_tokens = 1000,
};
```

**新API** - 统一的参数结构：
```c
ac_llm_t *llm = ac_llm_create(&(ac_llm_params_t){
    // Base info
    .model = "gpt-4",
    .api_key = "...",
    .api_base = "...",
    .instructions = "You are a helpful assistant",
    
    // Parameters
    .temperature = 0.7,
    .max_tokens = 1000,
    .timeout_ms = 60000,
});
```

### Agent参数

**旧API**：
```c
agentc_agent_config_t config = {
    .llm = llm,
    .tools = tools,
    .name = "MyAgent",
    .instructions = "...",
    .max_iterations = 10,
    .temperature = 0.7,
    .max_tokens = 1000,
    .tool_choice = "auto",
    .parallel_tool_calls = 1,
    .stream = 0,
    .hooks = {...},
};

agentc_agent_t *agent;
agentc_agent_create(&config, &agent);
```

**新API**：
```c
ac_agent_t *agent = ac_agent_create(&(ac_agent_params_t){
    .name = "MyAgent",
    .llm = llm,              // Required
    .tools = tools,          // Optional
    .memory = memory,        // Optional (new!)
    .max_iterations = 10,
    .timeout_ms = 0,
});
```

## 新增：Memory模块

Memory模块是全新的功能，用于管理会话记忆：

```c
// Create memory
ac_memory_t *memory = ac_memory_create(&(ac_memory_config_t){
    .session_id = "session-123",
    .max_messages = 100,
    .max_tokens = 0,
});

// Add message to memory
ac_message_t *msg = ac_message_create(AC_ROLE_USER, "Hello");
ac_memory_add(memory, msg);

// Get all messages
const ac_message_t *messages = ac_memory_get_messages(memory);

// Use with agent
ac_agent_t *agent = ac_agent_create(&(ac_agent_params_t){
    .llm = llm,
    .memory = memory,  // Agent will automatically use memory
    .max_iterations = 10,
});

// Cleanup
ac_memory_destroy(memory);
```

## Agent运行方式变化

### 同步调用

**旧API**：
```c
agentc_run_result_t result;
agentc_agent_run(agent, "Hello", &result);

printf("%s\n", result.final_output);
agentc_run_result_free(&result);
```

**新API**：
```c
ac_agent_result_t result;
ac_agent_run_sync(agent, "Hello", &result);

printf("%s\n", result.response);
ac_agent_result_free(&result);
```

### 流式调用（新增）

```c
ac_stream_t *stream = ac_agent_run(agent, "Hello");

while (ac_stream_is_running(stream)) {
    ac_stream_result_t *result = ac_stream_next(stream, 1000);
    if (result) {
        switch (result->type) {
            case AC_STREAM_CONTENT:
                printf("%.*s", (int)result->content_len, result->content);
                break;
            case AC_STREAM_DONE:
                printf("\nDone!\n");
                break;
            // ...
        }
    }
}

ac_stream_destroy(stream);
```

**注意**：Stream接口头文件已定义，但实现尚未完成。

## 工具定义变化

Tool定义基本保持不变，只是类型名称更新：

```c
// Old types
agentc_tool_t
agentc_param_t
agentc_tool_handler_t
AGENTC_PARAM_STRING

// New types
ac_tool_t
ac_param_t
ac_tool_handler_t
AC_PARAM_STRING
```

## 完整示例对比

### 旧API示例

```c
agentc_init();

agentc_llm_config_t llm_config = {
    .api_key = getenv("OPENAI_API_KEY"),
    .model = "gpt-4",
};
agentc_llm_client_t *llm;
agentc_llm_create(&llm_config, &llm);

agentc_tool_registry_t *tools = agentc_tool_registry_create();
// register tools...

agentc_agent_config_t config = {
    .llm = llm,
    .tools = tools,
    .max_iterations = 10,
};
agentc_agent_t *agent;
agentc_agent_create(&config, &agent);

agentc_run_result_t result;
agentc_agent_run(agent, "Hello", &result);
printf("%s\n", result.final_output);

agentc_run_result_free(&result);
agentc_agent_destroy(agent);
agentc_tool_registry_destroy(tools);
agentc_llm_destroy(llm);
agentc_cleanup();
```

### 新API示例

```c
ac_init();

ac_llm_t *llm = ac_llm_create(&(ac_llm_params_t){
    .model = "gpt-4",
    .api_key = getenv("OPENAI_API_KEY"),
    .instructions = "You are a helpful assistant",
    .temperature = 0.7,
});

ac_tools_t *tools = ac_tools_create();
// register tools...

ac_memory_t *memory = ac_memory_create(&(ac_memory_config_t){
    .session_id = "session-123",
});

ac_agent_t *agent = ac_agent_create(&(ac_agent_params_t){
    .llm = llm,
    .tools = tools,
    .memory = memory,
    .max_iterations = 10,
});

ac_agent_result_t result;
ac_agent_run_sync(agent, "Hello", &result);
printf("%s\n", result.response);

ac_agent_result_free(&result);
ac_agent_destroy(agent);
ac_memory_destroy(memory);
ac_tools_destroy(tools);
ac_llm_destroy(llm);
ac_cleanup();
```

## 移除的功能

1. **Agent Hooks**：旧API中的 `agentc_agent_hooks_t` 已移除
   - `on_start`, `on_content`, `on_tool_call` 等回调
   - 如需类似功能，请使用Stream API（待实现）

2. **配置项**：
   - LLM的 `organization` 字段保留但较少使用
   - Agent的 `parallel_tool_calls` 已合并到LLM参数中

## 待实现功能

1. **Stream API**：头文件已定义，实现待完成
   - `ac_agent_run()` - 返回stream
   - `ac_stream_next()` - 获取下一个结果
   - `ac_stream_is_running()` - 检查状态

2. **Persistent Memory**：
   - `ac_memory_save()` - 保存到SQLite
   - `ac_memory_load()` - 从SQLite加载

3. **MOC工具链**：
   - 使用 `@agentc_tool` 注解自动生成工具注册代码

## 编译变化

CMakeLists.txt已更新，现在包含memory模块：

```cmake
set(AGENTC_SOURCES
    src/agentc.c
    src/http_client.c
    src/llm.c
    src/tool.c
    src/memory.c    # New!
    src/agent.c
    ${CJSON_SOURCES}
)
```

## 总结

这次重构的主要目标是：

1. ✅ 简化API - 使用 `ac_` 前缀，更短更易记
2. ✅ 统一配置 - LLM参数集中到一个结构体
3. ✅ 添加Memory - 新增会话记忆管理
4. ✅ 简化Agent - 移除复杂的hooks机制
5. 🚧 Stream API - 接口已定义，实现待完成

新API更符合设计文档的理念，更适合嵌入式和受限环境使用。
