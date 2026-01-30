## 问题分析：1000 Agent 高并发场景下的潜在问题

### 1. Session 容量硬限制 - 无法支持1000个Agent

```36:41:libs/ac_core/src/session.c
#define MAX_AGENTS 32
#define MAX_REGISTRIES 16
#define MAX_MCP_CLIENTS 16
```

**问题**：当前设计最多只支持32个agent、16个registry。1000个agent根本无法创建。

**影响**：第33个agent创建时直接失败。

---

### 2. Arena 内存管理 - 只增不减，无扩容

```36:45:libs/ac_core/src/arena.c
char* arena_alloc(arena_t *arena, size_t size)
{
    if (!arena || arena->count + size > arena->capacity) {
        return NULL;  // 容量不足直接失败，无扩容
    }
    // ...
}
```

**问题**：
- **Arena 无扩容机制**：分配满了就返回NULL，agent直接崩溃
- **内存只增不减**：tool registry扩容时，旧数组仍留在arena无法回收
- **默认1MB可能不够**：10000次迭代累积的消息历史远超1MB

```18:18:libs/ac_core/src/agent.c
#define DEFAULT_ARENA_SIZE (1024 * 1024)  /* 1MB per agent */
```

**计算**：假设每次迭代3条消息，每条1KB = 30MB历史，远超1MB限制。

---

### 3. 消息历史无限增长 - 无截断机制

```37:39:libs/ac_core/src/agent.c
    /* Message history (stored in arena) */
    ac_message_t *messages;
    size_t message_count;
```

**问题**：
- 消息链表不断增长，没有最大长度限制
- 虽然定义了 `ac_memory_config_t.max_messages` 但未在agent中使用
- LLM API有token限制，发送过长历史会失败（或被截断）

**影响**：
- Arena内存耗尽
- API请求失败（超过context window）
- 每次请求的token费用线性增长

---

### 4. HTTP连接管理 - 无连接池，资源耗尽

```26:28:libs/ac_core/src/llm/providers/openai.c
typedef struct {
    agentc_http_client_t *http;  // 每个LLM一个HTTP client
} openai_priv_t;
```

**问题**：
- **每个agent一个HTTP client**：1000个agent = 1000个CURL handle
- **每个MCP client也一个HTTP client**：更多连接
- **无连接复用**：每次请求都建立新TCP连接

**影响**：
- 文件描述符耗尽（通常 `ulimit -n` = 1024）
- 系统socket资源耗尽
- TIME_WAIT状态的连接堆积

---

### 5. CURL全局初始化竞态条件 - 线程不安全

```93:107:libs/ac_core/port/posix/http/http_curl.c
static int s_curl_refcount = 0;  // 无锁保护

static agentc_err_t curl_global_init_once(void) {
    if (s_curl_refcount == 0) {  // 竞态条件
        // ...
    }
    s_curl_refcount++;  // 非原子操作
}
```

**问题**：多线程并发创建/销毁agent时，refcount操作不是原子的，可能导致：
- 多次 `curl_global_init()`
- 过早 `curl_global_cleanup()`
- 未定义行为

---

### 6. 消息追加性能 - O(n)链表遍历

```88:103:libs/ac_core/src/memory/message.c
void ac_message_append(ac_message_t** list, ac_message_t* message) {
    // ...
    ac_message_t* tail = *list;
    while (tail->next) {  // O(n) 遍历到尾部
        tail = tail->next;
    }
    tail->next = message;
}
```

**问题**：
- 10000条消息时，每次追加需遍历10000个节点
- 1000个agent × 10000条消息 × O(n)追加 = 灾难性性能

**计算**：追加第n条消息需要O(n)，总时间复杂度 O(n²)

---

### 7. 工具Schema重复构建 - 性能浪费

```208:209:libs/ac_core/src/agent.c
    /* Build tools schema */
    char *tools_schema = build_tools_schema(priv);  // 每次迭代都重建
```

**问题**：
- 工具定义不变，但每次迭代都重新构建JSON
- 10000次迭代 = 10000次JSON序列化
- cJSON内部大量malloc/free

---

### 8. 内存泄漏风险点

**风险1：LLM响应异常返回**

```276:280:libs/ac_core/src/agent.c
        if (err != AGENTC_OK) {
            AC_LOG_ERROR("LLM chat failed: %d", err);
            if (tools_schema) free(tools_schema);
            return NULL;  // response未free
        }
```

**风险2：工具执行失败路径**

```301:316:libs/ac_core/src/agent.c
            for (ac_tool_call_t *call = response.tool_calls; call; call = call->next) {
                char *result = execute_tool_call(priv, call);
                // 如果后续操作失败，result可能未free
```

---

### 9. 缺少关键的并发控制

当前代码完全没有任何锁、互斥量或原子操作：

- Session的agent数组无保护
- Arena分配无保护  
- 日志输出无保护
- Tool registry无保护

**影响**：多线程运行多个agent会导致数据竞争和未定义行为。

---

### 10. 资源监控与限制缺失

当前缺少：
- 内存使用统计与限制
- 连接数监控与限制
- 请求速率限制（LLM API通常有rate limit）
- 健康检查/心跳机制
- 优雅降级策略

---

## 问题严重程度排序

| 优先级 | 问题 | 影响 |
|--------|------|------|
| **P0 致命** | Session MAX_AGENTS=32 | 无法运行1000个agent |
| **P0 致命** | Arena无扩容 | 长运行必崩溃 |
| **P1 严重** | 消息历史无限增长 | 内存耗尽+API失败 |
| **P1 严重** | HTTP连接无复用 | 文件描述符耗尽 |
| **P1 严重** | CURL refcount竞态 | 多线程崩溃 |
| **P2 重要** | 链表O(n)追加 | 性能急剧下降 |
| **P2 重要** | 无并发控制 | 数据竞争 |
| **P3 优化** | Schema重复构建 | 性能浪费 |
| **P3 优化** | 缺少监控 | 难以定位问题 |

---

