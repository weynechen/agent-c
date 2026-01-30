# Minimal CLI

一个基于 ArC 框架的轻量级 AI 命令行工具。展示如何使用 ArC 快速构建实用的 AI Agent。

## 特性

- ⚡ **快速上手**: 单个二进制文件，立即可用
- 💬 **双模式**: 支持单次对话和交互式会话
- 🛠️ **内置工具**: Shell 执行、文件操作、计算器、时间查询
- 🔌 **多 Provider**: 支持 OpenAI、Anthropic、DeepSeek 等
- 📡 **流式输出**: 实时显示 AI 响应
- 🔒 **安全沙箱**: 危险命令需要用户确认

## 快速开始

### 构建

```bash
cd extras/minimal_cli
cmake -B build
cmake --build build
```

### 配置

创建 `.env` 文件：

```env
# OpenAI
OPENAI_API_KEY=sk-xxx
MODEL=gpt-4o-mini

# 或 Anthropic
ANTHROPIC_API_KEY=sk-ant-xxx
MODEL=claude-3-5-sonnet-20241022

# 或 DeepSeek
DEEPSEEK_API_KEY=sk-xxx
DEEPSEEK_BASE_URL=https://api.deepseek.com/v1
MODEL=deepseek-chat
```

## 使用示例

### 单次对话

```bash
# 快速提问
./minimal_cli "What is the capital of France?"

# 执行计算
./minimal_cli "Calculate 123 * 456"

# 获取时间
./minimal_cli "What time is it?"

# 文件操作
./minimal_cli "Read README.md and summarize it"

# Shell 命令
./minimal_cli "List all .c files in current directory"
```

### 交互式会话

```bash
./minimal_cli -i

> Hello, how are you?
[AI] I'm doing well! How can I help you today?

> What time is it?
[Tool] get_current_time()
[AI] It's currently 14:30:00 on 2026-01-23.

> Calculate 2^10
[Tool] calculator({"operation": "^", "a": 2, "b": 10})
[AI] 2^10 = 1024

> exit
Goodbye!
```

### 命令行选项

```bash
# 基础选项
minimal_cli -h, --help              # 显示帮助
minimal_cli -v, --version           # 显示版本
minimal_cli -i, --interactive       # 交互模式

# 配置选项
minimal_cli --model gpt-4           # 指定模型
minimal_cli --provider anthropic    # 指定 provider
minimal_cli --temp 0.9              # 设置温度
minimal_cli --max-iter 10           # 最大迭代次数

# 功能开关
minimal_cli --no-tools              # 禁用工具
minimal_cli --no-stream             # 禁用流式输出
minimal_cli --safe-mode             # 安全模式（所有命令需确认）

# 输出控制
minimal_cli --verbose               # 详细输出
minimal_cli --quiet                 # 安静模式
minimal_cli --json                  # JSON 输出
```

## 内置工具

### Shell 执行
```bash
./minimal_cli "Show git status"
./minimal_cli "Create backup of config.json"
```

### 文件操作
```bash
./minimal_cli "Read and analyze main.c"
./minimal_cli "Write 'Hello World' to output.txt"
./minimal_cli "List all files in src/"
```

### 计算器
```bash
./minimal_cli "Calculate (123 + 456) * 789"
./minimal_cli "What is 2^20?"
```

### 时间查询
```bash
./minimal_cli "What time is it?"
./minimal_cli "What's the date?"
```

## 安全特性

Minimal CLI 内置安全机制：

- 危险命令（`rm -rf`、`sudo`、`chmod 777` 等）需要用户确认
- Shell 执行默认在受限环境中运行
- 文件写入前显示预览
- 可配置命令白名单/黑名单

## 环境变量

```bash
# LLM Provider
OPENAI_API_KEY          # OpenAI API key
OPENAI_BASE_URL         # OpenAI API base URL (optional)
ANTHROPIC_API_KEY       # Anthropic API key
DEEPSEEK_API_KEY        # DeepSeek API key
DEEPSEEK_BASE_URL       # DeepSeek API base URL

# Configuration
MODEL                   # Default model name
TEMPERATURE             # Temperature (0.0-2.0)
MAX_ITERATIONS          # Max tool iterations
ENABLE_TOOLS            # Enable/disable tools (true/false)
SAFE_MODE               # Safe mode (true/false)
```

## 架构

Minimal CLI 基于 ArC 框架构建：

- **ac_core**: 核心 Agent 运行时、LLM 抽象、工具系统
- **ac_hosted**: 扩展功能（Markdown、MCP 等）
- **builtin_tools**: 内置工具实现
- **config**: 配置管理

## 开发

### 添加自定义工具

```c
// 在 tools/builtin_tools.c 中添加
static arc_err_t tool_my_function(
    const cJSON *args,
    char **output,
    void *user_data
) {
    // Tool implementation
    *output = strdup("{\"result\": \"success\"}");
    return ARC_OK;
}

// 注册工具
ac_tool_t tool = {
    .name = "my_function",
    .description = "My custom function",
    .parameters = params,
    .handler = tool_my_function,
};
ac_tool_register(tools, &tool);
```

## 许可证

与 ArC 框架相同的许可证。
