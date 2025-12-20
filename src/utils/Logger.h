#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>
#include <fstream>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <cstdint>

namespace Ups {

    // =========================================================================
    //                                 LogLevel
    // =========================================================================

    enum class LogLevel {
        Debug = 0,
        Info,
        Warning,
        Error,
        Critical
    };

    // =========================================================================
    //                                  Logger
    // =========================================================================

    class Logger {
    public:
        static Logger& Instance() {
            static Logger instance;
            return instance;
        }

        bool Open(const std::string& filename) {
            std::lock_guard<std::mutex> lock(mutex_);

            if (file_.is_open())
                file_.close();

            file_.open(filename, std::ios::out | std::ios::trunc);
            if (!file_.is_open())
                return false;

            lastFlushTick_ = GetTickCount64();
            WriteMarker("Session started");
            return true;
        }

        void Close() {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!file_.is_open())
                return;

            WriteMarker("Session ended");
            file_.close();
        }

        void SetMinLevel(LogLevel level) {
            std::lock_guard<std::mutex> lock(mutex_);
            minLevel_ = level;
        }

        void EnableConsole(bool enable) {
            std::lock_guard<std::mutex> lock(mutex_);
            consoleOutput_ = enable;
        }

        void Log(LogLevel level, const std::string& msg) {
            if (static_cast<int>(level) < static_cast<int>(minLevel_))
                return;

            std::lock_guard<std::mutex> lock(mutex_);

            const std::string line = FormatLine(level, msg);

            if (file_.is_open()) {
                file_ << line << "\n";
                FlushIfNeeded(level);
            }

            if (consoleOutput_) {
                OutputDebugStringA((line + "\n").c_str());
            }

#ifdef _DEBUG
            if (!consoleOutput_) {
                OutputDebugStringA((line + "\n").c_str());
            }
#endif
        }

        void Debug(const std::string& msg) { Log(LogLevel::Debug, msg); }
        void Info(const std::string& msg) { Log(LogLevel::Info, msg); }
        void Warning(const std::string& msg) { Log(LogLevel::Warning, msg); }
        void Error(const std::string& msg) { Log(LogLevel::Error, msg); }
        void Critical(const std::string& msg) { Log(LogLevel::Critical, msg); }

    private:
        Logger() = default;
        ~Logger() { Close(); }

        Logger(const Logger&) = delete;
        Logger& operator=(const Logger&) = delete;

        std::string GetTimestamp() const {
            using namespace std::chrono;

            const auto now = system_clock::now();
            const auto time = system_clock::to_time_t(now);
            const auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

            struct tm tmBuf;
            localtime_s(&tmBuf, &time);

            std::ostringstream oss;
            oss << std::put_time(&tmBuf, "%H:%M:%S")
                << '.' << std::setfill('0') << std::setw(3) << ms.count();
            return oss.str();
        }

        std::string FormatLine(LogLevel level, const std::string& msg) const {
            std::ostringstream oss;
            oss << "[" << GetTimestamp() << "][" << LevelToString(level) << "] " << msg;
            return oss.str();
        }

        void WriteMarker(const char* text) {
            file_ << std::string(60, '=') << "\n"
                << text << ": " << GetTimestamp() << "\n"
                << std::string(60, '=') << "\n";
            file_.flush();
        }

        void FlushIfNeeded(LogLevel level) {
            const ULONGLONG now = GetTickCount64();
            const bool needFlush = (level >= LogLevel::Error) ||
                (now - lastFlushTick_ >= FLUSH_INTERVAL_MS);

            if (needFlush) {
                file_.flush();
                lastFlushTick_ = now;
            }
        }

        static const char* LevelToString(LogLevel level) {
            switch (level) {
            case LogLevel::Debug:    return "DBG";
            case LogLevel::Info:     return "INF";
            case LogLevel::Warning:  return "WRN";
            case LogLevel::Error:    return "ERR";
            case LogLevel::Critical: return "CRT";
            }
            return "???";
        }

        static constexpr uint32_t FLUSH_INTERVAL_MS = 250;

        std::ofstream file_;
        std::mutex mutex_;
        LogLevel minLevel_ = LogLevel::Info;
        ULONGLONG lastFlushTick_ = 0;
        bool consoleOutput_ = true;
    };

} // namespace Ups

// =============================================================================
//                               Log Macros
// =============================================================================

#define LOG_DEBUG(msg) ::Ups::Logger::Instance().Debug(msg)
#define LOG_INFO(msg)  ::Ups::Logger::Instance().Info(msg)
#define LOG_WARN(msg)  ::Ups::Logger::Instance().Warning(msg)
#define LOG_ERROR(msg) ::Ups::Logger::Instance().Error(msg)
#define LOG_CRIT(msg)  ::Ups::Logger::Instance().Critical(msg)