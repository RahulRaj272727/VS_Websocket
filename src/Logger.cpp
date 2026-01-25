#include "Logger.hpp"
#include <iomanip>

/**
 * @file Logger.cpp
 * @brief Implementation of thread-safe logging system.
 * 
 * This file provides the implementation of the Logger singleton class.
 * All logging goes through std::cout, which is protected by a mutex to ensure
 * thread-safe access from multiple threads simultaneously.
 */

Logger& Logger::Instance()
{
    // Static local variable - initialized only once, thread-safe in C++11 and later
    // This is the standard way to implement a thread-safe singleton in modern C++
    static Logger instance;
    return instance;
}

void Logger::Log(Level level, const std::string& tag, const std::string& message)
{
    // Lock the mutex to ensure exclusive access to std::cout
    // The lock is automatically released when lock_guard goes out of scope
    std::lock_guard<std::mutex> lock(mMutex);
    
    // Format: "HH:MM:SS.mmm [LVL][TAG] message"
    // Example: "14:23:45.123 [INF][WsClient] Connected to server"
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
    // Get current system clock time
    auto now = std::chrono::system_clock::now();
    
    // Convert to time_t for struct tm conversion
    auto time = std::chrono::system_clock::to_time_t(now);
    
    // Extract milliseconds from the duration
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    // Build the timestamp string
    std::stringstream ss;
    
    // Convert to local time (thread-safe on Windows with localtime_s)
    struct tm timeInfo;
    localtime_s(&timeInfo, &time);
    
    // Format: HH:MM:SS
    ss << std::put_time(&timeInfo, "%H:%M:%S")
       << "."                          // Add decimal point
       << std::setfill('0')            // Pad with zeros
       << std::setw(3)                 // Width of 3 for milliseconds
       << ms.count();                  // Milliseconds value
    
    return ss.str();
}

std::string Logger::GetLevelStr(Level level) const
{
    // Convert Level enum to 3-character string
    switch (level)
    {
    case Level::Debug:
        return "DBG";  // Debug
    case Level::Info:
        return "INF";  // Information
    case Level::Warning:
        return "WRN";  // Warning
    case Level::Error:
        return "ERR";  // Error
    default:
        return "???";  // Unknown level (shouldn't happen)
    }
}
