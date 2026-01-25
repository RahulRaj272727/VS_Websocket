#include "MessageHandler.hpp"
#include "Logger.hpp"

MessageRouter::MessageRouter()
{
}

void MessageRouter::SetMessageHandler(IMessageHandler* handler)
{
    mHandler = handler;
}

void MessageRouter::RouteMessage(const Protocol::Message& msg)
{
    if (!mHandler)
    {
        Logger::Instance().Warning("MessageRouter", "No handler set for message routing");
        return;
    }

    switch (msg.type)
    {
    case Protocol::MessageType::Hello:
    case Protocol::MessageType::Acknowledge:
        mHandler->OnTextMessage(msg);
        break;

    case Protocol::MessageType::BinaryStart:
        mHandler->OnBinaryStart(msg);
        break;

    case Protocol::MessageType::Error:
        mHandler->OnProtocolError(msg.content);
        break;

    default:
        Logger::Instance().Warning("MessageRouter", "Unhandled message type");
    }
}

void MessageRouter::RouteBinaryData(const uint8_t* data, size_t size)
{
    if (!mHandler)
        return;
    mHandler->OnBinaryChunk(data, size);
}

void MessageRouter::RouteBinaryComplete()
{
    if (!mHandler)
        return;
    mHandler->OnBinaryComplete();
}
