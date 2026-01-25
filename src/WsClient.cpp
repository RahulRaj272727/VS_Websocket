#include "WsClient.hpp"
#include "Logger.hpp"

#include <iostream>
#include <vector>
#include <thread>

#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>

class WsClient::Impl
{
public:
    ix::WebSocket ws;
    Protocol::Config config;
    ConnectionState state = ConnectionState::Disconnected;
    std::mutex stateMutex;
    std::condition_variable stateCV;
    MessageRouter messageRouter;
    size_t binaryBytesReceived = 0;
    size_t binaryExpectedSize = 0;
};

WsClient::WsClient(const Protocol::Config& config)
    : mImpl(std::make_unique<Impl>())
{
    mImpl->config = config;
}

WsClient::~WsClient()
{
    Close();
}

bool WsClient::Open()
{
    if (!ix::initNetSystem())
    {
        Logger::Instance().Error("WsClient", "Failed to init network system");
        return false;
    }

    mImpl->ws.disableAutomaticReconnection();

    // Set up callback with thread-safe state management
    mImpl->ws.setOnMessageCallback(
        [this](const ix::WebSocketMessagePtr& msg)
        {
            switch (msg->type)
            {
            case ix::WebSocketMessageType::Open:
                OnOpen();
                break;

            case ix::WebSocketMessageType::Message:
                OnMessage(msg->str, msg->binary);
                break;

            case ix::WebSocketMessageType::Close:
                OnClose();
                break;

            case ix::WebSocketMessageType::Error:
                OnError(msg->errorInfo.reason);
                break;

            default:
                break;
            }
        });

    Logger::Instance().Info("WsClient", "Network system initialized");
    return true;
}

bool WsClient::Connect(const std::string& pUrl)
{
    {
        std::lock_guard<std::mutex> lock(mImpl->stateMutex);
        if (mImpl->state != ConnectionState::Disconnected)
        {
            Logger::Instance().Warning("WsClient", "Cannot connect: already connecting or connected");
            return false;
        }
        mImpl->state = ConnectionState::Connecting;
    }

    mImpl->ws.setUrl(pUrl);
    mImpl->ws.start();
    Logger::Instance().Info("WsClient", "Connection initiated to " + pUrl);
    return true;
}

bool WsClient::WaitForConnection(int timeoutMs)
{
    std::unique_lock<std::mutex> lock(mImpl->stateMutex);
    bool connected = mImpl->stateCV.wait_for(lock, std::chrono::milliseconds(timeoutMs),
        [this]() { return mImpl->state == ConnectionState::Connected; });

    if (connected)
    {
        Logger::Instance().Info("WsClient", "Connected successfully");
        return true;
    }

    Logger::Instance().Error("WsClient", "Connection timeout or failed");
    return false;
}

bool WsClient::SendText(const std::string& pText)
{
    {
        std::lock_guard<std::mutex> lock(mImpl->stateMutex);
        if (mImpl->state != ConnectionState::Connected)
        {
            Logger::Instance().Warning("WsClient", "Cannot send: not connected");
            return false;
        }
    }

    mImpl->ws.send(pText);
    Logger::Instance().Debug("WsClient", "[SEND][TEXT] " + pText);
    return true;
}

bool WsClient::SendBinary(const void* pData, size_t pSize)
{
    {
        std::lock_guard<std::mutex> lock(mImpl->stateMutex);
        if (mImpl->state != ConnectionState::Connected)
        {
            Logger::Instance().Warning("WsClient", "Cannot send: not connected");
            return false;
        }
    }

    if (pSize > mImpl->config.maxBinaryPayloadSize)
    {
        Logger::Instance().Error("WsClient", 
            "Binary payload exceeds max size: " + std::to_string(pSize));
        return false;
    }

    const char* data = reinterpret_cast<const char*>(pData);
    mImpl->ws.sendBinary(std::string(data, pSize));
    Logger::Instance().Debug("WsClient", "[SEND][BINARY] size=" + std::to_string(pSize));
    return true;
}

WsClient::ConnectionState WsClient::GetState() const
{
    std::lock_guard<std::mutex> lock(mImpl->stateMutex);
    return mImpl->state;
}

void WsClient::SetMessageHandler(IMessageHandler* handler)
{
    mImpl->messageRouter.SetMessageHandler(handler);
}

void WsClient::Close()
{
    {
        std::lock_guard<std::mutex> lock(mImpl->stateMutex);
        if (mImpl->state == ConnectionState::Disconnected)
            return;
        mImpl->state = ConnectionState::Closing;
    }

    mImpl->ws.stop();
    
    // Give thread time to exit gracefully
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    Logger::Instance().Info("WsClient", "Connection closed");
}

//
// Callback handlers (IXWebSocket thread context - use mutex for state changes)
//

void WsClient::OnOpen()
{
    {
        std::lock_guard<std::mutex> lock(mImpl->stateMutex);
        mImpl->state = ConnectionState::Connected;
    }
    mImpl->stateCV.notify_all();

    Logger::Instance().Info("WsClient", "Connected to server");
}

void WsClient::OnMessage(const std::string& pMsg, bool pIsBinary)
{
    if (pIsBinary)
    {
        Logger::Instance().Debug("WsClient", "[RECV][BINARY] size=" + std::to_string(pMsg.size()));
        mImpl->binaryBytesReceived += pMsg.size();
        mImpl->messageRouter.RouteBinaryData(
            reinterpret_cast<const uint8_t*>(pMsg.data()), pMsg.size());

        // Check if binary transfer complete
        if (mImpl->binaryBytesReceived >= mImpl->binaryExpectedSize)
        {
            mImpl->messageRouter.RouteBinaryComplete();
            mImpl->binaryBytesReceived = 0;
            mImpl->binaryExpectedSize = 0;
        }
    }
    else
    {
        Logger::Instance().Debug("WsClient", "[RECV][TEXT] " + pMsg);
        
        // Parse and route message
        Protocol::Message msg = Protocol::ParseJsonMessage(pMsg);
        
        if (msg.type == Protocol::MessageType::BinaryStart)
        {
            mImpl->binaryExpectedSize = msg.binarySize;
            mImpl->binaryBytesReceived = 0;
        }

        mImpl->messageRouter.RouteMessage(msg);
    }
}

void WsClient::OnClose()
{
    {
        std::lock_guard<std::mutex> lock(mImpl->stateMutex);
        mImpl->state = ConnectionState::Disconnected;
    }
    mImpl->stateCV.notify_all();

    Logger::Instance().Info("WsClient", "Server closed connection");
}

void WsClient::OnError(const std::string& pReason)
{
    {
        std::lock_guard<std::mutex> lock(mImpl->stateMutex);
        mImpl->state = ConnectionState::Error;
    }
    mImpl->stateCV.notify_all();

    Logger::Instance().Error("WsClient", "Connection error: " + pReason);
}
