/*
    DIRWATCHER.H
      Master include file for Dirwatcher

    * * * * * *
    * License *
    * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
    * CC0 1.0 Universal
    *
    * The person who associated a work with this deed has dedicated the work
    * to the public domain by waiving all of his or her rights to the work
    * worldwide under copyright law, including all related and neighboring
    * rights, to the extent allowed by law.
    *
    * You can copy, modify, distribute and perform the work, even for
    * commercial purposes, all without asking permission.
    *
    * The work is provided "as is", without warranties.
    * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *

    * * * * * * * *
    * Basic usage *
    * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
    * 1. Open a target directory:
    * 
    *     dirwatcher_target_t target = dirwatcher_open_target("PATH/TO/DIR");
    * 
    * 2. Set a callback function
    * 
    *     dirwatcher_set_target_callback(target, my_callback);
    * 
    * 3. Start watching:
    * 
    *     dirwatcher_start_watch_target(target);
    * 
    * 4. Stop watching (optional):
    * 
    *     dirwatcher_stop_watch_target(target);
    * 
    * 5. Close the target and release all resources:
    * 
    *     dirwatcher_close_target(target);
    * 
    * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
    
    * * * * * * * * * *
    * Callback Rules  *
    * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
    * - The callback is invoked from the worker thread, NOT from the caller thread.
    * - The callback must return quickly; blocking operations are discouraged.
    * - The event_info pointer is only valid during callback execution.
    * - The library owns all memory referenced by event_info.
    *
    * - If an internal worker error occurs, the callback is invoked once with
    *   event_info == NULL.
    *
    * - After event_info == NULL is delivered, the worker thread terminates and
    *   no further callbacks will be invoked.
    * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
    
    * * * * * * * * * * * * * * * * * * *
    * Thread-Safety and Lifetime Rules  *
    * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
    * - A target object must NOT be closed from inside its own callback.
    *   Doing so will result in a deadlock.
    * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
    
    * * * * * * * * * * * * * * *
    * Pause / Resume Semantics  *
    * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
    * - dirwatcher_start_watch_target() enables directory monitoring.
    * - dirwatcher_stop_watch_target() pauses monitoring but does NOT destroy the
    *   target or the worker thread.
    *
    * - While paused, no callbacks will be delivered.
    * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *

    * * * * * * * * * *
    * Error Handling  *
    * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
    * - If an internal error occurs in the worker thread, the target enters a
    *   permanent error state.
    *
    * - The error can be queried via:
    *
    *      dirwatcher_get_target_error()
    *
    *   or (Windows only):
    *
    *      dirwatcher_get_target_win32_error()
    *
    * - Once an error is reported, the target must be closed and recreated.
    * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
*/

#ifndef DIRWATCHER_H
#define DIRWATCHER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "c"
#endif

typedef void* dirwatcher_target_t;

typedef enum dirwatcher_event
{
    DIRWATCHER_EVENT_NULL, /* Internal / no-op event (not an error) */
    DIRWATCHER_EVENT_ADDED,
    DIRWATCHER_EVENT_REMOVED,
    DIRWATCHER_EVENT_MODIFIED,
    DIRWATCHER_EVENT_RENAMED_FROM,
    DIRWATCHER_EVENT_RENAMED_TO,
    DIRWATCHER_EVENT_COUNT
} dirwatcher_event_t;

typedef enum dirwatcher_error
{
    DIRWATCHER_INVALID_TARGET = -1,
    DIRWATCHER_SUCCESS,
    DIRWATCHER_UNKNOWN_INTERNAL_ERROR,
    DIRWATCHER_TARGET_NOT_SUPPORTED,
    DIRWATCHER_ACCESS_DENIED,
    DIRWATCHER_MEMORY_NOT_ENOUGH,
    DIRWATCHER_UNKNOWN_OS_ERROR
} dirwatcher_error_t;

typedef struct dirwatcher_event_info
{
    char*              name;  /* read-only, owned by library, UTF - 8 Encoding */
    dirwatcher_event_t event;
} dirwatcher_event_info_t;

/*
    event_info is only valid during callback execution.
    If an error occurs in the worker thread, event_info will be NULL. 
*/
typedef void (*dirwatcher_callback_t)(const dirwatcher_event_info_t* event_info, void* user_data);

/*
    Opens a directory target for mornitoring.
    Returns NULL on failure.
*/
dirwatcher_target_t dirwatcher_open_target(const char* name);

/*
    Opens a directory target and set callback and start watch
    Returns NULL on failure.
*/
dirwatcher_target_t dirwatcher_watch(const char* name, dirwatcher_callback_t callback, void* user_data);

/*
    Closes a directory target.
    Returns false if the target is invalid.
*/
bool dirwatcher_close_target(dirwatcher_target_t target);

/*
    Sets the target's callback thread-safely.
*/
bool dirwatcher_set_target_callback(dirwatcher_target_t target, dirwatcher_callback_t callback, void* user_data);

/*
    Start target watching.
    If target is invalid, returns false.
*/
bool dirwatcher_start_watch_target(dirwatcher_target_t target);

/*
    Stop target watching.
    If target is invalid, returns false.
*/
bool dirwatcher_stop_watch_target(dirwatcher_target_t target);

/*
    Gets full path name from target.
    If target is invalid or path is NULL, returns 0.

    If buf is NULL, returns required buffer length.
*/
size_t dirwatcher_get_full_path_from_target(const char* path, dirwatcher_target_t target, char* buf /* NULLABLE */, size_t buf_len);

/*
    Returns target's error code.
    if target is invalid, returns DIRWATCHER_INVALID_TARGET.
*/
dirwatcher_error_t dirwatcher_get_target_error(dirwatcher_target_t target);

#ifdef _WIN32
/*
    Returns target's error code.
    if target is invalid, returns -1. 
*/
long dirwatcher_get_target_win32_error(dirwatcher_target_t target);
#else
#error DIRWATCHER: Platform not supported.
#endif

#ifdef __cplusplus
}
#endif

#endif
