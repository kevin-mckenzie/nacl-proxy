#ifndef EVENT_H
#define EVENT_H

#include "utils.h"

enum {
    MAX_EVENTS = 512,
};

typedef int(callback_t)(int, short, void *);

int event_add(int fd, short events, void *p_data, callback_t *callback_func);

int event_remove(int fd);

int event_run_loop(const int *p_run_flag, int poll_timeout);

int event_modify(int fd, short events);

#endif /* EVENT_H */
