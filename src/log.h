#ifndef LOG_H
#define LOG_H

#include <stdlib.h>
#include <stdio.h>

#define ERROR(msg, ...) do { \
    printf("coda: \033[31mERROR:\033[0m "); \
    printf(msg, ##__VA_ARGS__);\
    putc('\n', stdout);\
} while (0)\

#endif