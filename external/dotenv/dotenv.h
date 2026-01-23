#ifndef DOTENV_DOTENV_H
#define DOTENV_DOTENV_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @param path Can be a directory containing a file named .env, or the path of the env file itself
 * @param overwrite Existing variables will be overwritten
 * @return 0 on success, -1 if can't open the file
 */
int env_load(const char* path, bool overwrite);

/**
 * @brief Get environment variable with default value
 * @param name Environment variable name
 * @param default_value Default value to return if variable is not set or empty
 * @return Environment variable value or default_value
 */
const char* getenv_default(const char* name, const char* default_value);

#ifdef __cplusplus
}
#endif

#endif
