# ArC Platform Wrap - 平台封装层

这个模块为 hosted 环境提供跨平台的抽象和工具函数。

## 目录结构

```
external/args/
├── CMakeLists.txt         # Build configuration
├── include/
│   └── platform_wrap.h    # Platform wrapper interface
└── src/
    └── platform_wrap.c    # Platform wrapper implementation
```

## 功能模块

### 1. Terminal Initialization (终端初始化)

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
#include "platform_wrap.h"

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

### 2. UTF-8 Command Line Arguments (UTF-8 命令行参数)

提供跨平台的 UTF-8 命令行参数处理，特别是解决 Windows 上的编码问题。

#### 功能特性

- **Windows**: 将系统编码（通常是 GBK）转换为 UTF-8
- **Linux/macOS**: 直接使用原始 argv（通常已是 UTF-8）
- **自动管理**: 自动处理内存分配和释放

#### 使用示例

```c
#include "platform_wrap.h"

int main(int argc, char *argv[]) {
    platform_init_terminal(NULL);
    
    // Get UTF-8 encoded arguments (handles Windows encoding)
    char **utf8_argv = platform_get_argv_utf8(argc, argv);
    
    // Use utf8_argv instead of argv
    if (argc > 1) {
        printf("First argument: %s\n", utf8_argv[1]);
    }
    
    // Cleanup
    platform_free_argv_utf8(utf8_argv, argc);
    platform_cleanup_terminal();
    return 0;
}
```

## API 参考

### Terminal Functions

```c
// Get default configuration with auto-detection
platform_init_config_t platform_init_get_defaults(void);

// Initialize terminal
int platform_init_terminal(const platform_init_config_t *config);

// Cleanup terminal state
void platform_cleanup_terminal(void);
```

### Command Line Argument Functions

```c
// Get UTF-8 encoded command line arguments
char **platform_get_argv_utf8(int argc, char *argv[]);

// Free memory allocated by platform_get_argv_utf8
void platform_free_argv_utf8(char **utf8_argv, int argc);
```

## 设计原则

1. **平台无关性**: 应用代码不应包含任何平台相关的 `#ifdef` 宏
2. **封装**: 所有平台相关逻辑封装在 `platform_wrap.c` 内部
3. **清晰接口**: 提供简洁、易用的跨平台 API
4. **可扩展**: 后续可添加更多平台相关的功能

## 集成到项目

在 CMakeLists.txt 中：

```cmake
# Link platform_wrap library
target_link_libraries(your_target PRIVATE arc_platform_wrap)

# Include header directory
target_include_directories(your_target PRIVATE
    ${CMAKE_SOURCE_DIR}/external/args/include
)
```

## 后续扩展方向

计划添加的功能：

- [x] 终端 UTF-8 初始化
- [x] 命令行参数 UTF-8 转换
- [ ] 终端大小检测
- [ ] 环境变量处理
- [ ] 文件路径规范化
- [ ] 进程间通信工具

## 许可证

遵循 ArC 项目的许可证。
