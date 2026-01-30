/**
 * @file chat_skills.c
 * @brief Skills system demo - Agent loads skills via tool
 *
 * This demo demonstrates the ArC Skills system with skill tool.
 * The Agent can dynamically load skills by calling the 'skill' tool.
 *
 * Features demonstrated:
 * - Skill discovery from directory
 * - Skill tool for Agent to load skills dynamically
 * - Progressive loading (Agent decides when to load skills)
 *
 * Usage:
 *   1. Create .env file with OPENAI_API_KEY=sk-xxx
 *   2. Run ./chat_skills
 *   3. Ask the Agent to help with a task matching a skill
 *   4. Agent will call skill tool to load relevant instructions
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <arc.h>
#include <arc/skills.h>
#include "dotenv.h"
#include "platform_wrap.h"

#define MAX_INPUT_LEN 4096
#define SKILLS_DIR "skills"

static volatile int g_running = 1;
static ac_tool_t *g_skill_tool = NULL;

static void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
    printf("\n[Interrupted]\n");
}

static void print_usage(void) {
    printf("\nCommands:\n");
    printf("  /help              - Show this help\n");
    printf("  /skills            - List all discovered skills\n");
    printf("  /tool-desc         - Show skill tool description\n");
    printf("  /clear             - Clear conversation (new agent)\n");
    printf("  /quit              - Exit\n\n");
    printf("The Agent will automatically load skills when needed.\n");
    printf("Try asking: 'Help me review this code' or 'Debug this error'\n\n");
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
                state_str = "[LOADED]";
                break;
            case AC_SKILL_DISABLED:
                state_str = "[disabled]";
                break;
            default:
                state_str = "[available]";
                break;
        }

        printf("  %s %s\n", state_str, skill->meta.name);
        printf("    %s\n", skill->meta.description);

        skill = skill->next;
    }

    printf("\nTotal: %zu skills (Agent can load via 'skill' tool)\n\n", count);
}

/**
 * @brief Build system prompt with available skills metadata
 *
 * Following agentskills.io specification:
 * - Inject <available_skills> into system prompt
 * - Use skill tool to load full instructions
 */
static char *build_system_prompt(ac_skills_t *skills) {
    const char *base_prompt =
        "You are a helpful coding assistant.\n\n"
        "You have access to specialized skills that provide detailed instructions "
        "for specific tasks. When a user's request matches an available skill, "
        "use the 'skill' tool to load the full instructions before proceeding.\n\n";

    /* Build available_skills XML for system prompt */
    char *skills_xml = ac_skills_build_discovery_prompt(skills);

    if (!skills_xml) {
        return strdup(base_prompt);
    }

    /* Combine base prompt + skills list */
    size_t base_len = strlen(base_prompt);
    size_t skills_len = strlen(skills_xml);
    size_t total = base_len + skills_len + 1;

    char *prompt = malloc(total);
    if (!prompt) {
        free(skills_xml);
        return NULL;
    }

    memcpy(prompt, base_prompt, base_len);
    memcpy(prompt + base_len, skills_xml, skills_len);
    prompt[total - 1] = '\0';

    free(skills_xml);
    return prompt;
}

/**
 * @brief Create agent with skill tool
 */
static ac_agent_t *create_agent(
    ac_session_t *session,
    ac_skills_t *skills,
    const char *model,
    const char *api_key,
    const char *base_url
) {
    /* Create tool registry */
    ac_tool_registry_t *tools = ac_tool_registry_create(session);
    if (!tools) {
        AC_LOG_ERROR("Failed to create tool registry");
        return NULL;
    }

    /* Create and register skill tool */
    g_skill_tool = ac_skills_create_tool(skills);
    if (!g_skill_tool) {
        AC_LOG_ERROR("Failed to create skill tool");
        return NULL;
    }

    ac_tool_registry_add(tools, g_skill_tool);

    /* Build system prompt with available skills (per agentskills.io spec) */
    char *system_prompt = build_system_prompt(skills);
    if (!system_prompt) {
        AC_LOG_ERROR("Failed to build system prompt");
        return NULL;
    }

    AC_LOG_DEBUG("System prompt:\n%s", system_prompt);

    ac_agent_t *agent = ac_agent_create(session, &(ac_agent_params_t){
        .name = "SkillsBot",
        .instructions = system_prompt,
        .llm = {
            .provider = "openai",
            .model = model,
            .api_key = api_key,
            .api_base = base_url,
        },
        .tools = tools,
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
        model = "gpt-4o-mini";  /* Need a model that supports tool calling */
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

    printf("\n=== ArC Skills Demo (Tool Mode) ===\n");
    printf("Model: %s\n", model);
    printf("Endpoint: %s\n", base_url ? base_url : "https://api.openai.com/v1");
    printf("Skills discovered: %zu\n", ac_skills_count(skills));
    printf("Agent has 'skill' tool to load skills on demand.\n");
    printf("Type /help for commands, /skills to list available skills\n\n");

    /* Open session */
    ac_session_t *session = ac_session_open();
    if (!session) {
        AC_LOG_ERROR("Failed to open session");
        ac_skills_destroy(skills);
        platform_cleanup_terminal();
        return 1;
    }

    /* Create agent with skill tool */
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
            } else if (strcmp(input, "/tool-desc") == 0) {
                char *desc = ac_skills_build_tool_description(skills);
                if (desc) {
                    printf("\n--- Skill Tool Description ---\n%s\n--- End ---\n\n", desc);
                    free(desc);
                }
                continue;
            } else if (strcmp(input, "/clear") == 0) {
                /* Destroy old agent and create new one */
                if (g_skill_tool) {
                    ac_skills_destroy_tool(g_skill_tool);
                    g_skill_tool = NULL;
                }
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

        /* Run agent - it will use skill tool if needed */
        ac_agent_result_t *result = ac_agent_run(agent, input);

        if (result && result->content) {
            printf("%s\n", result->content);
        } else {
            printf("[No response from agent]\n");
        }

        printf("\n");
    }

    /* Cleanup */
    if (g_skill_tool) {
        ac_skills_destroy_tool(g_skill_tool);
    }
    ac_session_close(session);
    ac_skills_destroy(skills);
    platform_cleanup_terminal();

    printf("Goodbye!\n");
    return 0;
}
