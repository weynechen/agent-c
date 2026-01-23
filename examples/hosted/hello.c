/**
 * @file hello.c
 * @brief Minimal AgentC example
 *
 * Usage:
 *   1. Create .env file with OPENAI_API_KEY=sk-xxx
 *   2. Run ./hello
 */

#include <stdio.h>
#include <stdlib.h>
#include "agentc/session.h"
#include "agentc/agent.h"
#include "agentc/log.h"
#include "dotenv.h"

int main(void) {
    env_load(".", false);
    
    const char *api_key = getenv("OPENAI_API_KEY");
    const char *model = getenv_default("OPENAI_MODEL", "gpt-3.5-turbo");
    const char *base_url = getenv_default("OPENAI_BASE_URL", NULL);

    if (!api_key) {
        AC_LOG_ERROR("Error: OPENAI_API_KEY not set\n");
        return 1;
    }
    
    ac_session_t* session = ac_session_open();
    if (!session) {
        AC_LOG_ERROR("Failed to open session\n");
        return 1;
    }
    
    ac_agent_t* agent = ac_agent_create(session, &(ac_agent_params_t){
        .name = "HelloBot",
        .instructions = "You are a friendly assistant.",
        .llm_params = {
            .provider = "openai",
            .model = model,
            .api_key = api_key,
            .api_base = base_url,
        },
        .tools_name = NULL,
        .max_iterations = 10
    });
    
    if (!agent) {
        AC_LOG_ERROR("Failed to create agent\n");
        ac_session_close(session);
        return 1;
    }
    
    const char *user_prompt = "Write a haiku about recursion in programming.";
    ac_agent_result_t* result = ac_agent_run_sync(agent, user_prompt);
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
