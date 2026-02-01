# ArC

An Agent library written in C. Supports both hosted (Windows/Linux/macOS) and RTOS environments, requiring only POSIX Thread and HTTPS connection dependencies.

Note: This project is under rapid development. Many features are not yet complete, and APIs may change. Currently only tested on Linux (Ubuntu & Arch). Windows and macOS are not fully tested yet.

[中文文档](docs/README_CN.md)

## Features

### Core
Cross-platform features:
- [x] Basic agent (prompt/llm/message manager): Supports OpenAI-compatible API, multi-turn conversations, ReACT loop.
- [x] Tools: Provides moc tool, just write normal functions with comments.
- [x] MCP: Supports streaming HTTP and SSE in MCP client.
- [x] Tracing: Callbacks at key points of the agent, with convenient JSON log export.

### Hosted
The following features are only supported on hosted platforms (Windows/Linux/macOS). (Some features could theoretically work on RTOS with a filesystem, but embedded systems usually have limited resources, so they are not considered.)
- [x] Sandbox: Detects dangerous commands, supports Linux and macOS, Windows not yet supported.
- [-] Skills
- [x] TUI
- [x] Markdown rendering
- [ ] Memory persistence
- [x] Connection pool: Foundation for future agent swarms.

## Usage

### Basic
A simple chat example:

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

For detailed code, see `examples/hosted/hello.c`

### Tools
Just add `AC_TOOL_META` before the tool function and write a good description:

```c
/**
 * @description: Get the current date and time
 */
AC_TOOL_META const char* get_current_time(void);
```

As shown above, a tool function is defined. The moc tool will automatically convert it to JSON schema syntax that the agent can call.

For detailed code, see `examples/hosted/chat_tools.c`

### MCP
Just write JSON with MCP fields to auto-load:

```json
{
  "servers": [
    {
      "name": "context7",
      "url": "https://mcp.context7.com/mcp",
      "enabled": true
    }
  ]
}
```

As shown above, you can add `context7` as an MCP server, automatically discover its supported tools, and add them to the tools list.

### Skills
Supports standard skill discovery, prompt injection, and on-demand loading. Just place the skill folder in the corresponding directory. Script execution is not yet supported (considering cross-platform issues).

## Complex Examples

Two complete hosted examples are provided in the `extras` folder.

### arc-cli

A lightweight AI command-line tool based on the ArC framework. Demonstrates how to quickly build practical AI agents using ArC.

### arc-coder

A coding agent built on the ArC framework. Uses OpenCode's prompts. Provides common tools like `edit`, `grep`, `bash`, etc. for programming functionality.

## Build

Requires cmake and a C compiler. Currently only tested with GCC.

### Windows

#### Install Dependencies

Install curl using vcpkg:

```powershell
vcpkg install curl:x64-windows
```

#### Option 1: Using Ninja Generator (Recommended, supports clangd)

```powershell
mkdir build && cd build
cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE=<vcpkg-path>/scripts/buildsystems/vcpkg.cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ..
cmake --build . --config Release
```

> If you don't have Ninja, install it via `winget install Ninja-build.Ninja`

#### Option 2: Using Visual Studio Generator

```powershell
mkdir build && cd build
cmake -DCMAKE_TOOLCHAIN_FILE=<vcpkg-path>/scripts/buildsystems/vcpkg.cmake ..
cmake --build . --config Release
```

### Linux/macOS

```bash
# Install dependencies (Ubuntu/Debian)
sudo apt install libcurl4-openssl-dev

# Build
mkdir build && cd build
cmake ..
make -j$(nproc)
```

## Run Examples

```bash
cd build/Release  # Windows
# or
cd build          # Linux/macOS

# Set environment variables (or create a .env file)
# OPENAI_API_KEY=your-api-key
# OPENAI_BASE_URL=https://api.openai.com/v1  # Optional
# OPENAI_MODEL=gpt-4o-mini                   # Optional

# Run demos
./chat_demo
./chat_markdown
./chat_tools "compute 199*89"

...
```

For more examples, please refer to `examples` and `extras`.
