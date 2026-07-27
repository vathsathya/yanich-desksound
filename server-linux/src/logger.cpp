#include "logger.h"
#include "gui_linux.h"
#include <chrono>
#include <iomanip>
#include <sstream>
#include <ctime>

Logger& Logger::Instance() {
    static Logger instance;
    return instance;
}

void Logger::SetLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_level = level;
}

LogLevel Logger::GetLevel() const {
    return m_level;
}

std::string Logger::GetTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::stringstream ss;
    std::tm buf{};
#if defined(_WIN32)
    localtime_s(&buf, &in_time_t);
#else
    localtime_r(&in_time_t, &buf);
#endif
    ss << std::put_time(&buf, "%Y-%m-%d %H:%M:%S")
       << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

std::string Logger::LevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::LOG_DEBUG: return "DEBUG";
        case LogLevel::LOG_INFO:  return "INFO ";
        case LogLevel::LOG_WARN:  return "WARN ";
        case LogLevel::LOG_ERROR: return "ERROR";
        default:                  return "INFO ";
    }
}

void Logger::Log(LogLevel level, const std::string& message) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (static_cast<int>(level) < static_cast<int>(m_level)) return;

    std::string formattedMsg = "[" + GetTimestamp() + "] [" + LevelToString(level) + "] " + message;
    std::cout << formattedMsg << std::endl;

    if (GuiLinux::Instance().HasGTK()) {
        GuiLinux::Instance().AddLogMessage(formattedMsg);
    }
}

void Logger::Debug(const std::string& message) { Log(LogLevel::LOG_DEBUG, message); }
void Logger::Info(const std::string& message)  { Log(LogLevel::LOG_INFO, message); }
void Logger::Warn(const std::string& message)  { Log(LogLevel::LOG_WARN, message); }
void Logger::Error(const std::string& message) { Log(LogLevel::LOG_ERROR, message); }
