#ifndef LOGGER_H
#define LOGGER_H

#include <string>
#include <mutex>
#include <iostream>

enum class LogLevel {
    LOG_DEBUG = 0,
    LOG_INFO = 1,
    LOG_WARN = 2,
    LOG_ERROR = 3
};

class Logger {
public:
    static Logger& Instance();

    void SetLevel(LogLevel level);
    LogLevel GetLevel() const;

    void Log(LogLevel level, const std::string& message);
    void Debug(const std::string& message);
    void Info(const std::string& message);
    void Warn(const std::string& message);
    void Error(const std::string& message);

private:
    Logger() = default;
    ~Logger() = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    LogLevel m_level = LogLevel::LOG_INFO;
    std::mutex m_mutex;
    std::string GetTimestamp();
    std::string LevelToString(LogLevel level);
};

#define LOG_DEBUG(msg) Logger::Instance().Debug(msg)
#define LOG_INFO(msg)  Logger::Instance().Info(msg)
#define LOG_WARN(msg)  Logger::Instance().Warn(msg)
#define LOG_ERROR(msg) Logger::Instance().Error(msg)

#endif // LOGGER_H
