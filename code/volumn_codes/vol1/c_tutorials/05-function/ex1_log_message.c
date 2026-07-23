#include <stdarg.h>
#include <stdio.h>

typedef enum { LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR } LogLevel;

void log_message(LogLevel level, const char* format, ...) {
    const char* level_str;
    switch (level) {
        case LOG_DEBUG:
            level_str = "DEBUG";
            break;
        case LOG_INFO:
            level_str = "INFO";
            break;
        case LOG_WARN:
            level_str = "WARN";
            break;
        case LOG_ERROR:
            level_str = "ERROR";
            break;
        default:
            level_str = "UNKNOWN";
            break;
    }
    printf("%s\n", level_str);

    va_list args;
    va_start(args, format);

    vprintf(format, args);
    printf("\n");

    va_end(args);
}

int main(void) {
    log_message(LOG_DEBUG, "debug value=%d", 42);
    log_message(LOG_INFO, "system started, port=%d", 8080);
    log_message(LOG_WARN, "low memory: %d MB free", 128);
    log_message(LOG_ERROR, "failed to open %s (code=%d)", "config.txt", -1);
    return 0;
}
