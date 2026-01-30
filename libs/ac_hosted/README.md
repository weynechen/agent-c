# AgentC Hosted Features

This directory contains features that require a full operating system environment (Linux/Windows/macOS/Server).

## Features

### 1. Rules System (`src/rules/`)
**Status**: ✅ Implemented

Manage coding rules and constraints that are injected into the system prompt.

**API**:
```c
#include <agentc/rules.h>

ac_rules_t *rules = ac_rules_create();
ac_rules_load_dir(rules, ".code-agentc/rules");
char *prompt = ac_rules_build_prompt(rules, base_prompt);
```

**Features**:
- Load rules from directory (`.yaml`, `.yml`, `.txt` files)
- Priority-based rule ordering
- Automatic system prompt generation

### 2. Skills System (`src/skills/`)
**Status**: ✅ Implemented (Phase 1)

Progressive skill loading following [agentskills.io](https://agentskills.io) specification.
Skills are discovered from directories containing `SKILL.md` files.

**API**:
```c
#include <agentc/skills.h>

ac_skills_t *skills = ac_skills_create();

// Discover skills (loads metadata only)
ac_skills_discover_dir(skills, ".code-agentc/skills");

// Build discovery prompt for system message
char *discovery = ac_skills_build_discovery_prompt(skills);

// Enable specific skill (loads full content)
ac_skills_enable(skills, "pdf-processing");

// Build active prompt with full instructions
char *active = ac_skills_build_active_prompt(skills);

// Validate skill's allowed_tools against registry
ac_skills_validate_tools(skills, "pdf-processing", tool_registry);

// Cleanup
free(discovery);
free(active);
ac_skills_destroy(skills);
```

**Features**:
- Progressive disclosure: discover metadata first, load content on demand
- Parse SKILL.md frontmatter (name, description, license, compatibility, allowed-tools)
- **Skill Tool**: Agent can load skills dynamically via tool call
- Enable/disable skills programmatically
- Generate discovery prompt (skill list) and active prompt (full instructions)
- Validate allowed_tools against tool registry
- Script execution interface (reserved, not yet implemented)

**Skill Tool Usage**:
```c
// Create skill tool for Agent
ac_tool_t *skill_tool = ac_skills_create_tool(skills);
ac_tool_registry_add(registry, skill_tool);

// Agent can now call: skill({"name": "code-review"})
// Returns full skill instructions

// Cleanup
ac_skills_destroy_tool(skill_tool);
```

### 3. MCP Client (`src/mcp/`)
**Status**: ⚠️ Stub Implementation

Model Context Protocol client for integrating with MCP servers.

**API**:
```c
#include <agentc/mcp.h>

ac_mcp_client_t *client = ac_mcp_create(&(ac_mcp_config_t){
    .server_url = "http://localhost:3000",
    .transport = "http",
});

ac_mcp_connect(client);
ac_mcp_discover_tools(client);
ac_mcp_register_tools(client, tool_registry);
```

**Features** (TODO):
- JSON-RPC 2.0 over HTTP/SSE
- Tool discovery and invocation
- Resource access
- Prompt templates
- Sampling (AI model requests)

### 4. Markdown Renderer (`src/markdown/`)
**Status**: ✅ Implemented

Terminal markdown rendering with syntax highlighting.

### 5. DotEnv (`components/dotenv/`)
**Status**: ✅ Implemented

Environment variable loading from `.env` files.

## Usage in Applications

```c
#include <agentc.h>           /* Core AgentC */
#include <agentc/rules.h>     /* Hosted: Rules */
#include <agentc/skills.h>    /* Hosted: Skills */
#include <agentc/mcp.h>       /* Hosted: MCP */

int main() {
    /* Load rules */
    ac_rules_t *rules = ac_rules_create();
    ac_rules_load_dir(rules, ".rules");
    char *prompt = ac_rules_build_prompt(rules, "Base prompt");
    
    /* Create LLM with rules */
    ac_llm_t *llm = ac_llm_create(&(ac_llm_params_t){
        .model = "gpt-4",
        .instructions = prompt,  /* ← Rules injected here */
        ...
    });
    
    /* Load skills and MCP */
    ac_skills_t *skills = ac_skills_create();
    ac_skills_load_dir(skills, ".skills");
    
    ac_mcp_client_t *mcp = ac_mcp_create(...);
    ac_mcp_connect(mcp);
    
    /* Register tools */
    ac_tools_t *tools = ac_tools_create();
    /* ... register local tools ... */
    ac_mcp_register_tools(mcp, tools);  /* Add MCP tools */
    
    /* Create agent */
    ac_agent_t *agent = ac_agent_create(&(ac_agent_params_t){
        .llm = llm,
        .tools = tools,  /* Contains both local and MCP tools */
        ...
    });
    
    /* Run agent */
    ac_agent_run_sync(agent, "User input", &result);
    
    /* Cleanup */
    ac_agent_destroy(agent);
    ac_mcp_destroy(mcp);
    ac_skills_destroy(skills);
    ac_rules_destroy(rules);
    free(prompt);
}
```

## Design Principles

1. **No Core Modification**: All hosted features work on top of agentc_core without modifying it
2. **Optional**: Applications can use only the features they need
3. **Composable**: Features work independently or together
4. **Clear Separation**: Hosted features require OS capabilities that embedded systems may not have

## File Structure

```
ac_hosted/
├── include/agentc/
│   ├── rules.h          # Rules management API
│   ├── skills.h         # Skills management API
│   └── ...
├── src/
│   ├── rules/
│   │   └── rules.c      # Rules implementation
│   ├── skills/
│   │   ├── skills.c          # Skills manager
│   │   ├── skill_parser.c    # SKILL.md frontmatter parser
│   │   ├── skill_prompt.c    # Prompt generation
│   │   └── skills_internal.h # Internal structures
│   ├── sandbox/         # Process sandboxing
│   └── markdown/        # Markdown renderer
└── ...
```

## Implementation Status

| Feature | Status | Lines | Completeness |
|---------|--------|-------|--------------|
| Rules | ✅ Complete | ~350 | 100% |
| Skills | ✅ Phase 1 | ~800 | 90% |
| MCP | ⚠️ Stub | ~200 | 10% |
| Markdown | ✅ Complete | ~2000 | 100% |
| DotEnv | ✅ Complete | ~200 | 100% |

## TODO

### Rules System
- [x] Basic file loading
- [x] Priority ordering
- [x] System prompt building
- [ ] YAML parsing (currently plain text)
- [ ] Rule templates

### Skills System
- [x] Basic structure
- [x] Enable/disable skills
- [x] YAML frontmatter parsing (simple parser)
- [x] Tool validation (allowed_tools)
- [x] Discovery prompt generation
- [x] Active prompt generation
- [x] **Skill Tool** - Agent loads skills via tool call
- [ ] Script execution (reserved interface)
- [ ] Auto-match activation by task description

### MCP Client
- [ ] HTTP transport
- [ ] SSE transport
- [ ] JSON-RPC 2.0
- [ ] Tool discovery
- [ ] Tool invocation
- [ ] Resource access
- [ ] Prompt templates
- [ ] Sampling support

## Dependencies

- **agentc_core**: Required (core agent framework)
- **libcurl**: Required for HTTP (MCP client)
- **pcre2**: Required for markdown rendering
- **yaml-parser** (future): For YAML configuration parsing
