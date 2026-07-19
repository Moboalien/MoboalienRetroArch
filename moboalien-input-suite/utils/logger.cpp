#include "logger.h"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <thread>

Logger& Logger::GetInstance() {
    static Logger instance;
    return instance;
}

Logger::~Logger() {
    if (m_logFile && m_logFile->is_open()) {
        m_logFile->close();
    }
}

void Logger::SetLogLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_minLevel = level;
}

void Logger::SetOutputToConsole(bool enable) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_consoleOutput = enable;
}

void Logger::SetOutputToFile(bool enable, const std::string& filename) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_fileOutput = enable;
    
    if (enable) {
        if (m_logFile && m_logFile->is_open()) {
            m_logFile->close();
        }
        m_logFile = std::make_unique<std::ofstream>(filename, std::ios::app);
        if (!m_logFile->is_open()) {
            m_fileOutput = false;
            std::cerr << "Failed to open log file: " << filename << std::endl;
        }
        else {
            *m_logFile << std::unitbuf;
        }
    } else {
        if (m_logFile && m_logFile->is_open()) {
            m_logFile->close();
        }
        m_logFile.reset();
    }
}

void Logger::Log(LogLevel level, const std::string& tag, const std::string& message) {
    if (level < m_minLevel) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(m_mutex);

#ifdef ANDROID
    android_LogPriority priority;
    switch (level) {
        case LogLevel::VERBOSE:     priority = ANDROID_LOG_VERBOSE; break;
        case LogLevel::DEBUG:       priority = ANDROID_LOG_DEBUG;   break;
        case LogLevel::INFO:        priority = ANDROID_LOG_INFO;    break;
        case LogLevel::WARNING:     priority = ANDROID_LOG_WARN;    break;
        case LogLevel::ERROR_LEVEL: priority = ANDROID_LOG_ERROR;   break;
        case LogLevel::FATAL:       priority = ANDROID_LOG_FATAL;   break;
        default:                    priority = ANDROID_LOG_UNKNOWN; break;
    }
    __android_log_print(priority, tag.c_str(), "%s", message.c_str());
#endif

    std::string formattedMessage = FormatMessage(level, tag, message);
    
    if (m_consoleOutput) {
        if (level >= LogLevel::ERROR_LEVEL) {
            std::cerr << formattedMessage << std::endl;
        } else {
            std::cout << formattedMessage << std::endl;
        }
    }
    
    if (m_fileOutput && m_logFile && m_logFile->is_open()) {
        *m_logFile << formattedMessage << std::endl;
    }
}

std::string Logger::GetTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%m-%d %H:%M:%S");
    ss << "." << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

std::string Logger::GetLevelString(LogLevel level) const {
    switch (level) {
        case LogLevel::VERBOSE: return "V";
        case LogLevel::DEBUG:   return "D";
        case LogLevel::INFO:    return "I";
        case LogLevel::WARNING: return "W";
        case LogLevel::ERROR_LEVEL:   return "E";
        case LogLevel::FATAL:   return "F";
        default:                return "?";
    }
}

std::string Logger::FormatMessage(LogLevel level, const std::string& tag, const std::string& message) const {
    std::stringstream ss;
    ss << GetTimestamp() << " " << std::this_thread::get_id() << " " << GetLevelString(level) << "/" << tag << ": " << message;
    return ss.str();
}