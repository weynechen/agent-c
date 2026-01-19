/**
 * @file file_tools.c
 * @brief File Operation Tools
 */

#include "tools.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>

/*============================================================================
 * Tool: read_file
 *============================================================================*/

static agentc_err_t tool_read_file(
    const struct cJSON *args,
    char **output,
    void *user_data
) {
    /* Get arguments */
    const struct cJSON *path_json = cJSON_GetObjectItem(args, "path");
    if (!path_json || !cJSON_IsString(path_json)) {
        *output = strdup("{\"error\": \"Missing or invalid 'path' argument\"}");
        return AGENTC_ERR_INVALID_ARG;
    }
    
    const char *path = cJSON_GetStringValue(path_json);
    
    /* Read file */
    FILE *fp = fopen(path, "r");
    if (!fp) {
        char error[256];
        snprintf(error, sizeof(error), "{\"error\": \"Failed to open file: %s\"}", path);
        *output = strdup(error);
        return AGENTC_ERR_IO;
    }
    
    /* Get file size */
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    /* Allocate and read */
    char *content = malloc(size + 1);
    if (!content) {
        fclose(fp);
        *output = strdup("{\"error\": \"Memory allocation failed\"}");
        return AGENTC_ERR_MEMORY;
    }
    
    size_t read = fread(content, 1, size, fp);
    content[read] = '\0';
    fclose(fp);
    
    /* Build response */
    struct cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "path", path);
    cJSON_AddStringToObject(response, "content", content);
    cJSON_AddNumberToObject(response, "size", read);
    
    free(content);
    *output = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
    
    return AGENTC_OK;
}

/*============================================================================
 * Tool: write_file
 *============================================================================*/

static agentc_err_t tool_write_file(
    const struct cJSON *args,
    char **output,
    void *user_data
) {
    /* Get arguments */
    const struct cJSON *path_json = cJSON_GetObjectItem(args, "path");
    const struct cJSON *content_json = cJSON_GetObjectItem(args, "content");
    
    if (!path_json || !cJSON_IsString(path_json) ||
        !content_json || !cJSON_IsString(content_json)) {
        *output = strdup("{\"error\": \"Missing or invalid arguments\"}");
        return AGENTC_ERR_INVALID_ARG;
    }
    
    const char *path = cJSON_GetStringValue(path_json);
    const char *content = cJSON_GetStringValue(content_json);
    
    /* Write file */
    FILE *fp = fopen(path, "w");
    if (!fp) {
        char error[256];
        snprintf(error, sizeof(error), "{\"error\": \"Failed to open file for writing: %s\"}", path);
        *output = strdup(error);
        return AGENTC_ERR_IO;
    }
    
    size_t written = fwrite(content, 1, strlen(content), fp);
    fclose(fp);
    
    /* Build response */
    struct cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "path", path);
    cJSON_AddNumberToObject(response, "bytes_written", written);
    cJSON_AddBoolToObject(response, "success", 1);
    
    *output = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
    
    return AGENTC_OK;
}

/*============================================================================
 * Tool: list_directory
 *============================================================================*/

static agentc_err_t tool_list_directory(
    const struct cJSON *args,
    char **output,
    void *user_data
) {
    /* Get arguments */
    const struct cJSON *path_json = cJSON_GetObjectItem(args, "path");
    if (!path_json || !cJSON_IsString(path_json)) {
        *output = strdup("{\"error\": \"Missing or invalid 'path' argument\"}");
        return AGENTC_ERR_INVALID_ARG;
    }
    
    const char *path = cJSON_GetStringValue(path_json);
    
    /* Open directory */
    DIR *dir = opendir(path);
    if (!dir) {
        char error[256];
        snprintf(error, sizeof(error), "{\"error\": \"Failed to open directory: %s\"}", path);
        *output = strdup(error);
        return AGENTC_ERR_IO;
    }
    
    /* Build file list */
    struct cJSON *response = cJSON_CreateObject();
    struct cJSON *files = cJSON_CreateArray();
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        struct cJSON *file_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(file_obj, "name", entry->d_name);
        cJSON_AddStringToObject(file_obj, "type", 
            entry->d_type == DT_DIR ? "directory" : "file");
        
        cJSON_AddItemToArray(files, file_obj);
    }
    closedir(dir);
    
    cJSON_AddStringToObject(response, "path", path);
    cJSON_AddItemToObject(response, "files", files);
    
    *output = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
    
    return AGENTC_OK;
}

/*============================================================================
 * Registration
 *============================================================================*/

agentc_err_t code_agentc_register_file_tools(ac_tools_t *tools) {
    if (!tools) {
        return AGENTC_ERR_INVALID_ARG;
    }
    
    /* read_file tool */
    {
        ac_param_t *params = ac_param_create("path", AC_PARAM_STRING,
            "Path to the file to read", 1);
        
        ac_tool_register(tools, &(ac_tool_t){
            .name = "read_file",
            .description = "Read the contents of a file",
            .parameters = params,
            .handler = tool_read_file,
            .tools_group = "file_operations",
        });
        
        ac_param_free(params);
    }
    
    /* write_file tool */
    {
        ac_param_t *path_param = ac_param_create("path", AC_PARAM_STRING,
            "Path to the file to write", 1);
        ac_param_t *content_param = ac_param_create("content", AC_PARAM_STRING,
            "Content to write to the file", 1);
        ac_param_append(&path_param, content_param);
        
        ac_tool_register(tools, &(ac_tool_t){
            .name = "write_file",
            .description = "Write content to a file",
            .parameters = path_param,
            .handler = tool_write_file,
            .tools_group = "file_operations",
        });
        
        ac_param_free(path_param);
    }
    
    /* list_directory tool */
    {
        ac_param_t *params = ac_param_create("path", AC_PARAM_STRING,
            "Path to the directory to list", 1);
        
        ac_tool_register(tools, &(ac_tool_t){
            .name = "list_directory",
            .description = "List contents of a directory",
            .parameters = params,
            .handler = tool_list_directory,
            .tools_group = "file_operations",
        });
        
        ac_param_free(params);
    }
    
    return AGENTC_OK;
}
