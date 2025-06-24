#include "Logger.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <filesystem>

// Undefine Windows macros that conflict with our enum
#ifdef ERROR
#undef ERROR
#endif

#include <Windows.h>

// Static member definitions
std::unique_ptr<Logger> Logger::s_instance = nullptr;
std::mutex Logger::s_mutex;

Logger::Logger() : m_minLevel(LogLevel::DEBUG) {
    // Constructor is private - use Initialize()
}

Logger::~Logger() {
    if (m_logFile.is_open()) {
        m_logFile.close();
    }
}

Logger* Logger::GetInstance() {
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_instance) {
        s_instance = std::unique_ptr<Logger>(new Logger());
    }
    return s_instance.get();
}

void Logger::Initialize(const std::string& logFilePath) {
    try {
        auto instance = GetInstance();
        std::lock_guard<std::mutex> lock(instance->m_fileMutex);
        
        std::string finalPath = logFilePath;
        if (finalPath.empty()) {
            try {
                // Get the DLL's directory
                char dllPath[MAX_PATH];
                HMODULE hModule = GetModuleHandle(L"WorldToScreenTesting.dll");
                if (hModule && GetModuleFileNameA(hModule, dllPath, MAX_PATH)) {
                    std::filesystem::path dllDir = std::filesystem::path(dllPath).parent_path();
                    finalPath = (dllDir / "WorldToScreenTesting.log").string();
                } else {
                    // Fallback to temp directory
                    char tempPath[MAX_PATH];
                    if (GetTempPathA(MAX_PATH, tempPath)) {
                        finalPath = std::string(tempPath) + "WorldToScreenTesting.log";
                    } else {
                        // Last resort - current directory
                        finalPath = "WorldToScreenTesting.log";
                    }
                }
            } catch (...) {
                // If path operations fail, use simple filename
                finalPath = "WorldToScreenTesting.log";
            }
        }
        
        // Try to ensure directory exists, but don't fail if it doesn't work
        try {
            std::filesystem::path logPath(finalPath);
            std::filesystem::create_directories(logPath.parent_path());
        } catch (...) {
            // Ignore directory creation errors
        }
        
        instance->m_logFile.open(finalPath, std::ios::out | std::ios::trunc);
        
        if (instance->m_logFile.is_open()) {
            instance->Log(LogLevel::INFO, "=== WorldToScreenTesting Logger Initialized ===");
            instance->Log(LogLevel::INFO, "Log file: " + finalPath);
        }
    } catch (...) {
        // If all else fails, just use OutputDebugString
        OutputDebugStringA("WorldToScreenTesting: Logger initialization failed, using debug output only");
    }
}

void Logger::Shutdown() {
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_instance) {
        s_instance->Log(LogLevel::INFO, "=== Logger Shutdown ===");
        s_instance.reset();
    }
}

std::string Logger::GetTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    ss << "." << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

std::string Logger::LogLevelToString(LogLevel level) const {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO: return "INFO";
        case LogLevel::WARNING: return "WARN";
        case LogLevel::LOG_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

void Logger::Log(LogLevel level, const std::string& message) {
    if (level < m_minLevel) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(m_fileMutex);
    
    if (m_logFile.is_open()) {
        m_logFile << "[" << GetTimestamp() << "] "
                  << "[" << LogLevelToString(level) << "] "
                  << message << std::endl;
        m_logFile.flush();
    }
    
    // Also output to debug console for development
    #ifdef _DEBUG
    std::string debugMsg = "[" + LogLevelToString(level) + "] " + message;
    OutputDebugStringA(debugMsg.c_str());
    #endif
} 