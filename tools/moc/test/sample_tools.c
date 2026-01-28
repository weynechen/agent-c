/**
 * @file sample_tools.c
 * @brief Implementation of sample tools for testing MOC
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "sample_tools.h"

/* Static weather data for demo */
static const char* weather_data[] = {
    "Sunny, 25°C",
    "Cloudy, 18°C",
    "Rainy, 15°C",
    "Snowy, -5°C"
};

const char* get_weather(const char* place) {
    /* Simple hash-based selection for demo */
    if (!place || strlen(place) == 0) {
        return "Unknown location";
    }
    
    int hash = 0;
    for (const char* p = place; *p; p++) {
        hash += *p;
    }
    
    return weather_data[hash % 4];
}

int add_two_numbers(int a, int b) {
    return a + b;
}

double calculate_average(double x, double y) {
    return (x + y) / 2.0;
}

bool is_positive(int value) {
    return value > 0;
}

void print_greeting(const char* name) {
    if (name) {
        printf("Hello, %s!\n", name);
    } else {
        printf("Hello, World!\n");
    }
}

int helper_function(int x) {
    return x * 2;
}
