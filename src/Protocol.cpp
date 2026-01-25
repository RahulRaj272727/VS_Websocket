#include "Protocol.hpp"
#include "Logger.hpp"

// Simple JSON parsing without external dependencies
namespace Protocol
{
    std::string GetJsonValue(const std::string& json, const std::string& key)
    {
        std::string searchKey = "\"" + key + "\":";
        size_t pos = json.find(searchKey);
        if (pos == std::string::npos)
            return "";

        pos += searchKey.length();

        // Skip whitespace
        while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t'))
            pos++;

        // Handle string values (quoted)
        if (pos < json.length() && json[pos] == '"')
        {
            pos++;
            size_t end = json.find('"', pos);
            if (end != std::string::npos)
                return json.substr(pos, end - pos);
        }
        // Handle numeric values
        else
        {
            size_t end = pos;
            while (end < json.length() && json[end] != ',' && json[end] != '}')
                end++;
            return json.substr(pos, end - pos);
        }

        return "";
    }

    Message ParseJsonMessage(const std::string& json)
    {
        Message msg;
        std::string typeStr = GetJsonValue(json, "type");

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
            msg.type = MessageType::Unknown;

        msg.msgId = GetJsonValue(json, "msg_id");
        msg.content = GetJsonValue(json, "content");

        std::string sizeStr = GetJsonValue(json, "size");
        if (!sizeStr.empty())
        {
            try
            {
                msg.binarySize = std::stoul(sizeStr);
            }
            catch (...)
            {
                Logger::Instance().Warning("Protocol", "Failed to parse binary size");
            }
        }

        return msg;
    }

    std::string SerializeJsonMessage(const Message& msg)
    {
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

        std::string json = "{\"type\":\"" + typeStr + "\",\"msg_id\":\"" + msg.msgId + "\"";

        if (!msg.content.empty())
            json += ",\"content\":\"" + msg.content + "\"";

        if (msg.binarySize > 0)
            json += ",\"size\":" + std::to_string(msg.binarySize);

        json += "}";
        return json;
    }
}
