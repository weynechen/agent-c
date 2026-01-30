arc 是一个面向嵌入式与受限系统的 C 语言 Agent Runtime。
项目目标是在仅依赖 socket / http / 文件等基础能力的前提下，将 传统 LLM Agent（LangChain / OpenAI Agents） 的核心能力落地到 C-only 平台。

# 出发点

现有 Agent 框架高度依赖 Python / TypeScript，无法运行在多数嵌入式与工业设备上。本项目将 Agent 的隐式语义显式化，实现可控、可裁剪、可移植的底层运行时。 目标支持平台：
- STM32 / ESP32 这类嵌入式平台
- Linux
- Windows
简单来说，支持如LwIP的BSD socket API的平台。

# 收益

让嵌入式设备具备真正的 LLM Agent 能力

跨平台（Linux / RTOS）一致的 Agent 基础设施

可作为设备智能、边缘 Agent 的通用底座

# 技术路径

模块化实现 Agent 核心组件：
LLM API、Prompt Manager、Tool Calling、Chat Memory、MCP/ACP、Agent Loop，
并通过平台抽象层保证跨平台可移植性。

## 底层网络模块
采用分层设计，arc 封装接口层。
对于嵌入式的平台来说，backend采用mongoose。

    > mongoose ，一个c语言的跨平台库。
    > https://github.com/cesanta/mongoose

如果要更底层，那么就是基于lwIP 和 libwebsockets开始。

对于Linux/Windows/Mac来说，使用libcurl。
