#pragma once

#include <string>
#include <mutex>
#include <iostream>
#include <sstream>
#include <chrono>

/**
 * @class Logger
 * @brief Thread-safe singleton logging system for the WebSocket client.
 * 
 * This class provides centralized, thread-safe logging functionality across the entire
 * application. It ensures that log messages from multiple threads never interleave,
 * which would result in garbled output.
 * 
 * Features:
 * - Thread-safe console output via mutex protection
 * - Multiple severity levels (Debug, Info, Warning, Error)
 * - Timestamps with millisecond precision
 * - Tag-based categorization (e.g., "[DBG][WsClient]")
 * - Singleton pattern (single instance for entire application)
 * 
 * @note The logger uses std::cout internally, so output can be redirected at application startup.
 * 
 * @example
 *   // Get singleton instance and log messages
 *   Logger::Instance().Info("MyComponent", "Application started successfully");
 *   Logger::Instance().Error("MyComponent", "Failed to connect: " + error_reason);
 */
class Logger
{
public:
    /**
     * @enum Level
     * @brief Severity levels for log messages.
     * 
     * These levels help filter and understand message importance:
     * - Debug: Detailed diagnostic information (lowest priority)
     * - Info: General informational messages
     * - Warning: Warning conditions that should be investigated
     * - Error: Error conditions (highest priority)
     */
    enum class Level
    {
        Debug,      ///< Detailed diagnostic information - for development
        Info,       ///< General informational messages
        Warning,    ///< Warning conditions - potential issues
        Error       ///< Error conditions - serious issues requiring attention
    };

    /**
     * @brief Get the singleton Logger instance.
     * 
     * Returns a reference to the single Logger instance for the entire application.
     * The first call creates the instance; subsequent calls return the same instance.
     * This is thread-safe due to static local variable initialization.
     * 
     * @return Reference to the global Logger instance
     * 
     * @example
     *   Logger::Instance().Info("App", "Starting");
     */
    static Logger& Instance();

    /**
     * @brief Set the minimum log level to display.
     * 
     * Messages below this level will be silently ignored. Useful for reducing
     * noise in production builds while keeping verbose logging in debug builds.
     * 
     * @param level Minimum level to display (Debug shows all, Error shows only errors)
     * 
     * @example
     *   // In production, only show warnings and errors
     *   Logger::Instance().SetMinLevel(Logger::Level::Warning);
     */
    void SetMinLevel(Level level);

    /**
     * @brief Log a message with specified severity level and tag.
     * 
     * @param level The severity level (Debug, Info, Warning, Error)
     * @param tag A component/module tag (e.g., "WsClient", "App", "Protocol")
     * @param message The message content to log
     * 
     * @note This is the core logging method; other methods call this internally.
     */
    void Log(Level level, const std::string& tag, const std::string& message);

    /**
     * @brief Log a debug message (lowest priority).
     * 
     * Use for detailed diagnostic information during development.
     * 
     * @param tag Component or module identifier
     * @param message Diagnostic message content
     */
    void Debug(const std::string& tag, const std::string& message);

    /**
     * @brief Log an informational message (normal priority).
     * 
     * Use for general application flow and status updates.
     * 
     * @param tag Component or module identifier
     * @param message Information message content
     */
    void Info(const std::string& tag, const std::string& message);

    /**
     * @brief Log a warning message (high priority).
     * 
     * Use when something unexpected happens but execution can continue.
     * 
     * @param tag Component or module identifier
     * @param message Warning message content
     */
    void Warning(const std::string& tag, const std::string& message);

    /**
     * @brief Log an error message (highest priority).
     * 
     * Use when a serious error occurs that prevents normal operation.
     * 
     * @param tag Component or module identifier
     * @param message Error message content
     */
    void Error(const std::string& tag, const std::string& message);

private:
    /// @brief Private constructor - use Logger::Instance() instead
    Logger() = default;
    
    /// @brief Private destructor
    ~Logger() = default;

    /// @brief Prevent copying (singleton pattern)
    Logger(const Logger&) = delete;
    
    /// @brief Prevent assignment (singleton pattern)
    Logger& operator=(const Logger&) = delete;

    /// @brief Mutex protecting concurrent access to std::cout
    mutable std::mutex mMutex;

    /// @brief Minimum log level to display (messages below this are ignored)
    Level mMinLevel = Level::Debug;

    /**
     * @brief Generate current timestamp as string with milliseconds.
     * 
     * @return Timestamp in format "HH:MM:SS.mmm"
     */
    std::string GetTimestamp() const;

    /**
     * @brief Convert severity level to short string representation.
     * 
     * @param level The severity level
     * @return String representation (e.g., "DBG", "INF", "WRN", "ERR")
     */
    std::string GetLevelStr(Level level) const;
};
