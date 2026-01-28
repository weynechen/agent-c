/**
 * @file demo_tools.h
 * @brief Demo tools for chat_tools example
 *
 * This file uses AC_TOOL_META markers for MOC code generation.
 * MOC will generate wrapper functions and registration code automatically.
 */

#ifndef DEMO_TOOLS_H
#define DEMO_TOOLS_H

#include <stdbool.h>

/* AC_TOOL_META marker - recognized by MOC but ignored by compiler */
#define AC_TOOL_META

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Tool Declarations
 *============================================================================*/

/**
 * @description: Get the current date and time
 */
AC_TOOL_META const char* get_current_time(void);

/**
 * @description: Perform arithmetic calculation
 * @param: operation  The operation to perform (add, subtract, multiply, divide, power, mod)
 * @param: a  First operand
 * @param: b  Second operand
 */
AC_TOOL_META double calculator(const char* operation, double a, double b);

/**
 * @description: Get the current weather for a location
 * @param: location  The city or location name
 */
AC_TOOL_META const char* get_weather(const char* location);

/**
 * @description: Convert temperature between Celsius and Fahrenheit
 * @param: value  Temperature value to convert
 * @param: to_unit  Target unit (celsius or fahrenheit)
 */
AC_TOOL_META double convert_temperature(double value, const char* to_unit);

/**
 * @description: Generate a random number within a range
 * @param: min  Minimum value (inclusive)
 * @param: max  Maximum value (inclusive)
 */
AC_TOOL_META int random_number(int min, int max);

#ifdef __cplusplus
}
#endif

#endif /* DEMO_TOOLS_H */
