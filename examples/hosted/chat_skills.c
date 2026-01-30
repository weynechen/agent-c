/**
 * @file chat_skills.c
 * @brief Skills system demo - Progressive skill loading
 *
 * This demo demonstrates the AgentC Skills system following
 * the agentskills.io specification.
 *
 * Features demonstrated:
 * - Skill discovery from directory
 * - Progressive loading (metadata first, content on enable)
 * - Discovery prompt generation
 * - Active prompt injection
 * - Dynamic skill enable/disable
 *
 * Usage:
 *   1. Create .env file with OPENAI_API_KEY=sk-xxx
 *   2. Run ./chat_skills
 *   3. Use /skills to list available skills
 *   4. Use /enable <skill-name> to activate a skill
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <agentc.h>
#include <agentc/skills.h>
#include "agentc/log.h"
#include "dotenv.h"
#include "platform_wrap.h"

#define MAX_INPUT_LEN 4096
#define SKILLS_DIR "skills"

static volatile int g_running = 1;

static void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
    printf("\n[Interrupted]\n");
}

static void print_usage(void) {
    printf("\nCommands:\n");
    printf("  /help              - Show this help\n");
    printf("  /skills            - List all discovered skills\n");
    printf("  /enable <name>     - Enable a skill\n");
    printf("  /disable <name>    - Disable a skill\n");
    printf("  /enable-all        - Enable all skills\n");
    printf("  /disable-all       - Disable all skills\n");
    printf("  /active            - Show enabled skills\n");
    printf("  /discovery         - Show discovery prompt\n");
    printf("  /prompt            - Show active prompt\n");
    printf("  /clear             - Clear conversation (new agent)\n");
    printf("  /quit              - Exit\n\n");
}

static void print_skills_list(const ac_skills_t *skills) {
    printf("\n=== Discovered Skills ===\n");
    
    size_t count = ac_skills_count(skills);
    if (count == 0) {
        printf("No skills discovered.\n");
        printf("Make sure the 'skills/' directory exists with skill subdirectories.\n\n");
        return;
    }
    
    const ac_skill_t *skill = ac_skills_list(skills);
    while (skill) {
        const char *state_str;
        switch (skill->state) {
            case AC_SKILL_ENABLED:
                state_str = "[ENABLED]";
                break;
            case AC_SKILL_DISABLED:
                state_str = "[disabled]";
                break;
            default:
                state_str = "[discovered]";
                break;
        }
        
        printf("  %s %s\n", state_str, skill->meta.name);
        printf("    %s\n", skill->meta.description);
        
        if (skill->meta.allowed_tools && skill->meta.allowed_tools_count > 0) {
            printf("    Tools: ");
            for (size_t i = 0; i < skill->meta.allowed_tools_count; i++) {
                printf("%s%s", skill->meta.allowed_tools[i],
                       i < skill->meta.allowed_tools_count - 1 ? ", " : "");
            }
            printf("\n");
        }
        
        skill = skill->next;
    }
    
    printf("\nTotal: %zu skills (%zu enabled)\n\n", 
           count, ac_skills_enabled_count(skills));
}

static void print_active_skills(const ac_skills_t *skills) {
    printf("\n=== Active Skills ===\n");
    
    size_t enabled = ac_skills_enabled_count(skills);
    if (enabled == 0) {
        printf("No skills enabled.\n");
        printf("Use /enable <skill-name> to activate a skill.\n\n");
        return;
    }
    
    const ac_skill_t *skill = ac_skills_list(skills);
    while (skill) {
        if (skill->state == AC_SKILL_ENABLED) {
            printf("  - %s\n", skill->meta.name);
        }
        skill = skill->next;
    }
    printf("\nTotal: %zu enabled\n\n", enabled);
}

/**
 * @brief Build combined system prompt with skills
 */
static char *build_system_prompt(
    const char *base_prompt,
    ac_skills_t *skills
) {
    /* Get discovery and active prompts */
    char *discovery = ac_skills_build_discovery_prompt(skills);
    char *active = ac_skills_build_active_prompt(skills);
    
    /* Calculate total size */
    size_t base_len = base_prompt ? strlen(base_prompt) : 0;
    size_t discovery_len = discovery ? strlen(discovery) : 0;
    size_t active_len = active ? strlen(active) : 0;
    
    size_t total = base_len + discovery_len + active_len + 4; /* +4 for newlines */
    
    char *prompt = malloc(total);
    if (!prompt) {
        free(discovery);
        free(active);
        return NULL;
    }
    
    char *p = prompt;
    
    /* Base prompt */
    if (base_prompt) {
        memcpy(p, base_prompt, base_len);
        p += base_len;
        *p++ = '\n';
        *p++ = '\n';
    }
    
    /* Discovery prompt (always included) */
    if (discovery) {
        memcpy(p, discovery, discovery_len);
        p += discovery_len;
        *p++ = '\n';
    }
    
    /* Active prompt (enabled skills) */
    if (active) {
        memcpy(p, active, active_len);
        p += active_len;
    }
    
    *p = '\0';
    
    free(discovery);
    free(active);
    
    return prompt;
}

/**
 * @brief Create agent with current skill configuration
 */
static ac_agent_t *create_agent(
    ac_session_t *session,
    ac_skills_t *skills,
    const char *model,
    const char *api_key,
    const char *base_url
) {
    const char *base_prompt = 
        "You are a helpful coding assistant. "
        "You have access to various skills that can help you perform specialized tasks. "
        "When a task matches a skill's description, use the instructions from that skill.";
    
    char *system_prompt = build_system_prompt(base_prompt, skills);
    if (!system_prompt) {
        AC_LOG_ERROR("Failed to build system prompt");
        return NULL;
    }

    AC_LOG_INFO("system_prompt:%s\n", system_prompt);
    
    ac_agent_t *agent = ac_agent_create(session, &(ac_agent_params_t){
        .name = "SkillsBot",
        .instructions = system_prompt,
        .llm = {
            .provider = "openai",
            .model = model,
            .api_key = api_key,
            .api_base = base_url,
        },
        .tools = NULL,
        .max_iterations = 10
    });
    
    free(system_prompt);
    return agent;
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    
    /* Initialize platform-specific terminal settings */
    platform_init_terminal(NULL);
    
    /* Load environment from .env file */
    if (env_load(".", false) == 0) {
        printf("[Loaded .env file]\n");
    } else {
        printf("[No .env file found, using environment variables]\n");
    }
    
    /* Get API key from environment */
    const char *api_key = getenv("OPENAI_API_KEY");
    if (!api_key || strlen(api_key) == 0) {
        AC_LOG_ERROR("OPENAI_API_KEY not set");
        AC_LOG_ERROR("Create a .env file with: OPENAI_API_KEY=sk-xxx");
        platform_cleanup_terminal();
        return 1;
    }
    
    /* Optional: custom base URL and model */
    const char *base_url = getenv("OPENAI_BASE_URL");
    const char *model = getenv("OPENAI_MODEL");
    if (!model) {
        model = "gpt-3.5-turbo";
    }
    
    /* Setup signal handler */
    signal(SIGINT, signal_handler);
    
    /* Create skills manager and discover skills */
    ac_skills_t *skills = ac_skills_create();
    if (!skills) {
        AC_LOG_ERROR("Failed to create skills manager");
        platform_cleanup_terminal();
        return 1;
    }
    
    /* Discover skills from directory */
    ac_skills_discover_dir(skills, SKILLS_DIR);
    
    printf("\n=== AgentC Skills Demo ===\n");
    printf("Model: %s\n", model);
    printf("Endpoint: %s\n", base_url ? base_url : "https://api.openai.com/v1");
    printf("Skills discovered: %zu\n", ac_skills_count(skills));
    printf("Type /help for commands, /skills to list skills\n\n");
    
    /* Open session */
    ac_session_t *session = ac_session_open();
    if (!session) {
        AC_LOG_ERROR("Failed to open session");
        ac_skills_destroy(skills);
        platform_cleanup_terminal();
        return 1;
    }
    
    /* Create agent */
    ac_agent_t *agent = create_agent(session, skills, model, api_key, base_url);
    if (!agent) {
        AC_LOG_ERROR("Failed to create agent");
        ac_session_close(session);
        ac_skills_destroy(skills);
        platform_cleanup_terminal();
        return 1;
    }
    
    char input[MAX_INPUT_LEN];
    
    while (g_running) {
        printf("You: ");
        fflush(stdout);
        
        if (!fgets(input, sizeof(input), stdin)) {
            break;
        }
        
        /* Remove trailing newline */
        size_t len = strlen(input);
        if (len > 0 && input[len - 1] == '\n') {
            input[--len] = '\0';
        }
        
        /* Skip empty input */
        if (len == 0) {
            continue;
        }
        
        /* Handle commands */
        if (input[0] == '/') {
            if (strcmp(input, "/quit") == 0 || strcmp(input, "/exit") == 0) {
                break;
            } else if (strcmp(input, "/help") == 0) {
                print_usage();
                continue;
            } else if (strcmp(input, "/skills") == 0) {
                print_skills_list(skills);
                continue;
            } else if (strcmp(input, "/active") == 0) {
                print_active_skills(skills);
                continue;
            } else if (strncmp(input, "/enable ", 8) == 0) {
                const char *name = input + 8;
                if (ac_skills_enable(skills, name) == AGENTC_OK) {
                    printf("[Enabled skill: %s]\n", name);
                    /* Recreate agent with new prompt */
                    ac_agent_destroy(agent);
                    agent = create_agent(session, skills, model, api_key, base_url);
                    if (!agent) {
                        AC_LOG_ERROR("Failed to recreate agent");
                        break;
                    }
                } else {
                    printf("[Skill not found: %s]\n", name);
                }
                continue;
            } else if (strncmp(input, "/disable ", 9) == 0) {
                const char *name = input + 9;
                if (ac_skills_disable(skills, name) == AGENTC_OK) {
                    printf("[Disabled skill: %s]\n", name);
                    /* Recreate agent with new prompt */
                    ac_agent_destroy(agent);
                    agent = create_agent(session, skills, model, api_key, base_url);
                    if (!agent) {
                        AC_LOG_ERROR("Failed to recreate agent");
                        break;
                    }
                } else {
                    printf("[Skill not found: %s]\n", name);
                }
                continue;
            } else if (strcmp(input, "/enable-all") == 0) {
                size_t n = ac_skills_enable_all(skills);
                printf("[Enabled %zu skills]\n", n);
                /* Recreate agent */
                ac_agent_destroy(agent);
                agent = create_agent(session, skills, model, api_key, base_url);
                if (!agent) {
                    AC_LOG_ERROR("Failed to recreate agent");
                    break;
                }
                continue;
            } else if (strcmp(input, "/disable-all") == 0) {
                ac_skills_disable_all(skills);
                printf("[Disabled all skills]\n");
                /* Recreate agent */
                ac_agent_destroy(agent);
                agent = create_agent(session, skills, model, api_key, base_url);
                if (!agent) {
                    AC_LOG_ERROR("Failed to recreate agent");
                    break;
                }
                continue;
            } else if (strcmp(input, "/discovery") == 0) {
                char *discovery = ac_skills_build_discovery_prompt(skills);
                if (discovery) {
                    printf("\n--- Discovery Prompt ---\n%s--- End ---\n\n", discovery);
                    free(discovery);
                } else {
                    printf("[No skills discovered]\n");
                }
                continue;
            } else if (strcmp(input, "/prompt") == 0) {
                char *active = ac_skills_build_active_prompt(skills);
                if (active) {
                    printf("\n--- Active Prompt ---\n%s--- End ---\n\n", active);
                    free(active);
                } else {
                    printf("[No skills enabled]\n");
                }
                continue;
            } else if (strcmp(input, "/clear") == 0) {
                ac_agent_destroy(agent);
                agent = create_agent(session, skills, model, api_key, base_url);
                if (!agent) {
                    AC_LOG_ERROR("Failed to recreate agent");
                    break;
                }
                printf("[Conversation cleared - new agent created]\n");
                continue;
            } else {
                printf("[Unknown command: %s]\n", input);
                continue;
            }
        }
        
        printf("Assistant: ");
        fflush(stdout);
        
        /* Run agent */
        ac_agent_result_t *result = ac_agent_run(agent, input);
        
        if (result && result->content) {
            printf("%s\n", result->content);
        } else {
            printf("[No response from agent]\n");
        }
        
        printf("\n");
    }
    
    /* Cleanup */
    ac_session_close(session);
    ac_skills_destroy(skills);
    platform_cleanup_terminal();
    
    printf("Goodbye!\n");
    return 0;
}
