#include "../include/logger.h"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>

static std::mutex g_logMutex;
static std::vector<std::string> g_logHistory;

static std::string GetCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

void LogEventA(const std::string& msg) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    std::string timeStr = GetCurrentTimestamp();
    std::string fullMsg = "[" + timeStr + "] " + msg;
    std::cout << fullMsg << std::endl;

    g_logHistory.push_back(fullMsg);
    if (g_logHistory.size() > 500) {
        g_logHistory.erase(g_logHistory.begin());
    }
}

void LogEvent(const std::wstring& msg) {
    std::string narrowMsg(msg.begin(), msg.end());
    LogEventA(narrowMsg);
}

std::wstring GetLogHistory() {
    std::lock_guard<std::mutex> lock(g_logMutex);
    std::string result;
    for (const auto& line : g_logHistory) {
        result += line + "\r\n";
    }
    return std::wstring(result.begin(), result.end());
}

void ClearLogHistory() {
    std::lock_guard<std::mutex> lock(g_logMutex);
    g_logHistory.clear();
}

std::vector<std::string> GetLogHistoryVector() {
    std::lock_guard<std::mutex> lock(g_logMutex);
    return g_logHistory;
}
