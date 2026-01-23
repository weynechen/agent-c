/**
 * @file builtin_tools.c
 * @brief Built-in Tools Implementation
 */

#include "builtin_tools.h"
#include <agentc/log.h>
#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

/*============================================================================
 * Utility Functions
 *============================================================================*/

/* Check if command is dangerous */
static int is_dangerous_command(const char *cmd) {
    const char *dangerous[] = {
        "rm -rf", "rm -fr", "sudo", "chmod 777", "chmod -R 777",
        "> /dev/", "mkfs", "dd if=", ":(){ :|:& };:",
        "mv /* ", "mv / ", NULL
    };
    
    for (int i = 0; dangerous[i] != NULL; i++) {
        if (strstr(cmd, dangerous[i]) != NULL) {
            return 1;
        }
    }
    return 0;
}

/* Execute command and capture output */
static char *execute_command(const char *cmd, int *exit_code) {
    char buffer[256];
    char *result = NULL;
    size_t result_len = 0;
    size_t result_cap = 1024;
    
    result = malloc(result_cap);
    if (!result) return NULL;
    result[0] = '\0';
    
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        free(result);
        return NULL;
    }
    
    while (fgets(buffer, sizeof(buffer), fp) != NULL) {
        size_t len = strlen(buffer);
        if (result_len + len + 1 > result_cap) {
            result_cap *= 2;
            char *new_result = realloc(result, result_cap);
            if (!new_result) {
                free(result);
                pclose(fp);
                return NULL;
            }
            result = new_result;
        }
        strcpy(result + result_len, buffer);
        result_len += len;
    }
    
    *exit_code = pclose(fp);
    return result;
}

/*============================================================================
 * Tool: shell_execute
 *============================================================================*/

static agentc_err_t tool_shell_execute(
    const cJSON *args,
    char **output,
    void *user_data
) {
    int safe_mode = *(int *)user_data;
    
    cJSON *cmd_json = cJSON_GetObjectItem(args, "command");
    if (!cmd_json || !cJSON_IsString(cmd_json)) {
        *output = strdup("{\"error\": \"command parameter is required\"}");
        return AGENTC_OK;
    }
    
    const char *cmd = cmd_json->valuestring;
    
    /* Safety check */
    if (safe_mode && is_dangerous_command(cmd)) {
        char msg[512];
        snprintf(msg, sizeof(msg),
            "{\"error\": \"Dangerous command blocked in safe mode\", \"command\": \"%s\"}",
            cmd);
        *output = strdup(msg);
        return AGENTC_OK;
    }
    
    /* Execute command */
    int exit_code;
    char *result = execute_command(cmd, &exit_code);
    
    if (!result) {
        *output = strdup("{\"error\": \"Failed to execute command\"}");
        return AGENTC_OK;
    }
    
    /* Build JSON response */
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "command", cmd);
    cJSON_AddNumberToObject(resp, "exit_code", WEXITSTATUS(exit_code));
    cJSON_AddStringToObject(resp, "output", result);
    
    free(result);
    
    *output = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);
    
    return AGENTC_OK;
}

/*============================================================================
 * Tool: read_file
 *============================================================================*/

static agentc_err_t tool_read_file(
    const cJSON *args,
    char **output,
    void *user_data
) {
    (void)user_data;
    
    cJSON *path_json = cJSON_GetObjectItem(args, "path");
    if (!path_json || !cJSON_IsString(path_json)) {
        *output = strdup("{\"error\": \"path parameter is required\"}");
        return AGENTC_OK;
    }
    
    const char *path = path_json->valuestring;
    
    /* Open file */
    FILE *fp = fopen(path, "r");
    if (!fp) {
        char msg[512];
        snprintf(msg, sizeof(msg), "{\"error\": \"Failed to open file\", \"path\": \"%s\"}", path);
        *output = strdup(msg);
        return AGENTC_OK;
    }
    
    /* Get file size */
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    /* Limit file size to 100KB for safety */
    if (size > 100 * 1024) {
        fclose(fp);
        *output = strdup("{\"error\": \"File too large (max 100KB)\"}");
        return AGENTC_OK;
    }
    
    /* Read file */
    char *content = malloc(size + 1);
    if (!content) {
        fclose(fp);
        *output = strdup("{\"error\": \"Out of memory\"}");
        return AGENTC_OK;
    }
    
    size_t read_size = fread(content, 1, size, fp);
    content[read_size] = '\0';
    fclose(fp);
    
    /* Build response */
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "path", path);
    cJSON_AddNumberToObject(resp, "size", (double)read_size);
    cJSON_AddStringToObject(resp, "content", content);
    
    free(content);
    
    *output = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);
    
    return AGENTC_OK;
}

/*============================================================================
 * Tool: write_file
 *============================================================================*/

static agentc_err_t tool_write_file(
    const cJSON *args,
    char **output,
    void *user_data
) {
    (void)user_data;
    
    cJSON *path_json = cJSON_GetObjectItem(args, "path");
    cJSON *content_json = cJSON_GetObjectItem(args, "content");
    
    if (!path_json || !cJSON_IsString(path_json)) {
        *output = strdup("{\"error\": \"path parameter is required\"}");
        return AGENTC_OK;
    }
    
    if (!content_json || !cJSON_IsString(content_json)) {
        *output = strdup("{\"error\": \"content parameter is required\"}");
        return AGENTC_OK;
    }
    
    const char *path = path_json->valuestring;
    const char *content = content_json->valuestring;
    
    /* Write file */
    FILE *fp = fopen(path, "w");
    if (!fp) {
        char msg[512];
        snprintf(msg, sizeof(msg), "{\"error\": \"Failed to open file for writing\", \"path\": \"%s\"}", path);
        *output = strdup(msg);
        return AGENTC_OK;
    }
    
    size_t written = fwrite(content, 1, strlen(content), fp);
    fclose(fp);
    
    /* Build response */
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "path", path);
    cJSON_AddNumberToObject(resp, "bytes_written", (double)written);
    cJSON_AddBoolToObject(resp, "success", 1);
    
    *output = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);
    
    return AGENTC_OK;
}

/*============================================================================
 * Tool: list_directory
 *============================================================================*/

static agentc_err_t tool_list_directory(
    const cJSON *args,
    char **output,
    void *user_data
) {
    (void)user_data;
    
    cJSON *path_json = cJSON_GetObjectItem(args, "path");
    if (!path_json || !cJSON_IsString(path_json)) {
        *output = strdup("{\"error\": \"path parameter is required\"}");
        return AGENTC_OK;
    }
    
    const char *path = path_json->valuestring;
    
    /* Open directory */
    DIR *dir = opendir(path);
    if (!dir) {
        char msg[512];
        snprintf(msg, sizeof(msg), "{\"error\": \"Failed to open directory\", \"path\": \"%s\"}", path);
        *output = strdup(msg);
        return AGENTC_OK;
    }
    
    /* Build file list */
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "path", path);
    cJSON *files = cJSON_CreateArray();
    
    struct dirent *entry;
    int count = 0;
    while ((entry = readdir(dir)) != NULL && count < 1000) {
        /* Skip . and .. */
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        cJSON *file_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(file_obj, "name", entry->d_name);
        
        /* Get file type */
        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
        struct stat st;
        if (stat(full_path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                cJSON_AddStringToObject(file_obj, "type", "directory");
            } else if (S_ISREG(st.st_mode)) {
                cJSON_AddStringToObject(file_obj, "type", "file");
                cJSON_AddNumberToObject(file_obj, "size", (double)st.st_size);
            } else {
                cJSON_AddStringToObject(file_obj, "type", "other");
            }
        }
        
        cJSON_AddItemToArray(files, file_obj);
        count++;
    }
    
    closedir(dir);
    
    cJSON_AddItemToObject(resp, "files", files);
    cJSON_AddNumberToObject(resp, "count", count);
    
    *output = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);
    
    return AGENTC_OK;
}

/*============================================================================
 * Tool: get_current_time
 *============================================================================*/

static agentc_err_t tool_get_current_time(
    const cJSON *args,
    char **output,
    void *user_data
) {
    (void)args;
    (void)user_data;
    
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    
    char date_buf[64];
    char time_buf[64];
    strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", tm_info);
    strftime(time_buf, sizeof(time_buf), "%H:%M:%S", tm_info);
    
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "date", date_buf);
    cJSON_AddStringToObject(resp, "time", time_buf);
    cJSON_AddStringToObject(resp, "timezone", "local");
    cJSON_AddNumberToObject(resp, "timestamp", (double)now);
    
    *output = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);
    
    return AGENTC_OK;
}

/*============================================================================
 * Tool: calculator
 *============================================================================*/

static agentc_err_t tool_calculator(
    const cJSON *args,
    char **output,
    void *user_data
) {
    (void)user_data;
    
    cJSON *op_json = cJSON_GetObjectItem(args, "operation");
    cJSON *a_json = cJSON_GetObjectItem(args, "a");
    cJSON *b_json = cJSON_GetObjectItem(args, "b");
    
    if (!op_json || !cJSON_IsString(op_json)) {
        *output = strdup("{\"error\": \"operation parameter is required\"}");
        return AGENTC_OK;
    }
    
    if (!a_json || !cJSON_IsNumber(a_json)) {
        *output = strdup("{\"error\": \"a parameter is required (number)\"}");
        return AGENTC_OK;
    }
    
    if (!b_json || !cJSON_IsNumber(b_json)) {
        *output = strdup("{\"error\": \"b parameter is required (number)\"}");
        return AGENTC_OK;
    }
    
    const char *op = op_json->valuestring;
    double a = a_json->valuedouble;
    double b = b_json->valuedouble;
    double result = 0;
    int error = 0;
    
    if (strcmp(op, "add") == 0 || strcmp(op, "+") == 0) {
        result = a + b;
    } else if (strcmp(op, "subtract") == 0 || strcmp(op, "-") == 0) {
        result = a - b;
    } else if (strcmp(op, "multiply") == 0 || strcmp(op, "*") == 0) {
        result = a * b;
    } else if (strcmp(op, "divide") == 0 || strcmp(op, "/") == 0) {
        if (b == 0) {
            *output = strdup("{\"error\": \"Division by zero\"}");
            return AGENTC_OK;
        }
        result = a / b;
    } else if (strcmp(op, "power") == 0 || strcmp(op, "^") == 0) {
        result = pow(a, b);
    } else if (strcmp(op, "mod") == 0 || strcmp(op, "%") == 0) {
        result = fmod(a, b);
    } else {
        *output = strdup("{\"error\": \"Unknown operation. Supported: add, subtract, multiply, divide, power, mod\"}");
        return AGENTC_OK;
    }
    
    if (error) {
        *output = strdup("{\"error\": \"Calculation error\"}");
        return AGENTC_OK;
    }
    
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "operation", op);
    cJSON_AddNumberToObject(resp, "a", a);
    cJSON_AddNumberToObject(resp, "b", b);
    cJSON_AddNumberToObject(resp, "result", result);
    
    *output = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);
    
    return AGENTC_OK;
}

/*============================================================================
 * Tool Registration
 *============================================================================*/

ac_tools_t *builtin_tools_create(int safe_mode) {
    ac_tools_t *tools = ac_tools_create();
    if (!tools) return NULL;
    
    /* Store safe_mode flag for tool callbacks */
    static int s_safe_mode;
    s_safe_mode = safe_mode;
    
    /* Tool: shell_execute */
    {
        ac_param_t *params = ac_param_create("command", AC_PARAM_STRING,
            "Shell command to execute", 1);
        
        ac_tool_t tool = {
            .name = "shell_execute",
            .description = "Execute a shell command and return output. Use for system operations, file management, git commands, etc.",
            .parameters = params,
            .handler = tool_shell_execute,
            .user_data = &s_safe_mode,
        };
        ac_tool_register(tools, &tool);
        ac_param_free(params);
    }
    
    /* Tool: read_file */
    {
        ac_param_t *params = ac_param_create("path", AC_PARAM_STRING,
            "Path to file to read", 1);
        
        ac_tool_t tool = {
            .name = "read_file",
            .description = "Read the contents of a file. Returns file content as string.",
            .parameters = params,
            .handler = tool_read_file,
        };
        ac_tool_register(tools, &tool);
        ac_param_free(params);
    }
    
    /* Tool: write_file */
    {
        ac_param_t *params = NULL;
        
        ac_param_t *path = ac_param_create("path", AC_PARAM_STRING,
            "Path to file to write", 1);
        ac_param_append(&params, path);
        
        ac_param_t *content = ac_param_create("content", AC_PARAM_STRING,
            "Content to write to file", 1);
        ac_param_append(&params, content);
        
        ac_tool_t tool = {
            .name = "write_file",
            .description = "Write content to a file. Creates new file or overwrites existing.",
            .parameters = params,
            .handler = tool_write_file,
        };
        ac_tool_register(tools, &tool);
        ac_param_free(params);
    }
    
    /* Tool: list_directory */
    {
        ac_param_t *params = ac_param_create("path", AC_PARAM_STRING,
            "Path to directory to list", 1);
        
        ac_tool_t tool = {
            .name = "list_directory",
            .description = "List files and directories in a directory. Returns array of file info.",
            .parameters = params,
            .handler = tool_list_directory,
        };
        ac_tool_register(tools, &tool);
        ac_param_free(params);
    }
    
    /* Tool: get_current_time */
    {
        ac_tool_t tool = {
            .name = "get_current_time",
            .description = "Get the current date and time",
            .parameters = NULL,
            .handler = tool_get_current_time,
        };
        ac_tool_register(tools, &tool);
    }
    
    /* Tool: calculator */
    {
        ac_param_t *params = NULL;
        
        ac_param_t *op = ac_param_create("operation", AC_PARAM_STRING,
            "Operation: add, subtract, multiply, divide, power, mod (or +, -, *, /, ^, %)", 1);
        op->enum_values = strdup("add,subtract,multiply,divide,power,mod");
        ac_param_append(&params, op);
        
        ac_param_t *a = ac_param_create("a", AC_PARAM_NUMBER,
            "First operand", 1);
        ac_param_append(&params, a);
        
        ac_param_t *b = ac_param_create("b", AC_PARAM_NUMBER,
            "Second operand", 1);
        ac_param_append(&params, b);
        
        ac_tool_t tool = {
            .name = "calculator",
            .description = "Perform arithmetic calculations. Supports add, subtract, multiply, divide, power, mod.",
            .parameters = params,
            .handler = tool_calculator,
        };
        ac_tool_register(tools, &tool);
        ac_param_free(params);
    }
    
    return tools;
}
