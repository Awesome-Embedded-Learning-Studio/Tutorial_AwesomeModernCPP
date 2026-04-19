#include <stdio.h>
#include <stdarg.h>

typedef enum { LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR } LogLevel;

static const char* kLevelStrings[] = {
    "DEBUG", "INFO", "WARN", "ERROR"
};

void log_message(LogLevel level, const char* format, ...)
{
    fprintf(stderr, "[%s] ", kLevelStrings[level]);
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fprintf(stderr, "\n");
}

int main(void)
{
    log_message(LOG_DEBUG, "Initializing module %s v%d.%d", "core", 1, 0);
    log_message(LOG_INFO, "Server started on port %d", 8080);
    log_message(LOG_WARN, "Connection pool at %d%% capacity", 85);
    log_message(LOG_ERROR, "Failed to open file: %s", "/tmp/data.bin");

    return 0;
}
