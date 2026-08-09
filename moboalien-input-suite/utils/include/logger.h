#pragma once
#include <string>
#include <fstream>
#include <mutex>
#include <memory>

#ifdef ANDROID
#include <android/log.h>
#endif

#ifdef DEBUG
#undef DEBUG
#endif

enum class LogLevel {
    VERBOSE = 0,
    DEBUG = 1,
    INFO = 2,
    WARNING = 3,
    ERROR_LEVEL = 4,
    FATAL = 5
};

class Logger {
public:
    static Logger& GetInstance();
    
    void SetLogLevel(LogLevel level);
    void SetOutputToConsole(bool enable);
    void SetOutputToFile(bool enable, const std::string& filename = "controller.log");
    
    void Log(LogLevel level, const std::string& tag, const std::string& message);
    
    // Convenience methods
    void V(const std::string& tag, const std::string& message) { Log(LogLevel::VERBOSE, tag, message); }
    void D(const std::string& tag, const std::string& message) { Log(LogLevel::DEBUG, tag, message); }
    void I(const std::string& tag, const std::string& message) { Log(LogLevel::INFO, tag, message); }
    void W(const std::string& tag, const std::string& message) { Log(LogLevel::WARNING, tag, message); }
    void E(const std::string& tag, const std::string& message) { Log(LogLevel::ERROR_LEVEL, tag, message); }
    void F(const std::string& tag, const std::string& message) { Log(LogLevel::FATAL, tag, message); }

private:
    Logger() = default;
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    
    std::string GetTimestamp() const;
    std::string GetLevelString(LogLevel level) const;
    std::string FormatMessage(LogLevel level, const std::string& tag, const std::string& message) const;
    
    mutable std::mutex m_mutex;
    LogLevel m_minLevel = LogLevel::INFO;
    bool m_consoleOutput = true;
    bool m_fileOutput = false;
    std::unique_ptr<std::ofstream> m_logFile;
};

// Convenience macros similar to Android logcat
#define LOGV(tag, msg) Logger::GetInstance().V(tag, msg)
#define LOGD(tag, msg) Logger::GetInstance().D(tag, msg)
#define LOGI(tag, msg) Logger::GetInstance().I(tag, msg)
#define LOGW(tag, msg) Logger::GetInstance().W(tag, msg)
#define LOGE(tag, msg) Logger::GetInstance().E(tag, msg)
#define LOGF(tag, msg) Logger::GetInstance().F(tag, msg)
