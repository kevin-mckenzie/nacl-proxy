#ifndef UTILS_H
#define UTILS_H

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

enum Base {
    DECIMAL = 10,
};

#ifndef NDEBUG
#define DEBUG_PRINT(fmt, ...) (void)fprintf(stderr, "DEBUG: %d:%s(): " fmt "\n", __LINE__, __func__, ##__VA_ARGS__)
#define DEBUG_PERROR(msg)                                                                                              \
    do {                                                                                                               \
        (void)fprintf(stderr, "DEBUG: %d:%s(): %s: %s \n", __LINE__, __func__, strerror(errno), msg); /*NOLINT*/       \
    } while (0);
#else
#define DEBUG_PRINT(fmt, ...)
#define DEBUG_PERROR(msg)
#endif

#define ASSERT_RET(condition)                                                                                          \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            assert(condition);                                                                                         \
            return -1;                                                                                                 \
        }                                                                                                              \
    } while (0)

#define FREE_AND_NULL(ptr)                                                                                             \
    do {                                                                                                               \
        free(ptr);                                                                                                     \
        ptr = NULL;                                                                                                    \
    } while (0)
#endif

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
