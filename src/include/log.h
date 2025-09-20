#ifndef LOGGING_H
#define LOGGING_H

#ifndef NDEBUG

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <time.h>

enum log_lvl {
    TRC = 0,
    IO,
    DBG,
    INF,
    WRN,
    ERR
};

#define LOG_LVL ERR

#define COLOR_RESET "\x1b[0m"   /**< ANSI Reset */
#define COLOR_RED "\x1b[31m"    /**< ANSI Red */
#define COLOR_YELLOW "\x1b[33m" /**< ANSI Yellow */
#define COLOR_GREEN "\x1b[32m"  /**< ANSI Green */
#define COLOR_BLUE "\x1b[34m"   /**< ANSI Blue */
#define COLOR_PURPLE "\x1b[35m" /**< ANSI Purple */
#define COLOR_CYAN "\x1b[36m"   /**< ANSI Cyan */

#define LOG(level, fmt, ...)                                                                                           \
    do {                                                                                                               \
        if (level >= LOG_LVL) {                                                                                        \
            struct timespec now = {0};                                                                                 \
            (void)clock_gettime(CLOCK_MONOTONIC, &now);                                                                \
            const char *lvl_str = "OTHER";                                                                             \
            const char *color = COLOR_RESET;                                                                           \
            switch (level) {                                                                                           \
            case TRC:                                                                                                  \
                lvl_str = "TRACE";                                                                                     \
                color = COLOR_CYAN;                                                                                    \
                break;                                                                                                 \
            case IO:                                                                                                   \
                lvl_str = "IO";                                                                                        \
                color = COLOR_PURPLE;                                                                                  \
                break;                                                                                                 \
            case DBG:                                                                                                  \
                lvl_str = "DEBUG";                                                                                     \
                color = COLOR_BLUE;                                                                                    \
                break;                                                                                                 \
            case INF:                                                                                                  \
                lvl_str = "INFO";                                                                                      \
                color = COLOR_GREEN;                                                                                   \
                break;                                                                                                 \
            case WRN:                                                                                                  \
                lvl_str = "WARN";                                                                                      \
                color = COLOR_YELLOW;                                                                                  \
                break;                                                                                                 \
            case ERR:                                                                                                  \
                lvl_str = "ERROR";                                                                                     \
                color = COLOR_RED;                                                                                     \
                break;                                                                                                 \
            default:                                                                                                   \
                lvl_str = "OTHER";                                                                                     \
                color = COLOR_RESET;                                                                                   \
                break;                                                                                                 \
            }                                                                                                          \
            (void)fprintf(stderr, "%ld.%09ld [%s%5s%s] %s():%d: ", now.tv_sec, now.tv_nsec, color, lvl_str,            \
                          COLOR_RESET, __func__, __LINE__);                                                            \
            if (0 != errno) {                                                                                          \
                (void)fprintf(stderr, "(errno:%d): " fmt "\n", errno, ##__VA_ARGS__) /*NOLINT*/;                       \
            } else {                                                                                                   \
                (void)fprintf(stderr, fmt "\n", ##__VA_ARGS__);                                                        \
            }                                                                                                          \
        }                                                                                                              \
    } while (0)

#else
#define LOG(level, fmt, ...)
#endif

#endif

/*** END OF FILE ***/
