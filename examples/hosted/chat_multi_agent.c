/**
 * @file chat_multi_agent.c
 * @brief Multi-Agent Parallel Execution Demo
 *
 * Demonstrates a "fan-out / fan-in" pattern:
 * 1. User inputs a word
 * 2. 10 expert agents analyze the word from different perspectives (parallel)
 * 3. A summary agent consolidates all insights
 *
 * This demo showcases:
 * - Multiple agents in one session
 * - Parallel agent execution using pthreads
 * - HTTP connection pool for efficient resource usage
 * - Agent coordination pattern
 *
 * Usage:
 *   1. Create .env file with OPENAI_API_KEY=sk-xxx
 *   2. Run ./chat_multi_agent
 *   3. Enter a word to analyze (e.g., "Apple", "Water", "Light")
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <arc.h>
#include <arc/env.h>
#include <arc/http_pool.h>

/*===========================================================================
 * Configuration
 *===========================================================================*/

#define NUM_EXPERTS     10
#define MAX_INPUT_LEN   256
#define MAX_OUTPUT_LEN  4096
#define EXPERT_TIMEOUT_MS   60000   /* 60s for each expert */
#define SUMMARY_TIMEOUT_MS  120000  /* 120s for summary (longer input) */

/*===========================================================================
 * Expert Definitions
 *===========================================================================*/

typedef struct {
    const char *name;
    const char *domain;
    const char *instructions;
} expert_def_t;

static const expert_def_t EXPERTS[NUM_EXPERTS] = {
    {
        .name = "ChineseExpert",
        .domain = "Chinese",
        .instructions =
            "You are a Chinese language expert. When given a word, analyze it from a Chinese linguistics perspective:\n"
            "- The literal and extended meanings of the word\n"
            "- Related idioms, poems, or literary allusions\n"
            "- Common usage in Chinese contexts\n"
            "Keep your response concise, under 100 words."
    },
    {
        .name = "MathExpert",
        .domain = "Mathematics",
        .instructions =
            "You are a mathematics expert. When given a word, analyze it from a mathematical perspective:\n"
            "- Relevant applications of this concept in mathematics\n"
            "- Related mathematical formulas or theorems\n"
            "- Interesting mathematical facts or numerical associations\n"
            "Keep your response concise, under 100 words."
    },
    {
        .name = "EnglishExpert",
        .domain = "English",
        .instructions =
            "You are an English language expert. When given a word, analyze it from an English linguistics perspective:\n"
            "- English equivalents and their etymology\n"
            "- Common English expressions and idioms\n"
            "- Meanings in English-speaking cultures\n"
            "Keep your response concise, under 100 words."
    },
    {
        .name = "PhysicsExpert",
        .domain = "Physics",
        .instructions =
            "You are a physicist. When given a word, analyze it from a physics perspective:\n"
            "- Related physical phenomena and principles\n"
            "- Physical laws involved\n"
            "- Interesting applications in physics\n"
            "Keep your response concise, under 100 words."
    },
    {
        .name = "ChemistryExpert",
        .domain = "Chemistry",
        .instructions =
            "You are a chemist. When given a word, analyze it from a chemistry perspective:\n"
            "- Related chemical components or molecular structures\n"
            "- Chemical reactions and properties\n"
            "- Applications in the field of chemistry\n"
            "Keep your response concise, under 100 words."
    },
    {
        .name = "BiologyExpert",
        .domain = "Biology",
        .instructions =
            "You are a biologist. When given a word, analyze it from a biological perspective:\n"
            "- Manifestations in the biological world\n"
            "- Related biological principles\n"
            "- Significance for life\n"
            "Keep your response concise, under 100 words."
    },
    {
        .name = "HistoryExpert",
        .domain = "History",
        .instructions =
            "You are a historian. When given a word, analyze it from a historical perspective:\n"
            "- Related historical events or figures\n"
            "- The process of historical evolution\n"
            "- Impact on human history\n"
            "Keep your response concise, under 100 words."
    },
    {
        .name = "GeographyExpert",
        .domain = "Geography",
        .instructions =
            "You are a geographer. When given a word, analyze it from a geographical perspective:\n"
            "- Geographical distribution characteristics\n"
            "- Relationships with terrain and climate\n"
            "- Interesting facts from a geographical perspective\n"
            "Keep your response concise, under 100 words."
    },
    {
        .name = "MusicExpert",
        .domain = "Music",
        .instructions =
            "You are a musician. When given a word, analyze it from a musical art perspective:\n"
            "- Related musical works or genres\n"
            "- Application in musical expression\n"
            "- Connections between music and this concept\n"
            "Keep your response concise, under 100 words."
    },
    {
        .name = "PhilosophyExpert",
        .domain = "Philosophy",
        .instructions =
            "You are a philosopher. When given a word, analyze it from a philosophical perspective:\n"
            "- Philosophical-level contemplations\n"
            "- Related philosophical concepts or schools\n"
            "- Deep meanings for human life\n"
            "Keep your response concise, under 100 words."
    }
};

/*===========================================================================
 * Worker Thread Data
 *===========================================================================*/

typedef struct {
    int id;
    ac_agent_t *agent;
    const char *input;
    char output[MAX_OUTPUT_LEN];
    int success;
    uint64_t duration_ms;
} worker_task_t;

/*===========================================================================
 * Worker Thread Function
 *===========================================================================*/

static void *worker_thread(void *arg) {
    worker_task_t *task = (worker_task_t *)arg;

    uint64_t start_ms = ac_platform_timestamp_ms();

    /* Run the agent */
    ac_agent_result_t *result = ac_agent_run(task->agent, task->input);

    task->duration_ms = ac_platform_timestamp_ms() - start_ms;

    if (result && result->content) {
        strncpy(task->output, result->content, MAX_OUTPUT_LEN - 1);
        task->output[MAX_OUTPUT_LEN - 1] = '\0';
        task->success = 1;
    } else {
        snprintf(task->output, MAX_OUTPUT_LEN, "[Agent %d failed to respond]", task->id);
        task->success = 0;
    }

    return NULL;
}

/*===========================================================================
 * Create Expert Agent
 *===========================================================================*/

static ac_agent_t *create_expert_agent(
    ac_session_t *session,
    const expert_def_t *expert,
    const char *model,
    const char *api_key,
    const char *base_url
) {
    return ac_agent_create(session, &(ac_agent_params_t){
        .name = expert->name,
        .instructions = expert->instructions,
        .llm = {
            .provider = "openai",
            .model = model,
            .api_key = api_key,
            .api_base = base_url,
            .timeout_ms = EXPERT_TIMEOUT_MS,
        },
        .tools = NULL,
        .max_iterations = 1  /* No tool calls, single response */
    });
}

/*===========================================================================
 * Create Summary Agent
 *===========================================================================*/

static ac_agent_t *create_summary_agent(
    ac_session_t *session,
    const char *model,
    const char *api_key,
    const char *base_url
) {
    return ac_agent_create(session, &(ac_agent_params_t){
        .name = "SummaryAgent",
        .instructions =
            "You are a knowledge synthesis expert. You will receive analyses from multiple domain experts about the same word.\n"
            "Please:\n"
            "1. Briefly summarize the core perspectives of each domain (1-2 sentences per domain)\n"
            "2. Identify 2-3 interesting cross-domain connections\n"
            "3. Provide a concise comprehensive summary (3-5 sentences)\n"
            "Keep the output concise, under 500 words.",
        .llm = {
            .provider = "openai",
            .model = model,
            .api_key = api_key,
            .api_base = base_url,
            .timeout_ms = SUMMARY_TIMEOUT_MS,  /* Longer timeout for summary */
        },
        .tools = NULL,
        .max_iterations = 1
    });
}

/*===========================================================================
 * Main
 *===========================================================================*/

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║        ArC Multi-Agent Parallel Execution Demo            ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");

    /* Load multi-level config */
    ac_env_load_verbose(NULL);

    const char *api_key = ac_env_require("OPENAI_API_KEY");
    if (!api_key) {
        ac_env_print_help("chat_multi_agent");
        return 1;
    }

    const char *base_url = ac_env_get("OPENAI_BASE_URL", NULL);
    const char *model = ac_env_get("OPENAI_MODEL", "gpt-4o-mini");

    printf("[+] Model: %s\n", model);
    printf("[+] Endpoint: %s\n", base_url ? base_url : "https://api.openai.com/v1");
    printf("[+] Experts: %d domains\n", NUM_EXPERTS);

    /* Initialize HTTP connection pool */
    printf("[+] Initializing HTTP connection pool...\n");
    arc_err_t err = ac_http_pool_init(&(ac_http_pool_config_t){
        .max_connections = NUM_EXPERTS + 2,  /* Experts + summary + buffer */
        .acquire_timeout_ms = SUMMARY_TIMEOUT_MS,  /* Use longer timeout */
    });
    if (err != ARC_OK) {
        fprintf(stderr, "[!] Failed to initialize HTTP pool: %s\n", ac_strerror(err));
        return 1;
    }

    /* Open session */
    ac_session_t *session = ac_session_open();
    if (!session) {
        fprintf(stderr, "[!] Failed to open session\n");
        ac_http_pool_shutdown();
        return 1;
    }

    /* Create expert agents */
    printf("[+] Creating %d expert agents...\n", NUM_EXPERTS);
    ac_agent_t *experts[NUM_EXPERTS] = {0};

    for (int i = 0; i < NUM_EXPERTS; i++) {
        experts[i] = create_expert_agent(session, &EXPERTS[i], model, api_key, base_url);
        if (!experts[i]) {
            fprintf(stderr, "[!] Failed to create expert: %s\n", EXPERTS[i].name);
            ac_session_close(session);
            ac_http_pool_shutdown();
            return 1;
        }
        printf("    [%d] %s (%s)\n", i + 1, EXPERTS[i].name, EXPERTS[i].domain);
    }

    /* Create summary agent */
    printf("[+] Creating summary agent...\n");
    ac_agent_t *summary_agent = create_summary_agent(session, model, api_key, base_url);
    if (!summary_agent) {
        fprintf(stderr, "[!] Failed to create summary agent\n");
        ac_session_close(session);
        ac_http_pool_shutdown();
        return 1;
    }

    printf("\n[Ready] Enter a word to analyze (or 'quit' to exit)\n\n");

    /* Main loop */
    char input[MAX_INPUT_LEN];
    int round = 0;

    while (1) {
        printf("Word> ");
        fflush(stdout);

        if (!fgets(input, sizeof(input), stdin)) {
            break;
        }

        /* Trim newline */
        size_t len = strlen(input);
        if (len > 0 && input[len - 1] == '\n') {
            input[--len] = '\0';
        }

        if (len == 0) continue;
        if (strcmp(input, "quit") == 0 || strcmp(input, "exit") == 0) break;

        round++;
        printf("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
        printf("  Analyzing: \"%s\" (round %d)\n", input, round);
        printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");

        /*
         * IMPORTANT: Recreate expert agents each round to prevent memory growth.
         * Expert agents are stateless - they don't need conversation history.
         * Destroying and recreating releases their arena memory.
         */
        if (round > 1) {
            /* Destroy previous expert agents to free memory */
            for (int i = 0; i < NUM_EXPERTS; i++) {
                if (experts[i]) {
                    ac_agent_destroy(experts[i]);
                    experts[i] = NULL;
                }
            }

            /* Recreate expert agents */
            for (int i = 0; i < NUM_EXPERTS; i++) {
                experts[i] = create_expert_agent(session, &EXPERTS[i], model, api_key, base_url);
                if (!experts[i]) {
                    fprintf(stderr, "[!] Failed to recreate expert: %s\n", EXPERTS[i].name);
                }
            }
        }

        /* Prepare worker tasks */
        pthread_t threads[NUM_EXPERTS];
        worker_task_t tasks[NUM_EXPERTS];

        char prompt[512];
        snprintf(prompt, sizeof(prompt), "请分析这个词：%s", input);

        uint64_t parallel_start = ac_platform_timestamp_ms();

        /* Launch all expert threads */
        printf("[Phase 1] Launching %d expert agents in parallel...\n", NUM_EXPERTS);

        for (int i = 0; i < NUM_EXPERTS; i++) {
            tasks[i].id = i;
            tasks[i].agent = experts[i];
            tasks[i].input = prompt;
            tasks[i].success = 0;
            tasks[i].output[0] = '\0';

            if (pthread_create(&threads[i], NULL, worker_thread, &tasks[i]) != 0) {
                fprintf(stderr, "[!] Failed to create thread for expert %d\n", i);
                tasks[i].success = 0;
                snprintf(tasks[i].output, MAX_OUTPUT_LEN, "[Thread creation failed]");
            }
        }

        /* Wait for all threads */
        printf("[Phase 2] Waiting for all experts to complete...\n");

        for (int i = 0; i < NUM_EXPERTS; i++) {
            pthread_join(threads[i], NULL);
        }

        uint64_t parallel_end = ac_platform_timestamp_ms();

        /* Print individual results */
        printf("\n[Phase 3] Expert Analysis Results:\n\n");

        int success_count = 0;
        size_t total_summary_len = 0;

        for (int i = 0; i < NUM_EXPERTS; i++) {
            printf("┌─ [%s] %s (%lums)\n",
                   EXPERTS[i].domain,
                   EXPERTS[i].name,
                   (unsigned long)tasks[i].duration_ms);
            printf("│  %s\n", tasks[i].output);
            printf("└─\n\n");

            if (tasks[i].success) {
                success_count++;
                total_summary_len += strlen(tasks[i].output) + 100;
            }
        }

        printf("[Stats] %d/%d experts responded, parallel time: %lums\n\n",
               success_count, NUM_EXPERTS, (unsigned long)(parallel_end - parallel_start));

        /* Build summary prompt */
        if (success_count > 0) {
            printf("[Phase 4] Generating comprehensive summary...\n\n");

            /* Recreate summary agent each round to free memory */
            if (round > 1) {
                ac_agent_destroy(summary_agent);
                summary_agent = create_summary_agent(session, model, api_key, base_url);
                if (!summary_agent) {
                    fprintf(stderr, "[!] Failed to recreate summary agent\n");
                    continue;
                }
            }

            size_t summary_prompt_size = total_summary_len + 1024;
            char *summary_prompt = malloc(summary_prompt_size);
            if (!summary_prompt) {
                fprintf(stderr, "[!] Memory allocation failed\n");
                continue;
            }

            snprintf(summary_prompt, summary_prompt_size,
                     "用户想要了解「%s」这个词。以下是各领域专家的分析：\n\n", input);

            for (int i = 0; i < NUM_EXPERTS; i++) {
                if (tasks[i].success) {
                    char section[MAX_OUTPUT_LEN + 128];
                    snprintf(section, sizeof(section),
                             "## %s专家\n%s\n\n", EXPERTS[i].domain, tasks[i].output);
                    strncat(summary_prompt, section, summary_prompt_size - strlen(summary_prompt) - 1);
                }
            }

            strncat(summary_prompt,
                    "\n请综合以上各领域的分析，给出一个全面而有深度的总结。",
                    summary_prompt_size - strlen(summary_prompt) - 1);

            uint64_t summary_start = ac_platform_timestamp_ms();

            ac_agent_result_t *result = ac_agent_run(summary_agent, summary_prompt);

            uint64_t summary_end = ac_platform_timestamp_ms();

            printf("╔══════════════════════════════════════════════════════════════╗\n");
            printf("║                      综合总结                                ║\n");
            printf("╚══════════════════════════════════════════════════════════════╝\n\n");

            if (result && result->content) {
                printf("%s\n", result->content);
            } else {
                printf("[Summary agent failed to respond]\n");
            }

            printf("\n[Stats] Summary generation time: %lums\n",
                   (unsigned long)(summary_end - summary_start));

            free(summary_prompt);
        }

        /* Print HTTP pool stats */
        ac_http_pool_stats_t stats;
        if (ac_http_pool_get_stats(&stats) == ARC_OK) {
            printf("[Pool] connections=%zu/%zu, hits=%llu, misses=%llu\n",
                   stats.active_connections, stats.max_connections,
                   (unsigned long long)stats.pool_hits,
                   (unsigned long long)stats.pool_misses);
        }

        printf("\n");
    }

    /* Cleanup */
    printf("\n[+] Cleaning up...\n");
    ac_session_close(session);
    ac_http_pool_shutdown();

    printf("[+] Goodbye!\n");
    return 0;
}
