#pragma once

#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

    typedef enum {
        LOG_LEVEL_INFO,
        LOG_LEVEL_ERROR
    } LogLevel;

    int LogOpen(const wchar_t* path);
    void LogClose(void);
    void LogWrite(LogLevel level, const char* format, ...);

#ifdef __cplusplus
}
#endif

#define LOG_INFO(...)  LogWrite(LOG_LEVEL_INFO, __VA_ARGS__)
#define LOG_ERROR(...) LogWrite(LOG_LEVEL_ERROR, __VA_ARGS__)