# AgentC Args - 平台初始化和参数解析工具

这个模块为 hosted 环境提供平台相关的初始化功能和命令行参数解析工具。

## 目录结构

```
extras/args/
├── CMakeLists.txt         # 构建配置
├── include/
│   └── platform_init.h    # 平台初始化接口
└── src/
    └── platform_init.c    # 平台初始化实现
```

## 功能模块

### 1. Platform Init (平台初始化)

提供跨平台的终端初始化功能，处理各平台的特定设置。

#### 功能特性

- **Windows**: 
  - 设置控制台代码页为 UTF-8 (CP 65001)
  - 启用 ANSI 转义序列支持 (Windows 10+)
  - 自动保存和恢复原始控制台设置

- **Linux/macOS**: 
  - 检测 TTY 环境
  - 自动检测颜色支持

- **其他平台**: 
  - 无操作（no-op）

#### 使用示例

```c
#include "platform_init.h"

int main(void) {
    // Use default configuration (auto-detect)
    platform_init_terminal(NULL);
    
    // Your application code here
    printf("Hello, World! 你好世界! 🌍\n");
    
    // Cleanup on exit
    platform_cleanup_terminal();
    return 0;
}
```

#### 自定义配置

```c
platform_init_config_t config = {
    .enable_colors = 1,   // Force enable colors
    .enable_utf8 = 1,     // Force enable UTF-8
};
platform_init_terminal(&config);
```

配置选项：
- `1` = 强制启用
- `0` = 强制禁用
- `-1` = 自动检测（默认）

#### API 参考

```c
// Get default configuration with auto-detection
platform_init_config_t platform_init_get_defaults(void);

// Initialize terminal
int platform_init_terminal(const platform_init_config_t *config);

// Cleanup terminal state
void platform_cleanup_terminal(void);
```

## 设计原则

1. **平台无关性**: 示例代码（如 `chat_demo.c`）不应包含任何平台相关的 `#ifdef` 宏
2. **封装**: 所有平台相关逻辑封装在 `platform_init.c` 内部
3. **清晰接口**: 提供简洁、易用的跨平台 API
4. **可扩展**: 后续可添加更多功能（如参数解析、终端大小检测等）

## 集成到项目

在 CMakeLists.txt 中：

```cmake
# Link platform_init library
target_link_libraries(your_target PRIVATE agentc_platform_init)

# Include header directory
target_include_directories(your_target PRIVATE
    ${CMAKE_SOURCE_DIR}/extras/args/include
)
```

## 未来扩展

计划添加的功能：

- [ ] 命令行参数解析器 (`args_parser.h`)
- [ ] 终端大小检测
- [ ] 进度条支持
- [ ] 颜色输出工具函数
- [ ] 交互式输入工具

## 许可证

遵循 AgentC 项目的许可证。
