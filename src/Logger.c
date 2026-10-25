#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "Logger.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

enum { LINE_SIZE = 1024 };

static FILE* file;
static SRWLOCK lock = SRWLOCK_INIT;

static const char* LevelTag(LogLevel level) {
    if (level == LOG_LEVEL_ERROR)
        return "ERR";
    return "INF";
}

int LogOpen(const wchar_t* path) {
    AcquireSRWLockExclusive(&lock);

    if (file)
        fclose(file);

    file = _wfopen(path, L"wt");
    const int opened = file != NULL;

    ReleaseSRWLockExclusive(&lock);
    return opened;
}

void LogClose(void) {
    AcquireSRWLockExclusive(&lock);

    if (file)
        fclose(file);

    file = NULL;
    ReleaseSRWLockExclusive(&lock);
}

void LogWrite(LogLevel level, const char* format, ...) {
    SYSTEMTIME time;
    GetLocalTime(&time);

    char line[LINE_SIZE];
    const int offset = snprintf(
        line,
        sizeof(line),
        "[%02u:%02u:%02u.%03u][%s] ",
        time.wHour,
        time.wMinute,
        time.wSecond,
        time.wMilliseconds,
        LevelTag(level));

    va_list args;
    va_start(args, format);
    vsnprintf(line + offset, sizeof(line) - offset - 1, format, args);
    va_end(args);

    const size_t length = strlen(line);
    line[length] = '\n';
    line[length + 1] = '\0';

    AcquireSRWLockExclusive(&lock);

    if (file) {
        fputs(line, file);
        fflush(file);
    }

    OutputDebugStringA(line);
    ReleaseSRWLockExclusive(&lock);
}