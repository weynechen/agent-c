/**
 * @file rules.h
 * @brief Rules Management System (Hosted Feature)
 *
 * Load and manage coding rules from configuration files.
 * This is a hosted feature requiring filesystem access.
 */

#ifndef AGENTC_HOSTED_RULES_H
#define AGENTC_HOSTED_RULES_H

#include <agentc/platform.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Rule Definition
 *============================================================================*/

typedef struct ac_rule {
    char *name;                        /* Rule name */
    char *content;                     /* Rule content */
    int priority;                      /* Priority (higher = more important) */
    struct ac_rule *next;
} ac_rule_t;

/*============================================================================
 * Rules Manager
 *============================================================================*/

typedef struct ac_rules ac_rules_t;

/**
 * @brief Create rules manager
 *
 * @return Rules manager handle, NULL on error
 */
ac_rules_t *ac_rules_create(void);

/**
 * @brief Load rules from directory
 *
 * Scans directory for .yaml/.yml/.txt files and loads rules.
 * Rules can be in plain text or YAML format.
 *
 * @param rules     Rules manager
 * @param rules_dir Rules directory path
 * @return AGENTC_OK on success
 */
agentc_err_t ac_rules_load_dir(
    ac_rules_t *rules,
    const char *rules_dir
);

/**
 * @brief Load a single rule file
 *
 * @param rules     Rules manager
 * @param filepath  Rule file path
 * @return AGENTC_OK on success
 */
agentc_err_t ac_rules_load_file(
    ac_rules_t *rules,
    const char *filepath
);

/**
 * @brief Add a rule manually
 *
 * @param rules    Rules manager
 * @param name     Rule name
 * @param content  Rule content
 * @param priority Rule priority (default: 0)
 * @return AGENTC_OK on success
 */
agentc_err_t ac_rules_add(
    ac_rules_t *rules,
    const char *name,
    const char *content,
    int priority
);

/**
 * @brief Build system prompt with all rules
 *
 * Generates a system prompt string by combining all rules
 * in priority order.
 *
 * @param rules        Rules manager
 * @param base_prompt  Base system prompt (optional, can be NULL)
 * @return System prompt string (caller must free), NULL on error
 */
char *ac_rules_build_prompt(
    ac_rules_t *rules,
    const char *base_prompt
);

/**
 * @brief Get rule count
 *
 * @param rules  Rules manager
 * @return Number of loaded rules
 */
size_t ac_rules_count(const ac_rules_t *rules);

/**
 * @brief Get all rules as linked list
 *
 * @param rules  Rules manager
 * @return Head of rule list (do not free, owned by rules manager)
 */
const ac_rule_t *ac_rules_list(const ac_rules_t *rules);

/**
 * @brief Clear all rules
 *
 * @param rules  Rules manager
 */
void ac_rules_clear(ac_rules_t *rules);

/**
 * @brief Destroy rules manager
 *
 * @param rules  Rules manager to destroy
 */
void ac_rules_destroy(ac_rules_t *rules);

#ifdef __cplusplus
}
#endif

#endif /* AGENTC_HOSTED_RULES_H */
