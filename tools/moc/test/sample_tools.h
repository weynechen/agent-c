/**
 * @file sample_tools.h
 * @brief Sample tools for testing MOC code generation
 *
 * This file demonstrates the AC_TOOL_META format that MOC parses.
 */

#ifndef SAMPLE_TOOLS_H
#define SAMPLE_TOOLS_H

/* AC_TOOL_META marker - recognized by MOC but ignored by compiler */
#define AC_TOOL_META

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @description: Get the current weather for a specified city
 * @param: place  The name of the city to get weather for
 */
AC_TOOL_META const char* get_weather(const char* place);

/**
 * @description: Add two numbers together
 * @param: a  The first number
 * @param: b  The second number
 */
AC_TOOL_META int add_two_numbers(int a, int b);

/**
 * @description: Calculate the average of two floating point numbers
 * @param: x  First value
 * @param: y  Second value
 */
AC_TOOL_META double calculate_average(double x, double y);

/**
 * @description: Check if a number is positive
 * @param: value  The number to check
 */
AC_TOOL_META bool is_positive(int value);

/**
 * @description: Print a greeting message to console
 * @param: name  The name to greet
 */
AC_TOOL_META void print_greeting(const char* name);

/* This function is NOT marked with AC_TOOL_META - should be ignored */
int helper_function(int x);

#ifdef __cplusplus
}
#endif

#endif /* SAMPLE_TOOLS_H */
