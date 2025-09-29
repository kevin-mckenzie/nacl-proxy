#ifndef EVENT_H
#define EVENT_H

#include "utils.h"

enum {
    MAX_EVENTS = 512,
};

// Callback function type for events.
typedef int(callback_t)(int, short, void *);

/**
 * @brief Adds a new event to the event manager.
 *
 * Registers a file descriptor with the specified event flags and associates it with a callback and user data.
 * Prevents duplicate registrations and respects the maximum event capacity.
 *
 * @param fd File descriptor to monitor.
 * @param events Event flags (e.g., POLLIN, POLLOUT).
 * @param p_data Pointer to user data passed to the callback.
 * @param callback_func Callback function to invoke when the event occurs.
 * @return PROXY_SUCCESS on success,
 *         PROXY_ERR on duplicate or invalid event,
 *         PROXY_MAX_EVENTS if capacity is reached.
 */
int event_add(int fd, short events, void *p_data, callback_t *callback_func);

/**
 * @brief Removes an event from the event manager.
 *
 * Unregisters the file descriptor and clears associated callback and user data.
 * Adjusts internal counters and indices as needed.
 *
 * @param fd File descriptor to remove.
 * @return PROXY_SUCCESS on success,
 *         PROXY_ERR if the event does not exist or parameters are invalid.
 */
int event_remove(int fd);

/**
 * @brief Runs the event loop, polling for registered events and invoking callbacks.
 *
 * Continuously polls for events until @p *p_run_flag is set to 0.
 * Handles all registered events, invoking their callbacks when triggered.
 * Returns on error or when @p *p_run_flag is cleared.
 *
 * @param p_run_flag Pointer to a flag controlling loop execution (set to 0 to exit).
 * @param poll_timeout Timeout for poll() in milliseconds.
 * @return PROXY_SUCCESS on clean exit,
 *         PROXY_ERR or callback error code on failure.
 */
int event_run_loop(volatile int *p_run_flag, int poll_timeout);

/**
 * @brief Modifies the event flags for an existing event.
 *
 * Updates the event flags for a registered file descriptor. Resets revents to ignore until next poll.
 *
 * @param fd File descriptor whose event is to be modified.
 * @param events New event flags.
 * @return PROXY_SUCCESS on success,
 *         PROXY_ERR if the event does not exist or parameters are invalid.
 */
int event_modify(int fd, short events);

/**
 * @brief Cleans up all events and closes associated file descriptors.
 *
 * Closes all registered file descriptors, frees user data using the provided function,
 * and resets internal event manager state.
 *
 * @param custom_free Function pointer to free user data (may be NULL).
 */
void event_teardown(void (*custom_free)(void *));

#endif /* EVENT_H */
