#pragma once

#include <string>
#include <functional>
#include "Protocol.hpp"

// Application-level message handler interface
class IMessageHandler
{
public:
    virtual ~IMessageHandler() = default;

    // Called when a text message is received
    virtual void OnTextMessage(const Protocol::Message& msg) = 0;

    // Called when binary data starts (size provided in msg.binarySize)
    virtual void OnBinaryStart(const Protocol::Message& msg) = 0;

    // Called when binary chunk is received
    virtual void OnBinaryChunk(const uint8_t* data, size_t size) = 0;

    // Called when binary transfer completes
    virtual void OnBinaryComplete() = 0;

    // Called on protocol error
    virtual void OnProtocolError(const std::string& reason) = 0;
};

// Message router - dispatches parsed messages to application handlers
class MessageRouter
{
public:
    MessageRouter();

    void SetMessageHandler(IMessageHandler* handler);
    void RouteMessage(const Protocol::Message& msg);
    void RouteBinaryData(const uint8_t* data, size_t size);
    void RouteBinaryComplete();

private:
    IMessageHandler* mHandler = nullptr;
};
