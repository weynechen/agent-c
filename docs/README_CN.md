# ArC

一个用 C 语言编写的 Agent 库。支持hosted(Windows/Linux/MacOS)环境和RTOS环境，仅需依赖POSIX Thread，以及https连接。

注：此项目正在快速开发中，许多特性尚未完成，API可能发生变化。我当前仅在Linux(Ubuntu && Arch)，windows和mac尚未完全测试通过。

## 特性 

### Core
跨平台的特性：
- [x] 基本的agent(prompt/llm/message manager) : 支持OpenAI兼容接口，多轮对话，ReACT循环。
- [x] tools : 提供moc工具，只需要编写正常的函数，并写好注释即可。 
- [x] mcp : 支持mcp client中的streaming http 和 sse。
- [x] 跟踪：agent各个关键点，都有回调，并提供方便的json日志导出。

### hosted
以下这些特性仅在windows/Linux/macOS 主机平台上支持。（部分特性RTOS也能支持，理论上只需要文件系统就可以。但嵌入式系统一般资源有限，就不考虑了。)
- [x] sandbox : 检测危险命令，支持Linux和macOS，windows尚未支持。
- [-] skills 
- [x] tui 
- [x] markdown 渲染
- [ ] 记忆持久化
- [x] 连接池 ： 为后续agent 群打基础。



## 用法
### 基础
一个简单的聊天功能,示例如下：
```c
ac_session_t *session = ac_session_open();
ac_agent_t *agent = ac_agent_create(session, &(ac_agent_params_t){
    .name = "HelloBot",
    .instructions = "You are a friendly assistant.",
    .llm = {
        .provider = "openai",
        .model = model,
        .api_key = api_key,
        .api_base = base_url,
    },
    .tools = NULL,
    .max_iterations = 10
});

const char *user_prompt = "Write a haiku about recursion in programming.";
ac_agent_result_t *result = ac_agent_run(agent, user_prompt);

printf("[assistant]:\n%s\n", result->content);

ac_session_close(session);
```
详细代码可参考 `examples/hosted/hello.c`

### 工具
只需要在工具函数前面加上`AC_TOOL_META`，并写好描述即可
```c
/**
 * @description: Get the current date and time
 */
AC_TOOL_META const char* get_current_time(void);


```
如上，就定义了一个工具函数，moc工具会自动将其转换成json schema 语法，agent可以调用
详细代码可参考 `examples/hosted/chat_tools.c`

### mcp
只需要编写json，填写mcp字段即可自动加载
```c
  "servers": [
    {
      "name": "context7",
      "url": "https://mcp.context7.com/mcp",
      "enabled": true
    },
 
```
如上，即可添加 `context7`作为mcp server，自动发现它支持的工具，并添加进tools list.

### skills
支持标准的skills发现，prompt注入，按需加载等功能。仅需将skill文件夹放在对应目录即可。尚未支持脚本运行（考虑跨平台问题）

## 复杂示例
在`extras`文件夹下面提供了两个完整的hosted的示例。
### arc-cli

一个基于 ArC 框架的轻量级 AI 命令行工具。展示如何使用 ArC 快速构建实用的 AI Agent。

### arc-coder
一个基于 Arc 框架构建的从coding agent。借用了OpenCode的提示词。提供了常用的`edit`,`grep`,`bash`等的工具用来实现编程功能。


## 构建
需要cmake和c语言编译器，目前我仅测试了gcc。

### Windows

#### 安装依赖

使用 vcpkg 安装 curl：

```powershell
vcpkg install curl:x64-windows
```

#### 方式1：使用 Ninja 生成器（推荐，支持 clangd）

```powershell
mkdir build && cd build
cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE=<vcpkg路径>/scripts/buildsystems/vcpkg.cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ..
cmake --build . --config Release
```

> 如果没有 Ninja，可以通过 `winget install Ninja-build.Ninja` 安装

#### 方式2：使用 Visual Studio 生成器

```powershell
mkdir build && cd build
cmake -DCMAKE_TOOLCHAIN_FILE=<vcpkg路径>/scripts/buildsystems/vcpkg.cmake ..
cmake --build . --config Release
```

### Linux/macOS

```bash
# 安装依赖 (Ubuntu/Debian)
sudo apt install libcurl4-openssl-dev

# 构建
mkdir build && cd build
cmake ..
make -j$(nproc)
```

## 运行示例

```bash
cd build/Release  # Windows
# 或
cd build          # Linux/macOS

# 设置环境变量（或创建 .env 文件）
# OPENAI_API_KEY=your-api-key
# OPENAI_BASE_URL=https://api.openai.com/v1  # 可选
# OPENAI_MODEL=gpt-4o-mini                   # 可选

# 运行 demo
./chat_demo
./chat_markdown
./chat_tools "计算 199*89"

...
```
更多示例，请参考 exmaples 以及 extras.
