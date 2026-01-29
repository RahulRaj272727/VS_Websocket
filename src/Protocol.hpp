// CodeRabbit: Please review this file thoroughly for production readiness
#pragma once

#include <string>
#include <memory>

/**
 * @namespace Protocol
 * @brief Encapsulates all protocol-level definitions and utilities for TallyIX WebSocket communication.
 * 
 * This namespace handles:
 * - Message type definitions (enum class MessageType)
 * - Data structures for protocol messages (Message, Config)
 * - JSON parsing and serialization functions
 * - Protocol configuration and limits
 * 
 * The protocol is designed to be extensible and human-readable using JSON format.
 * All binary data transfers are preceded by a BinaryStart metadata message.
 */
namespace Protocol
{
    /**
     * @enum MessageType
     * @brief Enumeration of all supported protocol message types.
     * 
     * These types define the possible message types that can be sent or received
     * in the TallyIX WebSocket protocol. Each type has specific handling rules.
     */
    enum class MessageType
    {
        Hello,           ///< Initial handshake message sent when connection is established
        BinaryStart,     ///< Signals the start of binary data transfer with size metadata
        BinaryData,      ///< Raw binary payload (data chunk)
        Acknowledge,     ///< ACK message confirming receipt of a message
        Error,           ///< Error response from the server
        Unknown          ///< Unknown or unhandled message type (for future extensibility)
    };

    /**
     * @struct Message
     * @brief Represents a parsed protocol message with type, ID, content, and size metadata.
     * 
     * This is the primary data structure for all protocol communication. When a message
     * is received (via JSON), it's converted to this typed structure for easier handling.
     */
    struct Message
    {
        MessageType type;        ///< The type of message (Hello, BinaryStart, etc.)
        std::string msgId;       ///< Unique message identifier (e.g., "msg_001")
        std::string content;     ///< Message content/payload (for text messages)
        size_t binarySize = 0;   ///< Expected size of binary data (used in BinaryStart messages)

        /// @brief Default constructor - initializes message as Unknown type
        Message() : type(MessageType::Unknown) {}
        
        /// @brief Constructor with message type, ID, and optional content
        /// @param t The message type
        /// @param id Unique message identifier
        /// @param c Optional message content (default empty string)
        Message(MessageType t, const std::string& id, const std::string& c = "")
            : type(t), msgId(id), content(c) {}
    };

    /**
     * @struct Config
     * @brief Configuration parameters for protocol behavior and limits.
     * 
     * These settings control timeouts, maximum payload sizes, and other protocol behaviors.
     * They should be configured during initialization based on your application's needs.
     * 
     * @warning Due to internal copies during send operations, actual memory usage may be
     *          2-3x the maxBinaryPayloadSize for large transfers. Plan accordingly.
     */
    struct Config
    {
        /// Connection timeout in milliseconds - how long to wait for server to accept connection
        int connectionTimeoutMs = 10000;
        
        /// Message timeout in milliseconds - how long to wait for responses to sent messages
        /// @note Currently reserved for future use - not enforced in current implementation
        int messageTimeoutMs = 5000;
        
        /// Maximum binary payload size in bytes (100MB default) - prevents out-of-memory errors
        /// Adjust based on your hardware and expected message sizes
        /// @warning Memory usage during transfer may be 2-3x this value due to internal copies
        /// @note Must be > 0 and <= 1GB for safety
        size_t maxBinaryPayloadSize = 100 * 1024 * 1024;
        
        /// Flag for compression support
        /// @note Currently reserved for future use - not implemented in current version
        bool enableCompression = false;
        
        /// Protocol version string for compatibility checking (semantic versioning)
        /// @note Currently reserved for future use - not validated in current implementation
        std::string protocolVersion = "1.0";
        
        /// @brief Validate configuration values for safety
        /// @return true if all values are within acceptable bounds
        bool IsValid() const 
        {
            return connectionTimeoutMs > 0 && 
                   messageTimeoutMs > 0 &&
                   maxBinaryPayloadSize > 0 && 
                   maxBinaryPayloadSize <= 1024ULL * 1024 * 1024;  // 1GB max
        }
    };

    /**
     * @brief Convert MessageType enum to human-readable string.
     * 
     * Useful for debugging and logging purposes. Returns the string representation
     * of the message type (e.g., "Hello", "BinaryStart", etc.).
     * 
     * @param type The MessageType enum value to convert
     * @return String representation of the message type
     */
    std::string MessageTypeToString(MessageType type);

    /**
     * @brief Check if a message is valid (has required fields).
     * 
     * A valid message must have:
     * - A known type (not Unknown)
     * - A non-empty message ID
     * 
     * @param msg The message to validate
     * @return true if the message is valid, false otherwise
     */
    bool IsValidMessage(const Message& msg);

    /**
     * @brief Parses a JSON-formatted string into a typed Protocol::Message.
     * 
     * This function extracts protocol message information from JSON format and converts
     * it into a strongly-typed Message structure. It handles:
     * - Type string to MessageType enum conversion
     * - String values (msg_id, content)
     * - Numeric values (size for binary transfers)
     * 
     * @param json The JSON string to parse (e.g., "{\"type\":\"hello\",\"msg_id\":\"123\"}")
     * @return A Protocol::Message object with extracted data; type is Unknown if parsing fails
     * 
     * @note This uses simple string manipulation without external JSON libraries.
     *       Keep content simple to avoid parsing issues (no escaped quotes, etc.).
     */
    Message ParseJsonMessage(const std::string& json);

    /**
     * @brief Serializes a Protocol::Message into JSON format for transmission.
     * 
     * This function converts a typed Message structure into JSON format suitable for
     * transmission over WebSocket. It handles:
     * - MessageType to type string conversion
     * - Escaping and quoting of fields
     * - Conditional inclusion of optional fields
     * 
     * @param msg The Message to serialize
     * @return A JSON string representation of the message
     * 
     * @example
     *   Protocol::Message msg(MessageType::Hello, "msg_001", "Hello");
     *   std::string json = SerializeJsonMessage(msg);
     *   // Result: {"type":"hello","msg_id":"msg_001","content":"Hello"}
     */
    std::string SerializeJsonMessage(const Message& msg);
}
