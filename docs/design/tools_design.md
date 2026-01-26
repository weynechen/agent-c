# C-Native Function Calling MOC 设计说明 (v2.0)

## 1. 核心架构：全 C 工具链

系统由一个独立的编译时工具 `moc` 和运行时库组成。

* **moc (Build-time)**：一个小型 C 程序，使用 `libtree-sitter` 和 `tree-sitter-c`。
* **输入**：包含 `AC_TOOL_META` 宏标记和 Doxygen 风格注释的 `.h` 文件。
* **输出**：自动生成的 `tools_gen.c` 和 `tools_gen.h`。
* **工作流**：`CMake` 预编译 `moc` -> 执行 `moc` 处理头文件 -> 将生成的代码编译进主程序。

---

## 2. MOC 解析策略 (Tree-sitter C API)

在 C 中遍历 AST 较为繁琐，设计应采用 **Query 驱动** 模式。

### A. 捕获元数据 (The Query)

利用 Tree-sitter 的 `TSQuery` API，通过 S-Expression 一次性定位目标节点。

```scm
; 查找带有注释的函数声明
(declaration
  (comment) @doc
  (function_declarator
    declarator: (identifier) @name
    parameters: (parameter_list) @params)
  ) @func_decl

```

### B. 符号解析与类型映射

* **基本类型**：映射 `int`, `float`, `char*` 到相应的 `cJSON` 提取函数。
* **POD 结构体**：当 `params` 中出现非内置类型时，MOC 需在同文件中二次查找 `struct_specifier`，提取成员并生成递归解析函数 `_parse_struct_X`。

注： 分步骤实现，POD为预留，暂不实现

---

## 3. 代码生成接口 (Code Generation)

由于 C 处理字符串较弱，建议采用 **“静态模板 + 动态填充”** 的方式生成代码。

输入举例

```c
> #ifndef _TOOLS_H
#define _TOOLS_H

// 使用宏作为标记，对编译器无影响，但对 MOC 脚本很重要
#define AC_TOOL_META 

/**
 * @description: 获取指定城市的温度
 * @param: place  城市名称
 */
AC_TOOL_META const char* get_weather(const char* place);

/**
 * @description: 加法运算
 * @param: a  数字1
 * @param: b  数字2
 */
AC_TOOL_META int add_two_numbers(int a, int b);

#endif
```


### A. 自动生成的 Wrapper 接口

MOC 为每个函数生成的 Wrapper 将统一符合 `ac_wrapper_t` 签名：

```c
// 接口定义
typedef char* (*ac_wrapper_t)(const char* json_args);

// MOC 自动生成的示例：
char* __wrapper_get_weather(const char* args) {
    cJSON *root = cJSON_Parse(args);
    // 自动生成的类型提取逻辑...
    const char* result = get_weather(cJSON_GetObjectItem(root, "place")->valuestring);
    cJSON_Delete(root);
    return strdup(result); // 结果返回给运行时分发器
}

```

### B. 自动生成的注册表

MOC 会汇总所有函数，生成一个静态数组，作为运行时的“路由表”：

```c
typedef struct {
    const char* name; // name 必须是函数名称一致，提供便捷的宏转换函数到字符串""##，使得用户仅需要提供函数名即可
    const char* schema;     // 由 MOC 根据注释生成的 JSON 字符串
    ac_wrapper_t wrapper;
} ac_tool_entry_t;

extern const ac_tool_entry_t G_TOOL_TABLE[];

```

---

## 4. 逻辑实现要点 (Logic Flow)

1. **加载语言**：调用 `tree_sitter_c()` 初始化解析器。
2. **提取源码文本**：将头文件读入内存。Tree-sitter 的 `TSNode` 只存储偏移量，需配合原始 `buffer` 获取字符串（如函数名）。
3. **解析注释**：
* 解析 `raw_comment`。
* 识别 `@description` 作为 Schema 的 `description`。
* 识别 `@param` 并匹配其类型，构建 Schema 的 `parameters` 对象。


4. **生成 C 代码字符串**：
* 使用 `fprintf` 或自定义的 `string_builder`。
* **重点**：对于结构体参数，先生成其成员的解析函数，再生成 Wrapper。



---

## 5. CMake 集成设计

为了自动化流程，需要在 `CMakeLists.txt` 中定义 `add_custom_command`：

```cmake
# 1. 编译 MOC 工具本身
add_executable(moc main.c ...) 

# 2. 定义自动生成规则
add_custom_command(
    OUTPUT tools_gen.c
    COMMAND ./moc ${CMAKE_CURRENT_SOURCE_DIR}/tools.h > tools_gen.c
    DEPENDS moc tools.h
)

# 3. 将生成的代码链接到主工程
add_executable(app main_app.c tools_gen.c)

```

---

## 6. 设计约束与边界

* **类型检查**：MOC 不进行 C 语言级别的完整语义检查，仅进行语法提取。
* **内存安全**：所有由 MOC 生成的代码必须确保在解析失败（`cJSON_Parse` 返回 NULL）时安全退出。
* **结构体递归**：暂不支持包含指针成员的结构体，以避免深拷贝和复杂的生命周期管理。

## 7. 使用

在agent中使用，用户仅需要提供如：
```c
tools_group = [get_weather , add_two_numbers]
```
的形式就可以使用。
