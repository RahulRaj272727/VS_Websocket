#pragma once

#include <string>
#include <memory>

// Protocol-level message structures for TallyIX WebSocket communication
namespace Protocol
{
    // Message types for TallyIX protocol
    enum class MessageType
    {
        Hello,           // Initial handshake
        BinaryStart,     // Signals incoming binary data with metadata
        BinaryData,      // Raw binary payload
        Acknowledge,     // ACK for received messages
        Error,           // Error response
        Unknown          // Unknown/unhandled type
    };

    struct Message
    {
        MessageType type;
        std::string msgId;
        std::string content;
        size_t binarySize = 0;  // For BinaryStart message

        Message() : type(MessageType::Unknown) {}
        Message(MessageType t, const std::string& id, const std::string& c = "")
            : type(t), msgId(id), content(c) {}
    };

    // Configuration for protocol behavior
    struct Config
    {
        int connectionTimeoutMs = 10000;       // Timeout for connection establishment
        int messageTimeoutMs = 5000;           // Timeout for waiting on responses
        size_t maxBinaryPayloadSize = 100 * 1024 * 1024;  // 100MB max per message
        bool enableCompression = false;
        std::string protocolVersion = "1.0";
    };

    // Parse JSON message to Protocol::Message
    Message ParseJsonMessage(const std::string& json);

    // Serialize Protocol::Message to JSON
    std::string SerializeJsonMessage(const Message& msg);
}
