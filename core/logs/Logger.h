#pragma once

#include <string>
#include <fstream>
#include <memory>
#include <mutex>

// Define this before any Windows headers to avoid macro conflicts
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

enum class LogLevel {
    DEBUG = 0,
    INFO = 1,
    WARNING = 2,
    LOG_ERROR = 3  // Renamed to avoid Windows ERROR macro conflict
};

class Logger {
private:
    static std::unique_ptr<Logger> s_instance;
    static std::mutex s_mutex;
    
    std::ofstream m_logFile;
    std::mutex m_fileMutex;
    LogLevel m_minLevel;
    
    Logger();
    std::string GetTimestamp() const;
    std::string LogLevelToString(LogLevel level) const;
    
public:
    ~Logger();
    
    static Logger* GetInstance();
    static void Initialize(HMODULE hModule, const std::string& logFilePath = "");
    static void Shutdown();
    
    void SetLogLevel(LogLevel level) { m_minLevel = level; }
    
    void Log(LogLevel level, const std::string& message);
    void Debug(const std::string& message) { Log(LogLevel::DEBUG, message); }
    void Info(const std::string& message) { Log(LogLevel::INFO, message); }
    void Warning(const std::string& message) { Log(LogLevel::WARNING, message); }
    void Error(const std::string& message) { Log(LogLevel::LOG_ERROR, message); }
};

// Convenience macros
#define LOG_DEBUG(msg) Logger::GetInstance()->Debug(msg)
#define LOG_INFO(msg) Logger::GetInstance()->Info(msg)
#define LOG_WARNING(msg) Logger::GetInstance()->Warning(msg)
#define LOG_ERROR(msg) Logger::GetInstance()->Error(msg) 