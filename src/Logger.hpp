#pragma once

#include <string>
#include <mutex>
#include <iostream>
#include <sstream>
#include <chrono>

// Thread-safe logging singleton for POC
class Logger
{
public:
    enum class Level
    {
        Debug,
        Info,
        Warning,
        Error
    };

    static Logger& Instance();

    void Log(Level level, const std::string& tag, const std::string& message);
    void Debug(const std::string& tag, const std::string& message);
    void Info(const std::string& tag, const std::string& message);
    void Warning(const std::string& tag, const std::string& message);
    void Error(const std::string& tag, const std::string& message);

private:
    Logger() = default;
    ~Logger() = default;

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    std::mutex mMutex;

    std::string GetTimestamp() const;
    std::string GetLevelStr(Level level) const;
};
