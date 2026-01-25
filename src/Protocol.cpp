#include "Protocol.hpp"
#include "Logger.hpp"

/**
 * @file Protocol.cpp
 * @brief Implementation of protocol parsing and serialization for TallyIX WebSocket.
 * 
 * This file contains the core logic for converting between wire format (JSON)
 * and the typed Protocol::Message structure. It uses simple string manipulation
 * to avoid external dependencies while maintaining clarity and robustness.
 */

// Protocol namespace implementation
namespace Protocol
{
    /**
     * @brief Helper function to extract a value from JSON by key.
     * 
     * This function searches for a key in JSON format and extracts its value.
     * It handles both string values (quoted) and numeric values (unquoted).
     * 
     * @param json The JSON string to search in
     * @param key The key to find (without quotes)
     * @return The extracted value as a string, or empty string if not found
     * 
     * @note This is a simple implementation that works for basic JSON without
     *       nested objects or special character escaping. For more complex JSON,
     *       consider using a proper JSON library like nlohmann/json.
     */
    std::string GetJsonValue(const std::string& json, const std::string& key)
    {
        // Build the search pattern: "key":
        std::string searchKey = "\"" + key + "\":";
        size_t pos = json.find(searchKey);
        
        // Key not found in JSON
        if (pos == std::string::npos)
            return "";

        // Move position past the key pattern
        pos += searchKey.length();

        // Skip any whitespace after the colon
        while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t'))
            pos++;

        // Handle string values (enclosed in quotes)
        if (pos < json.length() && json[pos] == '"')
        {
            pos++;  // Skip opening quote
            size_t end = json.find('"', pos);
            if (end != std::string::npos)
                return json.substr(pos, end - pos);
        }
        // Handle numeric values (not quoted)
        else
        {
            size_t end = pos;
            // Continue until we hit a comma or closing brace
            while (end < json.length() && json[end] != ',' && json[end] != '}')
                end++;
            return json.substr(pos, end - pos);
        }

        return "";
    }

    std::string MessageTypeToString(MessageType type)
    {
        switch (type)
        {
        case MessageType::Hello:       return "Hello";
        case MessageType::BinaryStart: return "BinaryStart";
        case MessageType::BinaryData:  return "BinaryData";
        case MessageType::Acknowledge: return "Acknowledge";
        case MessageType::Error:       return "Error";
        case MessageType::Unknown:
        default:                       return "Unknown";
        }
    }

    bool IsValidMessage(const Message& msg)
    {
        // A valid message must have a known type and a message ID
        return msg.type != MessageType::Unknown && !msg.msgId.empty();
    }

    Message ParseJsonMessage(const std::string& json)
    {
        Message msg;
        
        // Extract type string from JSON
        std::string typeStr = GetJsonValue(json, "type");

        // Convert type string to MessageType enum
        if (typeStr == "hello")
            msg.type = MessageType::Hello;
        else if (typeStr == "binary_start")
            msg.type = MessageType::BinaryStart;
        else if (typeStr == "binary_data")
            msg.type = MessageType::BinaryData;
        else if (typeStr == "ack")
            msg.type = MessageType::Acknowledge;
        else if (typeStr == "error")
            msg.type = MessageType::Error;
        else
        {
            msg.type = MessageType::Unknown;
            Logger::Instance().Warning("Protocol", 
                "Unknown message type: " + typeStr);
        }

        // Extract message ID
        msg.msgId = GetJsonValue(json, "msg_id");
        
        // Extract message content
        msg.content = GetJsonValue(json, "content");

        // Extract binary size (for BinaryStart messages)
        std::string sizeStr = GetJsonValue(json, "size");
        if (!sizeStr.empty())
        {
            try
            {
                // Convert string to size_t (unsigned long long)
                msg.binarySize = std::stoul(sizeStr);
            }
            catch (const std::exception& e)
            {
                // Handle conversion failure (non-numeric value)
                Logger::Instance().Warning("Protocol", 
                    std::string("Failed to parse binary size: ") + e.what());
                msg.binarySize = 0;
            }
        }

        return msg;
    }

    std::string SerializeJsonMessage(const Message& msg)
    {
        // Convert MessageType enum to string representation
        std::string typeStr;
        switch (msg.type)
        {
        case MessageType::Hello:
            typeStr = "hello";
            break;
        case MessageType::BinaryStart:
            typeStr = "binary_start";
            break;
        case MessageType::BinaryData:
            typeStr = "binary_data";
            break;
        case MessageType::Acknowledge:
            typeStr = "ack";
            break;
        case MessageType::Error:
            typeStr = "error";
            break;
        default:
            typeStr = "unknown";
        }

        // Build JSON string with required fields
        std::string json = "{\"type\":\"" + typeStr + "\",\"msg_id\":\"" + msg.msgId + "\"";

        // Add optional content field if present
        if (!msg.content.empty())
            json += ",\"content\":\"" + msg.content + "\"";

        // Add binary size field if it's a BinaryStart message
        if (msg.binarySize > 0)
            json += ",\"size\":" + std::to_string(msg.binarySize);

        // Close the JSON object
        json += "}";
        return json;
    }
}
