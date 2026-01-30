/**
 * @file test_skills.c
 * @brief Simple test for Skills system (no LLM required)
 *
 * Tests skill discovery, parsing, enable/disable, and prompt generation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <agentc/skills.h>
#include <agentc/log.h>

#define SKILLS_DIR "skills"
#define GREEN "\033[32m"
#define RED "\033[31m"
#define RESET "\033[0m"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) printf("  Testing: %s ... ", name)
#define PASS() do { printf(GREEN "PASS" RESET "\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf(RED "FAIL" RESET " (%s)\n", msg); tests_failed++; } while(0)

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    
    printf("\n=== Skills System Test ===\n\n");
    
    /* Test 1: Create skills manager */
    printf("[1] Skills Manager Creation\n");
    TEST("ac_skills_create");
    ac_skills_t *skills = ac_skills_create();
    if (skills) {
        PASS();
    } else {
        FAIL("returned NULL");
        return 1;
    }
    
    /* Test 2: Discover skills from directory */
    printf("\n[2] Skill Discovery\n");
    TEST("ac_skills_discover_dir");
    agentc_err_t err = ac_skills_discover_dir(skills, SKILLS_DIR);
    if (err == AGENTC_OK) {
        PASS();
    } else {
        FAIL("returned error");
    }
    
    TEST("skill count > 0");
    size_t count = ac_skills_count(skills);
    if (count > 0) {
        printf(GREEN "PASS" RESET " (found %zu skills)\n", count);
        tests_passed++;
    } else {
        FAIL("no skills found");
    }
    
    /* Test 3: List skills */
    printf("\n[3] Skill Listing\n");
    const ac_skill_t *skill = ac_skills_list(skills);
    while (skill) {
        printf("  Found: %s\n", skill->meta.name);
        printf("    Description: %.60s...\n", skill->meta.description);
        printf("    State: %s\n", 
               skill->state == AC_SKILL_DISCOVERED ? "discovered" :
               skill->state == AC_SKILL_ENABLED ? "enabled" : "disabled");
        if (skill->meta.allowed_tools_count > 0) {
            printf("    Allowed tools: ");
            for (size_t i = 0; i < skill->meta.allowed_tools_count; i++) {
                printf("%s ", skill->meta.allowed_tools[i]);
            }
            printf("\n");
        }
        skill = skill->next;
    }
    
    /* Test 4: Find skill by name */
    printf("\n[4] Skill Lookup\n");
    TEST("ac_skills_find(code-review)");
    const ac_skill_t *found = ac_skills_find(skills, "code-review");
    if (found && strcmp(found->meta.name, "code-review") == 0) {
        PASS();
    } else {
        FAIL("skill not found");
    }
    
    TEST("ac_skills_find(nonexistent)");
    found = ac_skills_find(skills, "nonexistent-skill");
    if (found == NULL) {
        PASS();
    } else {
        FAIL("should return NULL");
    }
    
    /* Test 5: Enable/disable skills */
    printf("\n[5] Skill Enable/Disable\n");
    TEST("ac_skills_enable(code-review)");
    err = ac_skills_enable(skills, "code-review");
    if (err == AGENTC_OK && ac_skills_enabled_count(skills) == 1) {
        PASS();
    } else {
        FAIL("enable failed");
    }
    
    TEST("skill content loaded after enable");
    found = ac_skills_find(skills, "code-review");
    if (found && found->content && strlen(found->content) > 0) {
        printf(GREEN "PASS" RESET " (%zu bytes)\n", strlen(found->content));
        tests_passed++;
    } else {
        FAIL("content not loaded");
    }
    
    TEST("ac_skills_enable(debugging)");
    err = ac_skills_enable(skills, "debugging");
    if (err == AGENTC_OK && ac_skills_enabled_count(skills) == 2) {
        PASS();
    } else {
        FAIL("enable failed");
    }
    
    TEST("ac_skills_disable(code-review)");
    err = ac_skills_disable(skills, "code-review");
    if (err == AGENTC_OK && ac_skills_enabled_count(skills) == 1) {
        PASS();
    } else {
        FAIL("disable failed");
    }
    
    TEST("ac_skills_enable_all");
    size_t enabled = ac_skills_enable_all(skills);
    if (enabled == count) {
        printf(GREEN "PASS" RESET " (enabled %zu)\n", enabled);
        tests_passed++;
    } else {
        FAIL("not all enabled");
    }
    
    TEST("ac_skills_disable_all");
    ac_skills_disable_all(skills);
    if (ac_skills_enabled_count(skills) == 0) {
        PASS();
    } else {
        FAIL("not all disabled");
    }
    
    /* Test 6: Discovery prompt generation */
    printf("\n[6] Discovery Prompt Generation\n");
    TEST("ac_skills_build_discovery_prompt");
    char *discovery = ac_skills_build_discovery_prompt(skills);
    if (discovery && strstr(discovery, "<available_skills>") && 
        strstr(discovery, "<name>code-review</name>") && strstr(discovery, "<name>debugging</name>")) {
        printf(GREEN "PASS" RESET " (%zu bytes)\n", strlen(discovery));
        tests_passed++;
        
        /* Print preview */
        printf("\n  --- Discovery Prompt Preview ---\n");
        printf("%.500s", discovery);
        if (strlen(discovery) > 500) printf("...\n");
        printf("  --- End Preview ---\n");
    } else {
        FAIL("invalid prompt");
    }
    free(discovery);
    
    /* Test 7: Active prompt generation */
    printf("\n[7] Active Prompt Generation\n");
    TEST("empty when no skills enabled");
    char *active = ac_skills_build_active_prompt(skills);
    if (active == NULL) {
        PASS();
    } else {
        FAIL("should be NULL");
        free(active);
    }
    
    /* Enable a skill and test */
    ac_skills_enable(skills, "code-review");
    
    TEST("ac_skills_build_active_prompt");
    active = ac_skills_build_active_prompt(skills);
    if (active && strstr(active, "<active-skills>") && 
        strstr(active, "<skill name=\"code-review\">")) {
        printf(GREEN "PASS" RESET " (%zu bytes)\n", strlen(active));
        tests_passed++;
        
        /* Print preview */
        printf("\n  --- Active Prompt Preview ---\n");
        printf("%.800s", active);
        if (strlen(active) > 800) printf("...\n");
        printf("  --- End Preview ---\n");
    } else {
        FAIL("invalid prompt");
    }
    free(active);
    
    /* Test 8: Skill Tool */
    printf("\n[8] Skill Tool\n");
    
    /* Re-create skills for tool test */
    ac_skills_disable_all(skills);
    
    TEST("ac_skills_create_tool");
    ac_tool_t *tool = ac_skills_create_tool(skills);
    if (tool && tool->name && strcmp(tool->name, "skill") == 0) {
        PASS();
    } else {
        FAIL("tool creation failed");
    }
    
    TEST("tool description contains available_skills");
    if (tool && tool->description && 
        strstr(tool->description, "<available_skills>") &&
        strstr(tool->description, "code-review")) {
        printf(GREEN "PASS" RESET " (%zu bytes)\n", strlen(tool->description));
        tests_passed++;
        
        printf("\n  --- Skill Tool Description ---\n");
        printf("%.600s", tool->description);
        if (strlen(tool->description) > 600) printf("...\n");
        printf("\n  --- End ---\n");
    } else {
        FAIL("invalid description");
    }
    
    TEST("tool execute - load skill");
    if (tool && tool->execute) {
        char *result = tool->execute(NULL, "{\"name\": \"code-review\"}", tool->priv);
        if (result && strstr(result, "## Skill: code-review")) {
            printf(GREEN "PASS" RESET " (%zu bytes)\n", strlen(result));
            tests_passed++;
            
            printf("\n  --- Tool Execute Result Preview ---\n");
            printf("%.400s", result);
            if (strlen(result) > 400) printf("...\n");
            printf("\n  --- End ---\n");
        } else {
            FAIL("unexpected result");
        }
        free(result);
    } else {
        FAIL("tool has no execute function");
    }
    
    TEST("tool execute - skill not found");
    if (tool && tool->execute) {
        char *result = tool->execute(NULL, "{\"name\": \"nonexistent\"}", tool->priv);
        if (result && strstr(result, "error") && strstr(result, "not found")) {
            PASS();
        } else {
            FAIL("should return error");
        }
        free(result);
    } else {
        FAIL("tool has no execute function");
    }
    
    TEST("ac_skills_destroy_tool");
    ac_skills_destroy_tool(tool);
    PASS();
    
    /* Cleanup */
    printf("\n[9] Cleanup\n");
    TEST("ac_skills_destroy");
    ac_skills_destroy(skills);
    PASS();
    
    /* Summary */
    printf("\n=== Test Summary ===\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    printf("Result: %s\n\n", tests_failed == 0 ? GREEN "ALL TESTS PASSED" RESET : RED "SOME TESTS FAILED" RESET);
    
    return tests_failed > 0 ? 1 : 0;
}
