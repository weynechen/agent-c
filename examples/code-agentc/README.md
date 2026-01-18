# Code-AgentC

基于 AgentC 框架的终端代码生成 AI Agent。

## 功能特性

- **Rules 系统**: 支持自定义编码规则和约束
- **Skills 系统**: 可复用的技能模板管理
- **MCP 协议**: 集成 Model Context Protocol 服务器
- **文件操作**: 完整的文件读写、搜索能力
- **Shell 集成**: 安全的命令执行
- **Git 集成**: 版本控制操作
- **终端 UI**: 友好的交互界面

## 目录结构

```
code-agentc/
├── core/              # 核心逻辑
│   ├── rules.c        # Rules 管理器
│   ├── skills.c       # Skills 管理器
│   ├── mcp_client.c   # MCP 协议客户端
│   └── context.c      # 项目上下文管理
├── tools/             # 工具集
│   ├── file_tools.c   # 文件操作工具
│   ├── shell_tools.c  # Shell 执行工具
│   ├── search_tools.c # 代码搜索工具
│   └── git_tools.c    # Git 集成工具
├── ui/                # 用户界面
│   ├── tui.c          # 终端 UI 组件
│   └── progress.c     # 进度显示
├── include/           # 头文件
└── main.c             # 主程序入口
```

## 构建

```bash
cd examples/code-agentc
cmake -B build
cmake --build build
```

## 使用

```bash
./build/code-agentc --help
```

## 配置

创建 `.env` 文件：

```env
# LLM 配置
OPENAI_API_KEY=your-api-key
MODEL=gpt-4

# 或使用 DeepSeek
DEEPSEEK_API_KEY=your-api-key
MODEL=deepseek-chat

# MCP 服务器（可选）
MCP_SERVER_URL=http://localhost:3000
```

## Rules 配置

在项目目录创建 `.code-agentc/rules/` 目录，添加规则文件：

```yaml
# .code-agentc/rules/style.yaml
name: Code Style Rules
priority: 10
content: |
  - Always use English for code comments and logs
  - Use Chinese for documentation
  - Follow C99 standard
```

## Skills 配置

Skills 定义在 `.code-agentc/skills/` 目录：

```yaml
# .code-agentc/skills/file-ops.yaml
name: File Operations
description: File manipulation skills
tools:
  - read_file
  - write_file
  - list_directory
```
