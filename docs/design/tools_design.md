## 问题分析

当前设计缺失的部分：
1. **工具注册的全局视图**：所有可用工具存储在哪里？
2. **工具分组机制**：tools_group 如何组织和管理工具？
3. **Agent 与工具的绑定**：Agent 如何获取和使用特定的工具集合？
4. **MOC 的角色**：自动生成的代码如何与运行时系统集成？

## 设计方案

我提供三个方案供你选择：

### 方案一：**全局注册表 + 动态过滤**

```
架构层次：
┌─────────────────────────────────────────┐
│          Application Layer              │
│  Agent A (tools: "file,shell")          │
│  Agent B (tools: "weather,calendar")    │
└──────────────┬──────────────────────────┘
               │ Query by group names
┌──────────────▼──────────────────────────┐
│      Tool Group Manager                 │
│  - Parse group names                    │
│  - Filter tools by groups               │
│  - Build tool list for agent            │
└──────────────┬──────────────────────────┘
               │ Query all tools
┌──────────────▼──────────────────────────┐
│    Global Tool Registry                 │
│  tool_1 → [file, shell]                 │
│  tool_2 → [weather]                     │
│  tool_3 → [file]                        │
│  ...                                    │
└─────────────────────────────────────────┘
```

**接口设计：**

```c
/* Global registry - stores ALL available tools */
typedef struct ac_tool_registry ac_tool_registry_t;

/* Tool definition with metadata */
typedef struct {
    const char* name;
    const char* description;
    const char* json_schema;
    ac_tool_func_t func;
    const char** groups;        /* Array of group names, NULL terminated */
    int group_count;
} ac_tool_t;

/* Global registry API */
ac_tool_registry_t* ac_tool_registry_get_global(void);
int ac_tool_register(ac_tool_registry_t* registry, const ac_tool_t* tool);
const ac_tool_t* ac_tool_find(ac_tool_registry_t* registry, const char* name);

/* Tool group - represents a filtered view of tools */
typedef struct ac_tool_group ac_tool_group_t;

/* Create tool group by filtering global registry */
ac_tool_group_t* ac_tool_group_create_from_names(
    arena_t* arena,
    const char* group_names  /* e.g., "file,shell,weather" */
);

/* Get tools array for LLM API call */
const ac_tool_t** ac_tool_group_get_tools(ac_tool_group_t* group, int* count);
```

**使用示例：**

```c
/* Step 1: Register tools (usually in MOC-generated code) */
ac_tool_registry_t* registry = ac_tool_registry_get_global();

ac_tool_register(registry, &(ac_tool_t){
    .name = "read_file",
    .description = "Read file content",
    .json_schema = "...",
    .func = tool_read_file_impl,
    .groups = (const char*[]){"file", "io", NULL},
    .group_count = 2
});

/* Step 2: Agent creates tool group by names */
ac_agent_t* agent = ac_agent_create(session, &(ac_agent_params_t){
    .name = "FileAgent",
    .tools_name = "file,shell",  /* Comma-separated group names */
    ...
});

/* Internally, agent creates tool group */
ac_tool_group_t* group = ac_tool_group_create_from_names(
    agent->arena,
    "file,shell"
);
```

**优点：**
- 工具只注册一次，避免重复
- 灵活的动态组合（Agent 可以使用任意组合的工具组）
- 全局视图清晰，便于管理和调试

**缺点：**
- 需要运行时字符串解析和过滤
- 全局注册表需要线程安全处理
- 略复杂

---

### 方案二：**独立工具组（静态分组）**

```
架构层次：
┌─────────────────────────────────────────┐
│          Application Layer              │
│  Agent A → Tool Group "file"            │
│  Agent B → Tool Group "weather"         │
└──────────────┬──────────────────────────┘
               │ Direct reference
┌──────────────▼──────────────────────────┐
│         Tool Groups                     │
│  ┌─────────────┐  ┌─────────────┐      │
│  │ Group "file"│  │Group"weather│      │
│  │- read_file  │  │- get_weather│      │
│  │- write_file │  │- forecast   │      │
│  └─────────────┘  └─────────────┘      │
└─────────────────────────────────────────┘
```

**接口设计：**

```c
/* Tool definition (no group metadata) */
typedef struct {
    const char* name;
    const char* description;
    const char* json_schema;
    ac_tool_func_t func;
} ac_tool_t;

/* Tool group - independent collection */
typedef struct ac_tool_group ac_tool_group_t;

ac_tool_group_t* ac_tool_group_create(arena_t* arena, const char* name);
int ac_tool_group_add(ac_tool_group_t* group, const ac_tool_t* tool);
const ac_tool_t* ac_tool_group_find(ac_tool_group_t* group, const char* name);
const ac_tool_t** ac_tool_group_get_all(ac_tool_group_t* group, int* count);

/* Registry - manages named tool groups */
typedef struct ac_tool_registry ac_tool_registry_t;

ac_tool_registry_t* ac_tool_registry_create(void);
int ac_tool_registry_add_group(ac_tool_registry_t* registry, 
                                 const char* name,
                                 ac_tool_group_t* group);
ac_tool_group_t* ac_tool_registry_get_group(ac_tool_registry_t* registry,
                                              const char* name);
```

**使用示例：**

```c
/* Step 1: Create tool groups */
ac_tool_group_t* file_group = ac_tool_group_create(arena, "file");
ac_tool_group_add(file_group, &(ac_tool_t){
    .name = "read_file",
    .description = "Read file content",
    ...
});
ac_tool_group_add(file_group, &(ac_tool_t){
    .name = "write_file",
    ...
});

/* Step 2: Register groups */
ac_tool_registry_t* registry = ac_tool_registry_get_global();
ac_tool_registry_add_group(registry, "file", file_group);

/* Step 3: Agent uses group by name */
ac_agent_t* agent = ac_agent_create(session, &(ac_agent_params_t){
    .tools_name = "file",  /* Single group name */
    ...
});
```

**优点：**
- 简单直观，概念清晰
- 无需运行时过滤
- 工具组边界明确

**缺点：**
- 如果工具属于多个组，需要重复添加
- Agent 只能使用一个预定义的组（不能动态组合）
- 灵活性较差

---

### 方案三：**分层设计（推荐）**

```
架构层次：
┌─────────────────────────────────────────┐
│          Application Layer              │
│  Agent A → ["file", "shell"]            │
│  Agent B → ["weather"]                  │
└──────────────┬──────────────────────────┘
               │ Query by group tags
┌──────────────▼──────────────────────────┐
│      Tool Registry (Indexed)            │
│  Tools Storage:                         │
│    - tool_list[] (all tools)            │
│  Group Index:                           │
│    - "file" → [0, 2, 5]                 │
│    - "shell" → [1, 3]                   │
│    - "weather" → [4]                    │
└──────────────┬──────────────────────────┘
               │ Generated by MOC
┌──────────────▼──────────────────────────┐
│      Tool Definitions (Code)            │
│  tool_read_file + @tools_group: file    │
│  tool_exec_shell + @tools_group: shell  │
│  ...                                    │
└─────────────────────────────────────────┘
```

**接口设计：**

```c
/* Tool definition with group tags */
typedef struct {
    const char* name;
    const char* description;
    const char* json_schema;
    ac_tool_func_t func;
} ac_tool_def_t;

/* Tool registry - indexed by groups */
typedef struct ac_tool_registry ac_tool_registry_t;

/* Create registry from tool definitions array (MOC-generated) */
ac_tool_registry_t* ac_tool_registry_create_from_defs(
    arena_t* arena,
    const ac_tool_def_t* tools,
    int tool_count,
    const char* const* group_tags,  /* Parallel array of group tags */
    int tag_count
);

/* Tool group - lightweight view into registry */
typedef struct ac_tool_group ac_tool_group_t;

/* Create tool group by querying registry */
ac_tool_group_t* ac_tool_group_create_from_registry(
    arena_t* arena,
    ac_tool_registry_t* registry,
    const char* group_names  /* e.g., "file,shell" */
);

/* Get tools for LLM call */
const ac_tool_def_t** ac_tool_group_get_tools(
    ac_tool_group_t* group,
    int* count
);

/* Find tool for execution */
const ac_tool_def_t* ac_tool_group_find(
    ac_tool_group_t* group,
    const char* name
);
```

**MOC 生成代码示例：**

```c
/* User code */
/**
 * @agentc_tool
 * @tools_group: file,io
 * @description: Read file content
 */
char* tool_read_file(const char* path) { ... }

/* MOC-generated code: tool_registry_generated.c */

/* Tool definitions */
static const ac_tool_def_t g_tools[] = {
    {
        .name = "read_file",
        .description = "Read file content",
        .json_schema = "{\"type\":\"object\",\"properties\":{...}}",
        .func = tool_read_file
    },
    // ... more tools
};

/* Group index (tool_index -> group_tags) */
static const char* g_tool_groups[] = {
    "file,io",      /* tool 0: read_file */
    "shell",        /* tool 1: exec_shell */
    // ...
};

/* Registration function */
ac_tool_registry_t* register_all_tools(arena_t* arena) {
    return ac_tool_registry_create_from_defs(
        arena,
        g_tools,
        sizeof(g_tools) / sizeof(g_tools[0]),
        g_tool_groups,
        sizeof(g_tool_groups) / sizeof(g_tool_groups[0])
    );
}
```

**使用示例：**

```c
/* Application initialization */
ac_tool_registry_t* registry = register_all_tools(global_arena);

/* Agent creation */
ac_agent_t* agent = ac_agent_create(session, &(ac_agent_params_t){
    .tools_name = "file,shell",  /* Multiple groups */
    .tools_registry = registry,  /* Pass registry reference */
    ...
});

/* Internally: agent creates tool group */
agent->tool_group = ac_tool_group_create_from_registry(
    agent->arena,
    registry,
    params->tools_name
);
```

**优点：**
- 工具只定义一次（MOC 生成）
- 支持动态组合（Agent 可以使用多个组）
- 高效索引（编译期生成，运行时快速查找）
- 清晰的职责分离

**缺点：**
- 需要 MOC 工具生成索引代码
- 实现相对复杂

---

## 对比总结

| 特性 | 方案一（全局+过滤） | 方案二（独立分组） | 方案三（分层索引）⭐ |
|------|-------------------|-------------------|---------------------|
| 工具重复注册 | 否 | 是（多组情况） | 否 |
| 动态组合 | 是 | 否 | 是 |
| 运行时开销 | 中等（字符串解析） | 低 | 低（预索引） |
| 实现复杂度 | 中等 | 低 | 高 |
| MOC 集成 | 简单 | 简单 | 需要生成索引 |
| 可维护性 | 好 | 一般 | 优秀 |

## 我的建议

**推荐方案三（分层索引设计）**，理由：

1. **符合当前架构**：与你的 MOC 自动生成、arena 内存管理理念一致
2. **性能最优**：编译期生成索引，运行时O(1)查找
3. **灵活性强**：支持工具属于多个组，Agent 可以动态组合
4. **可扩展**：未来可以轻松添加工具权限、版本等元数据

