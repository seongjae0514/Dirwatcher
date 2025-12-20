/* Includes *******************************************/

#include <dirwatcher.h>

#include <Windows.h>
#include <stdbool.h>
#include <wchar.h>
#include <stdint.h>

/* Defines ********************************************/

#define DIRWATCHER_TARGET_MAGIC_NUMBER 0x4449525741544348ULL // 'DIRWATCH'

typedef struct _dirwatcher_target_impl
{
    uint64_t              magic;

    HANDLE                dir_handle;           // Handle to the target directory

    HANDLE                worker_thread_handle; // Handle to the worker thread
    HANDLE                worker_control_event; // Worker thread control event (set: run, reset: stop)

    volatile LONG         exit_flag;            // Indicates whether the worker thread should terminate
                                                // Interlocked-only (atomic); do NOT read/write directly

    volatile LONG         error_code;           // Win32 error code set by worker thread
                                                // ERROR_SUCCESS (0) = no error
                                                // If this value is non-zero, the worker thread will terminate
                                                // Interlocked-only (atomic); do NOT read/write directly
                                                // ERROR_OPERATION_ABORTED (cancel/shutdown) must NOT be stored here

    dirwatcher_callback_t callback;             // Callback invoked when a directory event occurs
    SRWLOCK               callback_lock;        // Must be held when changing the callback
} _dirwatcher_target_impl_t;

/* Private functions **********************************/

static bool _wstrn_to_cstr(const wchar_t* wstrn   /* not null termed wchar str */,
                           int            wlen,   /* count of wchar */
                           char*          buf     /* null termed */,
                           int            buf_len /* count of char */)
{
    if (!wstrn || !buf)
    {
        return false;
    }

    int ret = WideCharToMultiByte(CP_UTF8, 0, wstrn, wlen, buf, buf_len - 1, NULL, FALSE);

    if (ret <= 0)
    {
        buf[0] = '\0';
        return false;
    }

    buf[ret] = '\0';

    return true;
}

static int _get_cstr_count_from_wstrn(const wchar_t* wstrn, int wcount /* count of wchar */)
{
    return WideCharToMultiByte(CP_UTF8, 0, wstrn, wcount, NULL, 0, NULL, FALSE) + 1 /* null-terminate */;
}

static dirwatcher_event_t _action_to_event(DWORD action)
{
    switch (action)
    {
    case FILE_ACTION_ADDED:
        return DIRWATCHER_EVENT_ADDED;
    case FILE_ACTION_REMOVED:
        return DIRWATCHER_EVENT_REMOVED;
    case FILE_ACTION_MODIFIED:
        return DIRWATCHER_EVENT_MODIFIED;
    case FILE_ACTION_RENAMED_OLD_NAME:
        return DIRWATCHER_EVENT_RENAMED_FROM;
    case FILE_ACTION_RENAMED_NEW_NAME:
        return DIRWATCHER_EVENT_RENAMED_TO;
    }
    return DIRWATCHER_EVENT_NULL;
}

static bool _go_next_notify(PFILE_NOTIFY_INFORMATION* ppnotify)
{
    if (!(*ppnotify)->NextEntryOffset)
    {
        return false;
    }

    *ppnotify = (PFILE_NOTIFY_INFORMATION)(((char*)*ppnotify) + ((*ppnotify)->NextEntryOffset));

    return true;
}

static int _get_notifies_count(PFILE_NOTIFY_INFORMATION pnotify)
{
    int count = 1;

    while (_go_next_notify(&pnotify))
    {
        count++;
    }

    return count;
}

static bool _notifies_to_events(PFILE_NOTIFY_INFORMATION pnotify,
                                dirwatcher_event_info_t* p_events_arr,
                                size_t                   arr_size,      /* byte-size */
                                int*                     p_events_count /* returned events count */)
{
    if (!p_events_count || !p_events_arr)
    {
        return false;
    }
    
    int events_arr_count = (int)(arr_size / sizeof(dirwatcher_event_info_t));
    int events_count     = _get_notifies_count(pnotify);
    
    _get_notifies_count(pnotify);

    memset(p_events_arr, 0, arr_size);

    for (int i = 0; i < events_arr_count; i++)
    {
        int name_len = _get_cstr_count_from_wstrn(pnotify->FileName, pnotify->FileNameLength / sizeof(wchar_t));

        p_events_arr[i].name = malloc(name_len);

        if (!p_events_arr[i].name)
        {
            for (int j = 0; j < i; j++)
            {
                free(p_events_arr[j].name);
            }
            memset(p_events_arr, 0, arr_size);

            *p_events_count = 0;

            return false;
        }

        _wstrn_to_cstr(pnotify->FileName, pnotify->FileNameLength / sizeof(wchar_t), p_events_arr[i].name, name_len);
        p_events_arr[i].event = _action_to_event(pnotify->Action);
    
        if (!_go_next_notify(&pnotify))
        {
            break;
        }
    }

    *p_events_count = events_count;

    return true;
}

static void _cleanup_events(dirwatcher_event_info_t* p_events_arr, int events_count)
{
    for (int i = 0; i < events_count; i++)
    {
        free(p_events_arr[i].name);
        p_events_arr[i].name = NULL;
    }
}

static DWORD WINAPI _worker_thread_routine(PVOID data)
{
    /*
        NOTE: This worker relies on CancelIoEx() or handle closure to unblock
              ReadDirectoryChangesW during shutdown.
    */

    _dirwatcher_target_impl_t* target              = data;
    dirwatcher_event_info_t    events[256]         = { 0 };
    int                        events_count        = 0;
    __declspec(align(4)) BYTE  notify_buffer[4096] = { 0 };
    DWORD                      bytes_returned      = 0;
    bool                       success             = true;
    dirwatcher_callback_t      cb                  = NULL;

    for (;;)
    {
        //
        // Wait for enable
        //

        WaitForSingleObject(target->worker_control_event, INFINITE);

        /* If exit flag set then exit */
        if (InterlockedCompareExchange(&target->exit_flag, 0, 0))
        {
            return 0;
        }

        //
        // Get directory events
        //

        success = ReadDirectoryChangesW(target->dir_handle,
                                        notify_buffer,
                                        sizeof(notify_buffer),
                                        TRUE,
                                        FILE_NOTIFY_CHANGE_DIR_NAME   |
                                        FILE_NOTIFY_CHANGE_FILE_NAME  |
                                        FILE_NOTIFY_CHANGE_LAST_WRITE |
                                        FILE_NOTIFY_CHANGE_SIZE,
                                        &bytes_returned,
                                        NULL,
                                        NULL);

        //
        // Get callback function safely
        //

        AcquireSRWLockShared(&target->callback_lock);
        cb = target->callback;
        ReleaseSRWLockShared(&target->callback_lock);

        if (success)
        {
            _notifies_to_events((PFILE_NOTIFY_INFORMATION)notify_buffer, events, sizeof(events), &events_count);

            //
            // Call callback function
            //

            for (int i = 0; i < events_count; i++)
            {
                if (cb) cb(&events[i]);
            }

            //
            // Cleanup events
            //

            _cleanup_events(events, events_count);
        }
        else
        {
            DWORD last_error = GetLastError();

            if (last_error == ERROR_OPERATION_ABORTED)
            {
                continue;
            }
            else
            {
                InterlockedExchange(&target->error_code, last_error);
                InterlockedExchange(&target->exit_flag, 1);
                if (cb) cb(NULL);
                return (DWORD)-1;
            }
        }
    }
}

static HANDLE _create_worker_thread(_dirwatcher_target_impl_t* target)
{
    return CreateThread(NULL,
                        0,
                        _worker_thread_routine,
                        target,
                        0,
                        NULL);
}

static HANDLE _open_target_dir(const char* name)
{
    HANDLE h = CreateFileA(name,
                           FILE_LIST_DIRECTORY,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL,
                           OPEN_EXISTING,
                           FILE_FLAG_BACKUP_SEMANTICS,
                           NULL);

    return h != INVALID_HANDLE_VALUE ? h : NULL;
}

static HANDLE _create_working_event(void)
{
    return CreateEventW(NULL, TRUE, FALSE, NULL);
}

static bool _is_valid_target_ptr(_dirwatcher_target_impl_t* target)
{
    return ((target) && (target->magic == DIRWATCHER_TARGET_MAGIC_NUMBER));
}

static _dirwatcher_target_impl_t* _create_target(const char* name)
{
    _dirwatcher_target_impl_t* target = calloc(1, sizeof(_dirwatcher_target_impl_t));

    if (!target)
    {
        return NULL;
    }

    target->dir_handle = _open_target_dir(name);
    
    if (!target->dir_handle)
    {
        free(target);
        return NULL;
    }

    target->worker_control_event = _create_working_event();

    if (!target->worker_control_event)
    {
        CloseHandle(target->dir_handle);
        free(target);
        return NULL;
    }

    target->worker_thread_handle = _create_worker_thread(target);

    if (!target->worker_thread_handle)
    {
        CloseHandle(target->dir_handle);
        CloseHandle(target->worker_control_event);
        free(target);
        return NULL;
    }

    target->magic      = DIRWATCHER_TARGET_MAGIC_NUMBER;
    target->exit_flag  = 0;
    target->error_code = 0;
    target->callback   = NULL;

    InitializeSRWLock(&target->callback_lock);

    return target;
}

static void _delete_target(_dirwatcher_target_impl_t* target)
{
    //
    // Stop worker and set exit flag
    //

    ResetEvent(target->worker_control_event);

    InterlockedExchange(&target->exit_flag, 1);

    //
    // Cancle `ReadDirectoryChangesW`
    //

    CancelIoEx(target->dir_handle, NULL);

    //
    // Resume worker and wait for it ends
    //

    SetEvent(target->worker_control_event);

    WaitForSingleObject(target->worker_thread_handle, INFINITE);

    //
    // Cleanup resources
    //

    CloseHandle(target->dir_handle);
    CloseHandle(target->worker_thread_handle);
    CloseHandle(target->worker_control_event);

    //
    // Initialize magic for safe
    //

    target->magic = 0;
    
    free(target);
}

static void _pause_target(_dirwatcher_target_impl_t* target)
{
    ResetEvent(target->worker_control_event);
    CancelIoEx(target->dir_handle, NULL);
}

static void _resume_target(_dirwatcher_target_impl_t* target)
{
    SetEvent(target->worker_control_event);
}

/* Public functions ***********************************/

dirwatcher_target_t dirwatcher_open_target(const char* name)
{
    DWORD attr = GetFileAttributesA(name);

    if (!name                           ||
        attr == INVALID_FILE_ATTRIBUTES ||
        !(attr & FILE_ATTRIBUTE_DIRECTORY))
    {
        return NULL;
    }

    return (dirwatcher_target_t)_create_target(name);
}

bool dirwatcher_close_target(dirwatcher_target_t target)
{
    if (_is_valid_target_ptr((_dirwatcher_target_impl_t*)target))
    {
        _delete_target((_dirwatcher_target_impl_t*)target);
        return true;
    }
    else
    {
        return false;
    }
}

bool dirwatcher_set_target_callback(dirwatcher_target_t target, dirwatcher_callback_t callback)
{
    if (_is_valid_target_ptr((_dirwatcher_target_impl_t*)target))
    {
        _dirwatcher_target_impl_t* target_impl = target;

        AcquireSRWLockExclusive(&target_impl->callback_lock);
        target_impl->callback = callback;
        ReleaseSRWLockExclusive(&target_impl->callback_lock);

        return true;
    }
    else
    {
        return false;
    }
}

bool dirwatcher_start_watch_target(dirwatcher_target_t target)
{
    if (_is_valid_target_ptr((_dirwatcher_target_impl_t*)target))
    {
        _resume_target((_dirwatcher_target_impl_t*)target);
        return true;
    }
    else
    {
        return false;
    }
}

bool dirwatcher_stop_watch_target(dirwatcher_target_t target)
{
    if (_is_valid_target_ptr((_dirwatcher_target_impl_t*)target))
    {
        _pause_target((_dirwatcher_target_impl_t*)target);
        return true;
    }
    else
    {
        return false;
    }
}

dirwatcher_error_t dirwatcher_get_target_error(dirwatcher_target_t target)
{
    if (!_is_valid_target_ptr(target))
    {
        return DIRWATCHER_INVALID_TARGET;
    }

    switch (((_dirwatcher_target_impl_t*)target)->error_code)
    {
    case ERROR_ACCESS_DENIED:
        return DIRWATCHER_ACCESS_DENIED;
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY:
    case ERROR_TOO_MANY_OPEN_FILES:
        return DIRWATCHER_MEMORY_NOT_ENOUGH;
    case ERROR_INVALID_FUNCTION:
        return DIRWATCHER_TARGET_NOT_SUPPORTED;
    case ERROR_INVALID_PARAMETER:
    case ERROR_NOACCESS:
    case ERROR_NOTIFY_ENUM_DIR:
        return DIRWATCHER_UNKNOWN_INTERNAL_ERROR;
    default:
        return DIRWATCHER_UNKNOWN_OS_ERROR;
    }
}

long dirwatcher_get_target_win32_error(dirwatcher_target_t target)
{
    if (!_is_valid_target_ptr(target))
    {
        return DIRWATCHER_INVALID_TARGET;
    }

    return ((_dirwatcher_target_impl_t*)target)->error_code;
}
