#include "Logger.hpp"
#include <iomanip>

Logger& Logger::Instance()
{
    static Logger instance;
    return instance;
}

void Logger::Log(Level level, const std::string& tag, const std::string& message)
{
    std::lock_guard<std::mutex> lock(mMutex);
    std::cout << GetTimestamp() << " [" << GetLevelStr(level) << "][" << tag << "] "
              << message << "\n";
}

void Logger::Debug(const std::string& tag, const std::string& message)
{
    Log(Level::Debug, tag, message);
}

void Logger::Info(const std::string& tag, const std::string& message)
{
    Log(Level::Info, tag, message);
}

void Logger::Warning(const std::string& tag, const std::string& message)
{
    Log(Level::Warning, tag, message);
}

void Logger::Error(const std::string& tag, const std::string& message)
{
    Log(Level::Error, tag, message);
}

std::string Logger::GetTimestamp() const
{
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::stringstream ss;
    struct tm timeInfo;
    localtime_s(&timeInfo, &time);
    ss << std::put_time(&timeInfo, "%H:%M:%S") << "." << std::setfill('0')
       << std::setw(3) << ms.count();
    return ss.str();
}

std::string Logger::GetLevelStr(Level level) const
{
    switch (level)
    {
    case Level::Debug:
        return "DBG";
    case Level::Info:
        return "INF";
    case Level::Warning:
        return "WRN";
    case Level::Error:
        return "ERR";
    default:
        return "???";
    }
}
