// CodeRabbit: Please review this file thoroughly for production readiness
#include "MessageHandler.hpp"
#include "Logger.hpp"

/**
 * @file MessageHandler.cpp
 * @brief Implementation of message routing from protocol layer to application logic.
 * 
 * This file implements the MessageRouter class which acts as a dispatcher,
 * routing parsed protocol messages to the appropriate handler methods based
 * on the message type. This decouples the protocol layer from application logic.
 */

MessageRouter::MessageRouter()
{
    // Constructor is minimal - all state is initialized in-class
}

void MessageRouter::SetMessageHandler(IMessageHandler* handler)
{
    // Store the handler pointer for later routing
    // Note: This should be called before messages start arriving
    mHandler = handler;
    
    if (handler)
    {
        Logger::Instance().Debug("MessageRouter", 
            "Message handler attached");
    }
    else
    {
        Logger::Instance().Debug("MessageRouter", 
            "Message handler detached (set to null)");
    }
}

void MessageRouter::RouteMessage(const Protocol::Message& msg)
{
    // Check if a handler is attached
    if (!mHandler)
    {
        Logger::Instance().Warning("MessageRouter", 
            "No handler set for message routing - message dropped");
        return;
    }

    // Route the message to the appropriate handler method based on type
    switch (msg.type)
    {
    // Hello and Acknowledge are both text messages
    case Protocol::MessageType::Hello:
        Logger::Instance().Debug("MessageRouter", 
            "Routing Hello message: " + msg.msgId);
        mHandler->OnTextMessage(msg);
        break;

    case Protocol::MessageType::Acknowledge:
        Logger::Instance().Debug("MessageRouter", 
            "Routing Acknowledge message: " + msg.msgId);
        mHandler->OnTextMessage(msg);
        break;

    // Binary transfer starts with metadata
    case Protocol::MessageType::BinaryStart:
        Logger::Instance().Debug("MessageRouter", 
            "Routing BinaryStart: " + std::to_string(msg.binarySize) + " bytes");
        mHandler->OnBinaryStart(msg);
        break;

    // Protocol errors are reported separately
    case Protocol::MessageType::Error:
        Logger::Instance().Warning("MessageRouter", 
            "Routing Error message: " + msg.content);
        mHandler->OnProtocolError(msg.content);
        break;

    // Unknown message types are protocol violations - report to application
    case Protocol::MessageType::Unknown:
    case Protocol::MessageType::BinaryData:  // Should not arrive as text
    default:
        {
            std::string errorMsg = "Unhandled or invalid message type: " + 
                                   std::to_string(static_cast<int>(msg.type)) +
                                   " (msgId: " + msg.msgId + ")";
            
            Logger::Instance().Warning("MessageRouter", errorMsg);
            
            // Notify application of protocol anomaly so it can react
            mHandler->OnProtocolError(errorMsg);
        }
        break;
    }
}

void MessageRouter::RouteBinaryData(const uint8_t* data, size_t size)
{
    // Check if a handler is attached
    if (!mHandler)
    {
        Logger::Instance().Warning("MessageRouter", 
            "No handler set for binary data - data dropped");
        return;
    }

    // Sanity checks
    if (!data || size == 0)
    {
        Logger::Instance().Warning("MessageRouter", 
            std::string("Invalid binary chunk: ") + (data ? "empty" : "null pointer"));
        return;
    }

    // Route binary data to handler
    Logger::Instance().Debug("MessageRouter", 
        "Routing binary chunk: " + std::to_string(size) + " bytes");
    mHandler->OnBinaryChunk(data, size);
}

void MessageRouter::RouteBinaryComplete()
{
    // Check if a handler is attached
    if (!mHandler)
    {
        Logger::Instance().Warning("MessageRouter", 
            "No handler set for binary completion");
        return;
    }

    // Notify handler that binary transfer is complete
    Logger::Instance().Debug("MessageRouter", "Binary transfer completed");
    mHandler->OnBinaryComplete();
}

void MessageRouter::RouteProtocolError(const std::string& errorMsg)
{
    // Check if a handler is attached
    if (!mHandler)
    {
        Logger::Instance().Warning("MessageRouter", 
            "No handler set for protocol error: " + errorMsg);
        return;
    }

    // Route error to handler
    Logger::Instance().Warning("MessageRouter", 
        "Routing protocol error: " + errorMsg);
    mHandler->OnProtocolError(errorMsg);
}

void MessageRouter::RoutePing(const std::string& payload)
{
    // Check if a handler is attached
    if (!mHandler)
    {
        Logger::Instance().Debug("MessageRouter", 
            "No handler set for ping notification");
        return;
    }

    // Route ping to handler (informational only - pong is auto-sent by IXWebSocket)
    Logger::Instance().Debug("MessageRouter", 
        "Routing ping received" + (payload.empty() ? "" : ": " + payload));
    mHandler->OnPing(payload);
}

void MessageRouter::RoutePong(const std::string& payload)
{
    // Check if a handler is attached
    if (!mHandler)
    {
        Logger::Instance().Debug("MessageRouter", 
            "No handler set for pong notification");
        return;
    }

    // Route pong to handler
    Logger::Instance().Debug("MessageRouter", 
        "Routing pong received" + (payload.empty() ? "" : ": " + payload));
    mHandler->OnPong(payload);
}
