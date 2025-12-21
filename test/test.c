#include <dirwatcher.h>
#include <stdio.h>
#include <string.h>
#include <conio.h>
#ifdef _WIN32
#include <Windows.h>
#endif

#define MY_MAX_PATH 260

static char* event_names[] = {
    "<ERROR>",
    "Added",
    "Removed",
    "Modified",
    "Renamed from",
    "Renamed to",
    "<ERROR>"
};
static char* error_names[] = {
    "DIRWATCHER_SUCCESS",
    "DIRWATCHER_UNKNOWN_INTERNAL_ERROR",
    "DIRWATCHER_TARGET_NOT_SUPPORTED",
    "DIRWATCHER_ACCESS_DENIED",
    "DIRWATCHER_MEMORY_NOT_ENOUGH",
    "DIRWATCHER_UNKNOWN_OS_ERROR"
};
static dirwatcher_target_t target   = NULL;
static bool                err_flag = false;

static void callback(const dirwatcher_event_info_t* event)
{
    if (!event)
    {
        printf("Error occured. error name: %s\n"
               "win32 error code: % ld\n"
               "To exit, press any key.\n",
               error_names[dirwatcher_get_target_error(target)],
               dirwatcher_get_target_win32_error(target));
        err_flag = true;
        return;
    }

    size_t buffer_len = dirwatcher_get_full_path_from_target(event->name, target, NULL, 0);
    char*  buffer     = malloc(buffer_len);

    dirwatcher_get_full_path_from_target(event->name, target, buffer, buffer_len);

    printf("+---------------------------------------------------------\n"
           "| Event: %s\n"
           "| Name:  %s\n"
           "+---------------------------------------------------------\n",
           event_names[event->event], buffer);

    free(buffer);
}

static void remove_newline(char* buf)
{
    for (char* p = buf; *p; p++)
    {
        if (*p == '\n' || *p == '\r')
        {
            *p = '\0';
            break;
        }
    }
}

int main(void)
{
    char dir_name[MY_MAX_PATH]; 

#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    printf("Enter directory name to watch\n"
           "> ");
    fgets(dir_name, sizeof(dir_name), stdin);
    remove_newline(dir_name);

    target = dirwatcher_open_target(dir_name);

    if (!target)
    {
        fputs("ERROR: Failed to open target.\n", stderr);
        return -1;
    }

    printf("[A]: Stop watching [S]: Resume watching [Q]: Exit\n");

    dirwatcher_set_target_callback(target, callback);
    dirwatcher_start_watch_target(target);

    while (!err_flag)
    {
        int ch = _getch();

        switch (ch)
        {
        case 'a':
            dirwatcher_stop_watch_target(target);
            printf("Stop\n");
            break;
        case 's':
            dirwatcher_start_watch_target(target);
            printf("Resume\n");
            break;
        case 'q':
            printf("Exit\n");
            dirwatcher_close_target(target);
            return 0;
        }
    }

    dirwatcher_close_target(target);
    return 0;
}
