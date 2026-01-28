/**
 * @file demo_tools.c
 * @brief Demo tools implementation
 */

#include "demo_tools.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

/*============================================================================
 * Static buffers for string returns
 * Note: In production, consider thread-local storage or caller-allocated buffers
 *============================================================================*/

static char time_buffer[128];
static char weather_buffer[256];

/*============================================================================
 * Tool Implementations
 *============================================================================*/

const char* get_current_time(void) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    
    strftime(time_buffer, sizeof(time_buffer), 
             "%Y-%m-%d %H:%M:%S (local time)", tm_info);
    
    return time_buffer;
}

double calculator(const char* operation, double a, double b) {
    if (!operation) return 0.0;
    
    if (strcmp(operation, "add") == 0 || strcmp(operation, "+") == 0) {
        return a + b;
    } else if (strcmp(operation, "subtract") == 0 || strcmp(operation, "-") == 0) {
        return a - b;
    } else if (strcmp(operation, "multiply") == 0 || strcmp(operation, "*") == 0) {
        return a * b;
    } else if (strcmp(operation, "divide") == 0 || strcmp(operation, "/") == 0) {
        if (b == 0) return NAN;  /* Division by zero */
        return a / b;
    } else if (strcmp(operation, "power") == 0 || strcmp(operation, "^") == 0) {
        return pow(a, b);
    } else if (strcmp(operation, "mod") == 0 || strcmp(operation, "%") == 0) {
        return fmod(a, b);
    }
    
    return NAN;  /* Unknown operation */
}

const char* get_weather(const char* location) {
    if (!location || strlen(location) == 0) {
        return "Error: location is required";
    }
    
    /* Mock weather data based on location name hash */
    unsigned int hash = 0;
    for (const char *p = location; *p; p++) {
        hash = hash * 31 + (unsigned char)*p;
    }
    
    int temp = 15 + (hash % 20);  /* 15-35°C */
    int humidity = 40 + (hash % 40);  /* 40-80% */
    const char *conditions[] = {"sunny", "cloudy", "rainy", "windy", "snowy"};
    const char *condition = conditions[hash % 5];
    
    snprintf(weather_buffer, sizeof(weather_buffer),
             "Weather in %s: %d°C, %s, humidity %d%%",
             location, temp, condition, humidity);
    
    return weather_buffer;
}

double convert_temperature(double value, const char* to_unit) {
    if (!to_unit) return value;
    
    if (strcmp(to_unit, "celsius") == 0 || strcmp(to_unit, "c") == 0) {
        /* Fahrenheit to Celsius */
        return (value - 32.0) * 5.0 / 9.0;
    } else if (strcmp(to_unit, "fahrenheit") == 0 || strcmp(to_unit, "f") == 0) {
        /* Celsius to Fahrenheit */
        return value * 9.0 / 5.0 + 32.0;
    }
    
    return value;  /* Unknown unit, return as-is */
}

int random_number(int min, int max) {
    static int seeded = 0;
    if (!seeded) {
        srand((unsigned int)time(NULL));
        seeded = 1;
    }
    
    if (min > max) {
        int tmp = min;
        min = max;
        max = tmp;
    }
    
    return min + (rand() % (max - min + 1));
}
