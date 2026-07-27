#ifndef LOGGER_H
#define LOGGER_H

#include <string>
#include <vector>
#include <mutex>

void LogEvent(const std::wstring& msg);
void LogEventA(const std::string& msg);
std::wstring GetLogHistory();
void ClearLogHistory();
std::vector<std::string> GetLogHistoryVector();

#define LOG_INFO(msg) LogEventA(msg)
#define LOG_WARN(msg) LogEventA(msg)
#define LOG_ERROR(msg) LogEventA(msg)

#endif // LOGGER_H
