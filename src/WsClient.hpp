#pragma once

#include <string>
#include <memory>
#include <mutex>
#include <condition_variable>
#include "Protocol.hpp"
#include "MessageHandler.hpp"

// Production-grade WebSocket client with proper synchronization
class WsClient
{
public:
    enum class ConnectionState
    {
        Disconnected,
        Connecting,
        Connected,
        Closing,
        Error
    };

    WsClient(const Protocol::Config& config = Protocol::Config());
    ~WsClient();

    // Non-copyable, non-movable
    WsClient(const WsClient&) = delete;
    WsClient& operator=(const WsClient&) = delete;

    // Initialize network system (must call once before any connections)
    bool Open();

    // Connect to WebSocket server (async, use WaitForConnection to sync)
    bool Connect(const std::string& pUrl);

    // Wait for connection to be established (blocks with timeout)
    // Returns true if connected, false if timeout/error
    bool WaitForConnection(int timeoutMs);

    // Send text message (thread-safe)
    bool SendText(const std::string& pText);

    // Send binary message (thread-safe)
    bool SendBinary(const void* pData, size_t pSize);

    // Get current connection state
    ConnectionState GetState() const;

    // Set message handler for routing parsed messages
    void SetMessageHandler(IMessageHandler* handler);

    // Close connection gracefully (waits for background thread)
    void Close();

private:
    class Impl;
    std::unique_ptr<Impl> mImpl;

    // Private callback handlers (called from IXWebSocket thread)
    void OnOpen();
    void OnMessage(const std::string& pMsg, bool pIsBinary);
    void OnClose();
    void OnError(const std::string& pReason);

    friend class Impl;  // Allow Impl to call private methods
};
