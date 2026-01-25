#include "WsClient.hpp"
#include "Logger.hpp"

#include <iostream>
#include <vector>
#include <thread>

// IXWebSocket library includes - provides low-level WebSocket protocol handling
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>

/**
 * @file WsClient.cpp
 * @brief Implementation of production-grade WebSocket client with thread safety.
 * 
 * This file implements the WsClient class using the Pimpl (Pointer to Implementation)
 * pattern to hide implementation details. The Impl class contains:
 * - The actual ix::WebSocket instance (from IXWebSocket library)
 * - Protocol configuration
 * - State management with mutex and condition variable
 * - Message router for dispatching to application handlers
 * - Binary transfer state tracking
 */

/**
 * @class WsClient::Impl
 * @brief Private implementation class containing all WebSocket details.
 * 
 * Hidden from public API to avoid exposing IXWebSocket library headers.
 * Uses Pimpl pattern for cleaner interface and easier maintenance.
 */
class WsClient::Impl
{
public:
    /// The underlying IXWebSocket instance from IXWebSocket library
    ix::WebSocket ws;
    
    /// Protocol configuration (timeouts, limits, etc.)
    Protocol::Config config;
    
    /// Current connection state (Disconnected, Connecting, Connected, etc.)
    ConnectionState state = ConnectionState::Disconnected;
    
    /// Mutex protecting the connection state from concurrent access
    std::mutex stateMutex;
    
    /// Condition variable for synchronizing state changes (used in WaitForConnection)
    std::condition_variable stateCV;
    
    /// Message router for dispatching parsed messages to application handlers
    MessageRouter messageRouter;
    
    /// Track bytes received for binary transfer reassembly
    size_t binaryBytesReceived = 0;
    
    /// Expected total bytes for current binary transfer (from BinaryStart message)
    size_t binaryExpectedSize = 0;
};

WsClient::WsClient(const Protocol::Config& config)
    : mImpl(std::make_unique<Impl>())
{
    // Store the configuration in the implementation
    mImpl->config = config;
    
    Logger::Instance().Debug("WsClient", 
        "WebSocket client created - timeout=" + 
        std::to_string(config.connectionTimeoutMs) + "ms");
}

WsClient::~WsClient()
{
    // Ensure connection is closed before destroying
    Close();
    
    Logger::Instance().Debug("WsClient", "WebSocket client destroyed");
}

bool WsClient::Open()
{
    // Initialize the network system (must be done once for the entire application)
    if (!ix::initNetSystem())
    {
        Logger::Instance().Error("WsClient", 
            "Failed to initialize network system");
        return false;
    }

    // Disable automatic reconnection - we handle reconnection at application level
    mImpl->ws.disableAutomaticReconnection();

    // Set up the message callback - called from IXWebSocket's internal thread
    // when any message event occurs (open, message, close, error)
    mImpl->ws.setOnMessageCallback(
        [this](const ix::WebSocketMessagePtr& msg)
        {
            // Dispatch the event to appropriate callback based on message type
            switch (msg->type)
            {
            case ix::WebSocketMessageType::Open:
                OnOpen();  // Connection established
                break;

            case ix::WebSocketMessageType::Message:
                // msg->str contains the message (text or binary)
                // msg->binary indicates if it's binary (true) or text (false)
                OnMessage(msg->str, msg->binary);
                break;

            case ix::WebSocketMessageType::Close:
                OnClose();  // Connection closed by server
                break;

            case ix::WebSocketMessageType::Error:
                // msg->errorInfo.reason contains error description
                OnError(msg->errorInfo.reason);
                break;

            default:
                // Unknown message type - shouldn't happen
                break;
            }
        });

    Logger::Instance().Info("WsClient", 
        "Network system initialized successfully");
    return true;
}

bool WsClient::Connect(const std::string& pUrl)
{
    // Lock the state mutex to prevent race conditions
    {
        std::lock_guard<std::mutex> lock(mImpl->stateMutex);
        
        // Check if already connecting or connected
        if (mImpl->state != ConnectionState::Disconnected)
        {
            Logger::Instance().Warning("WsClient", 
                "Cannot connect: already in state " + 
                std::to_string(static_cast<int>(mImpl->state)));
            return false;
        }
        
        // Transition to Connecting state
        mImpl->state = ConnectionState::Connecting;
    }

    // Set the URL and start connection (non-blocking)
    mImpl->ws.setUrl(pUrl);
    mImpl->ws.start();
    
    Logger::Instance().Info("WsClient", 
        "Connection initiated to " + pUrl);
    return true;
}

bool WsClient::WaitForConnection(int timeoutMs)
{
    // Create unique_lock (unlike lock_guard, this works with condition variables)
    std::unique_lock<std::mutex> lock(mImpl->stateMutex);
    
    // Wait until one of these happens:
    // 1. Timeout expires (returns false)
    // 2. Predicate becomes true: state == Connected (returns true)
    // 3. Spurious wakeup (we loop again due to predicate check)
    bool connected = mImpl->stateCV.wait_for(lock, 
        std::chrono::milliseconds(timeoutMs),
        [this]() { return mImpl->state == ConnectionState::Connected; });

    if (connected)
    {
        Logger::Instance().Info("WsClient", 
            "Successfully connected to server");
        return true;
    }

    Logger::Instance().Error("WsClient", 
        "Connection timeout or error after " + 
        std::to_string(timeoutMs) + "ms");
    return false;
}

bool WsClient::SendText(const std::string& pText)
{
    // Check connection state before attempting to send
    {
        std::lock_guard<std::mutex> lock(mImpl->stateMutex);
        if (mImpl->state != ConnectionState::Connected)
        {
            Logger::Instance().Warning("WsClient", 
                "Cannot send text: not connected (state=" + 
                std::to_string(static_cast<int>(mImpl->state)) + ")");
            return false;
        }
    }

    // Queue the message for sending (IXWebSocket handles async transmission)
    mImpl->ws.send(pText);
    
    Logger::Instance().Debug("WsClient", 
        "[SEND][TEXT] " + pText.substr(0, 100) +  // Log first 100 chars
        (pText.length() > 100 ? "..." : ""));
    
    return true;
}

bool WsClient::SendBinary(const void* pData, size_t pSize)
{
    // Validate parameters
    if (!pData || pSize == 0)
    {
        Logger::Instance().Warning("WsClient", 
            "Cannot send binary: invalid data " + 
            (pData ? std::to_string(pSize) : "null pointer"));
        return false;
    }

    // Check connection state before attempting to send
    {
        std::lock_guard<std::mutex> lock(mImpl->stateMutex);
        if (mImpl->state != ConnectionState::Connected)
        {
            Logger::Instance().Warning("WsClient", 
                "Cannot send binary: not connected");
            return false;
        }
    }

    // Validate against maximum payload size
    if (pSize > mImpl->config.maxBinaryPayloadSize)
    {
        Logger::Instance().Error("WsClient", 
            "Binary payload exceeds max size: " + 
            std::to_string(pSize) + " > " + 
            std::to_string(mImpl->config.maxBinaryPayloadSize));
        return false;
    }

    // Convert void* to char* and create string from binary data
    const char* data = reinterpret_cast<const char*>(pData);
    mImpl->ws.sendBinary(std::string(data, pSize));
    
    Logger::Instance().Debug("WsClient", 
        "[SEND][BINARY] " + std::to_string(pSize) + " bytes");
    
    return true;
}

WsClient::ConnectionState WsClient::GetState() const
{
    // Atomic read of current state
    std::lock_guard<std::mutex> lock(mImpl->stateMutex);
    return mImpl->state;
}

void WsClient::SetMessageHandler(IMessageHandler* handler)
{
    // Pass the handler to the message router
    mImpl->messageRouter.SetMessageHandler(handler);
    
    if (handler)
    {
        Logger::Instance().Debug("WsClient", 
            "Message handler set");
    }
}

void WsClient::Close()
{
    // Gracefully close the connection
    {
        std::lock_guard<std::mutex> lock(mImpl->stateMutex);
        
        // Silently succeed if already disconnected
        if (mImpl->state == ConnectionState::Disconnected)
            return;
        
        // Mark as closing
        mImpl->state = ConnectionState::Closing;
    }

    // Stop the WebSocket connection
    mImpl->ws.stop();
    
    // Give the internal thread time to exit gracefully
    // (IXWebSocket manages its own thread internally)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    Logger::Instance().Info("WsClient", 
        "Connection closed");
}

//
// Private callback handlers - invoked from IXWebSocket's internal thread
// All state modifications are protected by mutex
//

void WsClient::OnOpen()
{
    // Connection established - update state and notify waiters
    {
        std::lock_guard<std::mutex> lock(mImpl->stateMutex);
        mImpl->state = ConnectionState::Connected;
    }
    
    // Notify all threads waiting in WaitForConnection()
    mImpl->stateCV.notify_all();

    Logger::Instance().Info("WsClient", 
        "Connected to server");
}

void WsClient::OnMessage(const std::string& pMsg, bool pIsBinary)
{
    if (pIsBinary)
    {
        // Binary data received - reassemble multipart binary transfers
        Logger::Instance().Debug("WsClient", 
            "[RECV][BINARY] " + std::to_string(pMsg.size()) + " bytes");
        
        // Update total bytes received for this transfer
        mImpl->binaryBytesReceived += pMsg.size();
        
        // Route binary chunk to application handler
        mImpl->messageRouter.RouteBinaryData(
            reinterpret_cast<const uint8_t*>(pMsg.data()), 
            pMsg.size());

        // Check if binary transfer is complete
        // (received >= expected size from BinaryStart message)
        if (mImpl->binaryBytesReceived >= mImpl->binaryExpectedSize &&
            mImpl->binaryExpectedSize > 0)
        {
            // Notify handler that transfer is complete
            mImpl->messageRouter.RouteBinaryComplete();
            
            // Reset for next binary transfer
            mImpl->binaryBytesReceived = 0;
            mImpl->binaryExpectedSize = 0;
        }
    }
    else
    {
        // Text message received (usually JSON protocol)
        Logger::Instance().Debug("WsClient", 
            "[RECV][TEXT] " + pMsg.substr(0, 100) + 
            (pMsg.length() > 100 ? "..." : ""));
        
        // Parse JSON to protocol message
        Protocol::Message msg = Protocol::ParseJsonMessage(pMsg);
        
        // Track expected binary size if this is a BinaryStart message
        if (msg.type == Protocol::MessageType::BinaryStart)
        {
            mImpl->binaryExpectedSize = msg.binarySize;
            mImpl->binaryBytesReceived = 0;
            
            Logger::Instance().Debug("WsClient", 
                "Binary transfer starting: " + 
                std::to_string(mImpl->binaryExpectedSize) + " bytes expected");
        }

        // Route parsed message to application handler
        mImpl->messageRouter.RouteMessage(msg);
    }
}

void WsClient::OnClose()
{
    // Connection closed by server - update state and notify waiters
    {
        std::lock_guard<std::mutex> lock(mImpl->stateMutex);
        mImpl->state = ConnectionState::Disconnected;
    }
    
    // Notify all threads waiting in WaitForConnection()
    mImpl->stateCV.notify_all();

    Logger::Instance().Info("WsClient", 
        "Server closed the connection");
}

void WsClient::OnError(const std::string& pReason)
{
    // Error occurred - update state and notify waiters
    {
        std::lock_guard<std::mutex> lock(mImpl->stateMutex);
        mImpl->state = ConnectionState::Error;
    }
    
    // Notify all threads waiting in WaitForConnection()
    mImpl->stateCV.notify_all();

    Logger::Instance().Error("WsClient", 
        "Connection error: " + pReason);
}
