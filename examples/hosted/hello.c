/**
 * @file hello.c
 * @brief Minimal ArC example
 *
 * Usage:
 *   1. Create .env file with OPENAI_API_KEY=sk-xxx
 *   2. Run ./hello
 */

#include <stdio.h>
#include <stdlib.h>
#include <arc.h>
#include <arc/env.h>

int main(void) {
    ac_env_load_verbose(NULL);

    const char *api_key = ac_env_require("OPENAI_API_KEY");
    const char *model = ac_env_get("OPENAI_MODEL", "gpt-3.5-turbo");
    const char *base_url = ac_env_get("OPENAI_BASE_URL", NULL);

    if (!api_key) {
        ac_env_print_help("hello");
        return 1;
    }

    ac_session_t *session = ac_session_open();
    if (!session) {
        AC_LOG_ERROR("Failed to open session");
        return 1;
    }

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

    if (!agent) {
        AC_LOG_ERROR("Failed to create agent");
        ac_session_close(session);
        return 1;
    }

    const char *user_prompt = "Write a haiku about recursion in programming.";
    ac_agent_result_t *result = ac_agent_run(agent, user_prompt);

    printf("----------------------\n");
    printf("[user]:\n%s\n\n", user_prompt);
    if (result && result->content) {
        printf("[assistant]:\n%s\n", result->content);
    } else {
        printf("No response from agent\n");
    }

    printf("----------------------\n");

    ac_session_close(session);

    return 0;
}
