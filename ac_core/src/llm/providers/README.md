# Custom LLM Providers

This directory is reserved for custom LLM provider implementations.

## Built-in Providers

AgentC includes the following built-in providers in the parent directory:

- **openai_api.c**: OpenAI-compatible API implementation
  - Supports: OpenAI, DeepSeek, 通义千问, 智谱AI, and other OpenAI-compatible services
  - Configuration: Set `api_base` to the appropriate endpoint
  
- **anthropic_api.c**: Anthropic Claude API implementation
  - Supports: Claude models via Anthropic API

## Adding Custom Providers

To add a custom provider:

1. Create a new `.c` file in this directory (e.g., `custom_provider.c`)

2. Implement the provider interface:

```c
#include "agentc/llm.h"

// Provider-specific initialization
ac_llm_t* custom_provider_create(const ac_llm_config_t* config) {
    // Implementation
}

// Provider-specific request handling
int custom_provider_chat(ac_llm_t* llm, const char* request_json, char** response) {
    // Implementation
}

// Provider-specific cleanup
void custom_provider_destroy(ac_llm_t* llm) {
    // Implementation
}
```

3. Register your provider in `llm.c`:

```c
// In llm_create(), add provider detection logic
if (strstr(config->api_base, "custom-provider.com")) {
    return custom_provider_create(config);
}
```

4. Update your build system (CMakeLists.txt) to include the new provider file

## Provider Interface Guidelines

- **Thread Safety**: Provider implementations should be thread-safe
- **Memory Management**: Always free allocated resources in the destroy function
- **Error Handling**: Use AC_LOG_ERROR() for error reporting
- **Configuration**: Respect all fields in `ac_llm_config_t`

## Examples

See the built-in providers (`openai_api.c`, `anthropic_api.c`) for implementation examples.
