/**
 * @file chat_git_commit.c
 * @brief Generate git commit messages using Conventional Commits skill
 *
 * This demo generates commit messages following the Conventional Commits
 * specification using the AI agent with the conventional-commits skill.
 *
 * Usage:
 *   1. Create .env file with OPENAI_API_KEY=sk-xxx
 *   2. Stage your changes with `git add`
 *   3. Run ./chat_git_commit /path/to/repo
 *   4. The tool will output a suggested commit message
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arc.h>
#include <arc/skills.h>
#include "dotenv.h"

#define MAX_DIFF_SIZE 65536
#define SKILLS_DIR "skills"

/**
 * @brief Execute a shell command and capture output
 *
 * @param command  Command to execute
 * @param output   Buffer to store output
 * @param max_size Maximum output buffer size
 * @return 0 on success, -1 on failure
 */
static int exec_command(const char *command, char *output, size_t max_size) {
    FILE *fp = popen(command, "r");
    if (!fp) {
        return -1;
    }

    size_t total_read = 0;
    char buffer[256];

    output[0] = '\0';

    while (fgets(buffer, sizeof(buffer), fp) != NULL) {
        size_t len = strlen(buffer);
        if (total_read + len + 1 > max_size) {
            /* Truncate if too large */
            break;
        }
        strcpy(output + total_read, buffer);
        total_read += len;
    }

    int status = pclose(fp);
    return status == 0 ? 0 : -1;
}

/**
 * @brief Get git diff for staged changes
 *
 * @param repo_path  Path to git repository
 * @param diff       Buffer to store diff output
 * @param max_size   Maximum buffer size
 * @return 0 on success, -1 on failure
 */
static int get_git_diff(const char *repo_path, char *diff, size_t max_size) {
    char command[2048];

    /* First, check if there are staged changes */
    snprintf(command, sizeof(command),
             "cd \"%s\" && git diff --cached --stat 2>/dev/null", repo_path);

    char stat_output[1024] = {0};
    if (exec_command(command, stat_output, sizeof(stat_output)) != 0) {
        fprintf(stderr, "Error: Failed to run git diff in %s\n", repo_path);
        return -1;
    }

    if (strlen(stat_output) == 0) {
        /* No staged changes, try to get unstaged changes */
        snprintf(command, sizeof(command),
                 "cd \"%s\" && git diff --stat 2>/dev/null", repo_path);
        if (exec_command(command, stat_output, sizeof(stat_output)) != 0) {
            fprintf(stderr, "Error: Failed to run git diff\n");
            return -1;
        }

        if (strlen(stat_output) == 0) {
            fprintf(stderr, "No changes detected. Please stage changes with 'git add' first.\n");
            return -1;
        }

        /* Get full unstaged diff */
        snprintf(command, sizeof(command),
                 "cd \"%s\" && git diff 2>/dev/null", repo_path);
        printf("Note: No staged changes found, showing unstaged changes.\n\n");
    } else {
        /* Get full staged diff */
        snprintf(command, sizeof(command),
                 "cd \"%s\" && git diff --cached 2>/dev/null", repo_path);
    }

    return exec_command(command, diff, max_size);
}

/**
 * @brief Build system prompt with skill instructions
 */
static char *build_system_prompt(ac_skills_t *skills) {
    const char *base_prompt =
        "You are a git commit message generator. Your task is to analyze "
        "the git diff provided and generate a commit message following the "
        "Conventional Commits specification.\n\n"
        "Rules:\n"
        "1. Output ONLY the commit message, nothing else\n"
        "2. The message should accurately describe the changes\n"
        "3. Use the appropriate type based on the nature of changes\n"
        "4. Keep the subject line under 50 characters if possible\n"
        "5. Add a body if the changes are complex\n\n";

    /* Enable conventional-commits skill and get active prompt */
    arc_err_t err = ac_skills_enable(skills, "conventional-commits");
    if (err != ARC_OK) {
        AC_LOG_WARN("conventional-commits skill not found, using basic prompt");
        return strdup(base_prompt);
    }

    char *skill_prompt = ac_skills_build_active_prompt(skills);
    if (!skill_prompt) {
        return strdup(base_prompt);
    }

    /* Combine prompts */
    size_t base_len = strlen(base_prompt);
    size_t skill_len = strlen(skill_prompt);
    size_t total = base_len + skill_len + 1;

    char *prompt = malloc(total);
    if (!prompt) {
        free(skill_prompt);
        return NULL;
    }

    memcpy(prompt, base_prompt, base_len);
    memcpy(prompt + base_len, skill_prompt, skill_len);
    prompt[total - 1] = '\0';

    free(skill_prompt);
    return prompt;
}

static void print_usage(const char *program) {
    printf("Usage: %s <path-to-git-repo>\n\n", program);
    printf("Generate a Conventional Commits formatted commit message based on git diff.\n\n");
    printf("Options:\n");
    printf("  <path-to-git-repo>  Path to the git repository (default: current directory)\n");
    printf("\nExamples:\n");
    printf("  %s .\n", program);
    printf("  %s /path/to/my/project\n", program);
}

int main(int argc, char *argv[]) {
    const char *repo_path = ".";

    /* Parse arguments */
    if (argc > 1) {
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        repo_path = argv[1];
    }

    /* Load environment from .env file */
    if (env_load(".", false) == 0) {
        printf("[Loaded .env file]\n");
    }

    /* Get API key from environment */
    const char *api_key = getenv("OPENAI_API_KEY");
    if (!api_key || strlen(api_key) == 0) {
        AC_LOG_ERROR("OPENAI_API_KEY not set");
        AC_LOG_ERROR("Create a .env file with: OPENAI_API_KEY=sk-xxx");
        return 1;
    }

    /* Optional: custom base URL and model */
    const char *base_url = getenv("OPENAI_BASE_URL");
    const char *model = getenv("OPENAI_MODEL");
    if (!model) {
        model = "gpt-4o-mini";
    }

    printf("=== Git Commit Message Generator ===\n");
    printf("Repository: %s\n", repo_path);
    printf("Model: %s\n\n", model);

    /* Get git diff */
    char *diff = malloc(MAX_DIFF_SIZE);
    if (!diff) {
        AC_LOG_ERROR("Failed to allocate memory for diff");
        return 1;
    }

    if (get_git_diff(repo_path, diff, MAX_DIFF_SIZE) != 0) {
        free(diff);
        return 1;
    }

    if (strlen(diff) == 0) {
        printf("No changes detected.\n");
        free(diff);
        return 0;
    }

    /* Show diff stats */
    char stat_cmd[2048];
    char stat_output[4096] = {0};
    snprintf(stat_cmd, sizeof(stat_cmd),
             "cd \"%s\" && git diff --cached --stat 2>/dev/null || git diff --stat 2>/dev/null",
             repo_path);
    exec_command(stat_cmd, stat_output, sizeof(stat_output));

    printf("Changes detected:\n%s\n", stat_output);

    /* Create skills manager and discover skills */
    ac_skills_t *skills = ac_skills_create();
    if (!skills) {
        AC_LOG_ERROR("Failed to create skills manager");
        free(diff);
        return 1;
    }

    /* Discover skills from directory */
    ac_skills_discover_dir(skills, SKILLS_DIR);

    /* Build system prompt with skill */
    char *system_prompt = build_system_prompt(skills);
    if (!system_prompt) {
        AC_LOG_ERROR("Failed to build system prompt");
        ac_skills_destroy(skills);
        free(diff);
        return 1;
    }

    /* Open session */
    ac_session_t *session = ac_session_open();
    if (!session) {
        AC_LOG_ERROR("Failed to open session");
        free(system_prompt);
        ac_skills_destroy(skills);
        free(diff);
        return 1;
    }

    /* Create agent */
    ac_agent_t *agent = ac_agent_create(session, &(ac_agent_params_t){
        .name = "CommitBot",
        .instructions = system_prompt,
        .llm = {
            .provider = "openai",
            .model = model,
            .api_key = api_key,
            .api_base = base_url,
        },
        .tools = NULL,
        .max_iterations = 1
    });

    free(system_prompt);

    if (!agent) {
        AC_LOG_ERROR("Failed to create agent");
        ac_session_close(session);
        ac_skills_destroy(skills);
        free(diff);
        return 1;
    }

    /* Build user prompt with diff */
    size_t prompt_size = strlen(diff) + 256;
    char *user_prompt = malloc(prompt_size);
    if (!user_prompt) {
        AC_LOG_ERROR("Failed to allocate memory for prompt");
        ac_session_close(session);
        ac_skills_destroy(skills);
        free(diff);
        return 1;
    }

    snprintf(user_prompt, prompt_size,
             "Generate a commit message for the following changes:\n\n"
             "```diff\n%s\n```", diff);

    free(diff);

    /* Run agent */
    printf("Generating commit message...\n\n");
    ac_agent_result_t *result = ac_agent_run(agent, user_prompt);

    free(user_prompt);

    if (result && result->content) {
        printf("=== Suggested Commit Message ===\n\n");
        printf("%s\n", result->content);
        printf("\n================================\n");
        printf("\nTip: Use 'git commit -m \"<message>\"' to commit with this message.\n");
    } else {
        printf("Failed to generate commit message.\n");
    }

    /* Cleanup */
    ac_session_close(session);
    ac_skills_destroy(skills);

    return 0;
}
