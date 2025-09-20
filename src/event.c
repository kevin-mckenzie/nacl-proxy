#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>

#ifdef __GLIBC__
#include <sys/poll.h>
#else
#include <poll.h>
#endif

#include "errors.h"
#include "event.h"
#include "log.h"
#include "utils.h"

typedef struct { // NOLINT (clang-diagnostic-padded)
    callback_t *callback;
    void *data;
    int fd;
} event_t;

typedef struct {
    struct pollfd pfds[MAX_EVENTS + 1];
    event_t events[MAX_EVENTS + 1];
    nfds_t max_idx;
    size_t num_events;
} event_manager_t;

static event_manager_t g_mgr = {0}; // NOLINT

static int get_idx_from_fd(int fd);
static event_t *get_event_from_fd(int fd);
static int handle_event(int idx);

int event_add(int efd, short events, void *p_data, callback_t *callback_func) {
    ASSERT_RET(-1 < efd);
    ASSERT_RET(0 != events);
    ASSERT_RET(NULL != callback_func); // NOLINT (misc-include-cleaner)

    if (-1 < get_idx_from_fd(efd)) {
        LOG(WRN, "duplicate event for %d; could not add", efd);
        return PROXY_ERR;
    }

    if (MAX_EVENTS == g_mgr.num_events) {
        LOG(WRN, "event manager at capacity: %zu events", g_mgr.num_events);
        return PROXY_MAX_EVENTS;
    }

    nfds_t insert_idx = (nfds_t)-1;
    for (nfds_t idx = 0; idx < g_mgr.max_idx; idx++) {
        if (-1 == g_mgr.pfds[idx].fd) {
            insert_idx = idx;
            break;
        }
    }

    if ((nfds_t)-1 == insert_idx) {
        insert_idx = g_mgr.max_idx;
        g_mgr.max_idx++;
    }
    g_mgr.num_events++;

    g_mgr.pfds[insert_idx].fd = efd;
    g_mgr.pfds[insert_idx].events = events;
    g_mgr.events[insert_idx].fd = efd;
    g_mgr.events[insert_idx].data = p_data;
    g_mgr.events[insert_idx].callback = callback_func;

    return PROXY_SUCCESS;
}

int event_modify(int efd, short events) {
    ASSERT_RET(-1 < efd);
    ASSERT_RET(0 != events);

    int event_idx = get_idx_from_fd(efd);
    if (-1 == event_idx) {
        LOG(WRN, "event to modify does not exist (fd %d)", efd);
        return PROXY_ERR;
    }

    g_mgr.pfds[event_idx].fd = efd;
    g_mgr.pfds[event_idx].events = events;
    g_mgr.pfds[event_idx].revents = 0; // reset so the event is ignored until the next poll() call
    g_mgr.events[event_idx].fd = efd;

    return PROXY_SUCCESS;
}

int event_remove(int efd) {
    ASSERT_RET(-1 < efd);

    int event_idx = get_idx_from_fd(efd);
    if (-1 == event_idx) {
        LOG(WRN, "could not locate event for FD %d", efd);
        return PROXY_ERR;
    }

    g_mgr.pfds[event_idx].fd = -1;
    g_mgr.pfds[event_idx].events = 0;
    g_mgr.pfds[event_idx].revents = 0;

    g_mgr.events[event_idx].fd = -1;
    g_mgr.events[event_idx].callback = NULL;
    g_mgr.events[event_idx].data = NULL;

    if ((nfds_t)event_idx == (g_mgr.max_idx - 1)) {
        g_mgr.max_idx--;
    }
    g_mgr.num_events--;

    return PROXY_SUCCESS;
}

// cppcheck-suppress constParameterPointer
int event_run_loop(volatile int *p_run_flag, int poll_timeout) { // NOLINT (readability-non-const-parameter)
    ASSERT_RET(NULL != p_run_flag);

    while (1 == *p_run_flag) {
        int poll_ct = poll(g_mgr.pfds, g_mgr.max_idx, poll_timeout);

        if (-1 == poll_ct) {
            LOG(ERR, "poll");
            return PROXY_ERR;
        }

        if (0 < poll_ct) {
            for (nfds_t idx = 0; idx < g_mgr.max_idx; idx++) {
                int err = handle_event((int)idx);
                if (err) {
                    LOG(WRN, "handle_event error %d", err);
                    return err;
                }
            }
        }
    }

    return PROXY_SUCCESS;
}

void event_teardown(void (*custom_free)(void *)) {
    for (nfds_t idx = 0; idx < g_mgr.max_idx; idx++) {
        if (-1 < g_mgr.events[idx].fd) {
            (void)close(g_mgr.events[idx].fd);
        }

        if (NULL != custom_free) {
            custom_free(g_mgr.events[idx].data);
        }

        memset(&g_mgr.events[idx], 0, sizeof(event_t));
        memset(&g_mgr.pfds[idx], 0, sizeof(struct pollfd));
        g_mgr.events[idx].fd = -1;
        g_mgr.pfds[idx].fd = -1;
    }
}

static int handle_event(int idx) {
    ASSERT_RET(-1 < idx);

    int err = PROXY_SUCCESS;

    struct pollfd *p_pfd = &g_mgr.pfds[idx];
    if (-1 != p_pfd->fd) { // skip if a slot between 0 and max_idx does not currently have an event
        event_t *p_event = get_event_from_fd(p_pfd->fd);
        if (NULL == p_event) {
            LOG(WRN, "Events array does not correspond to pollfds");
            return PROXY_ERR;
        }

        if (0 != p_pfd->revents) {
            err = p_event->callback(p_event->fd, p_pfd->revents, p_event->data);
        }
    }

    return err;
}

static int get_idx_from_fd(int efd) {
    ASSERT_RET(-1 < efd);

    for (nfds_t idx = 0; idx < g_mgr.max_idx; idx++) {
        if (g_mgr.pfds[idx].fd == efd) {
            ASSERT_RET(g_mgr.pfds[idx].fd == g_mgr.events[idx].fd);
            return (int)idx;
        }
    }

    return -1;
}

static event_t *get_event_from_fd(int efd) {
    assert(-1 < efd);

    int event_idx = get_idx_from_fd(efd);
    if (-1 == event_idx) {
        LOG(WRN, "failed to locate event for fd %d", efd);
        return NULL;
    }

    return &g_mgr.events[event_idx];
}

/** END OF FILE **/
