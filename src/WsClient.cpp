// CodeRabbit: Please review this file thoroughly for production readiness
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
    
    /// Mutex protecting binary transfer state variables
    std::mutex binaryMutex;
    
    /// Condition variable for synchronizing state changes (used in WaitForConnection)
    std::condition_variable stateCV;
    
    /// Condition variable for synchronizing shutdown completion
    std::condition_variable shutdownCV;
    
    /// Flag indicating that the WebSocket internal thread has completed shutdown
    bool shutdownComplete = false;
    
    /// Message router for dispatching parsed messages to application handlers
    MessageRouter messageRouter;
    
    /// Track bytes received for binary transfer reassembly (protected by binaryMutex)
    size_t binaryBytesReceived = 0;
    
    /// Expected total bytes for current binary transfer (protected by binaryMutex)
    size_t binaryExpectedSize = 0;
    
    /// Reset binary transfer state (call when connection closes or transfer completes)
    void ResetBinaryState()
    {
        std::lock_guard<std::mutex> lock(binaryMutex);
        binaryBytesReceived = 0;
        binaryExpectedSize = 0;
    }
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

    // Configure ping/pong heartbeat if enabled (keeps connection alive through load balancers)
    if (mImpl->config.pingIntervalSeconds > 0)
    {
        mImpl->ws.setPingInterval(mImpl->config.pingIntervalSeconds);
        Logger::Instance().Debug("WsClient", 
            "Heartbeat enabled: " + std::to_string(mImpl->config.pingIntervalSeconds) + " seconds");
    }

    // Configure per-message deflate compression if enabled
    if (mImpl->config.enableCompression)
    {
        mImpl->ws.enablePerMessageDeflate();
        Logger::Instance().Debug("WsClient", 
            "Per-message deflate compression enabled");
    }
    else
    {
        mImpl->ws.disablePerMessageDeflate();
    }

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

            case ix::WebSocketMessageType::Ping:
                // Ping received - pong is automatically sent by IXWebSocket
                Logger::Instance().Debug("WsClient", 
                    "[RECV][PING] " + (msg->str.empty() ? "(empty)" : msg->str));
                mImpl->messageRouter.RoutePing(msg->str);
                break;

            case ix::WebSocketMessageType::Pong:
                // Pong received (response to our ping)
                Logger::Instance().Debug("WsClient", 
                    "[RECV][PONG] " + (msg->str.empty() ? "(empty)" : msg->str));
                mImpl->messageRouter.RoutePong(msg->str);
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
    
    // Early return if already connected or in invalid state for waiting
    if (mImpl->state == ConnectionState::Connected)
    {
        Logger::Instance().Debug("WsClient", 
            "WaitForConnection: Already connected");
        return true;
    }
    
    if (mImpl->state != ConnectionState::Connecting)
    {
        Logger::Instance().Warning("WsClient", 
            "WaitForConnection: Invalid state - expected Connecting, got " + 
            std::to_string(static_cast<int>(mImpl->state)));
        return false;
    }
    
    // Wait until one of these happens:
    // 1. Timeout expires (returns false)
    // 2. State becomes Connected (returns true)
    // 3. State becomes Error (returns false - don't keep waiting)
    // 4. Spurious wakeup (we loop again due to predicate check)
    bool stateChanged = mImpl->stateCV.wait_for(lock, 
        std::chrono::milliseconds(timeoutMs),
        [this]() { 
            return mImpl->state == ConnectionState::Connected ||
                   mImpl->state == ConnectionState::Error ||
                   mImpl->state == ConnectionState::Disconnected;
        });

    // Check final state after waiting
    if (mImpl->state == ConnectionState::Connected)
    {
        Logger::Instance().Info("WsClient", 
            "Successfully connected to server");
        return true;
    }
    
    // Determine reason for failure
    if (mImpl->state == ConnectionState::Error)
    {
        Logger::Instance().Error("WsClient", 
            "Connection failed with error");
    }
    else if (!stateChanged)
    {
        Logger::Instance().Error("WsClient", 
            "Connection timeout after " + 
            std::to_string(timeoutMs) + "ms");
    }
    else
    {
        Logger::Instance().Error("WsClient", 
            "Connection failed - unexpected state: " + 
            std::to_string(static_cast<int>(mImpl->state)));
    }
    
    return false;
}

bool WsClient::SendText(const std::string& pText)
{
    // Check connection state before attempting to send
    // NOTE: There is an intentional TOCTOU (time-of-check-time-of-use) gap here.
    // The connection state could change between this check and the actual send below.
    // This is acceptable because:
    // 1. IXWebSocket handles sends on closed connections gracefully (returns error)
    // 2. Holding the lock during I/O would risk deadlock with callbacks
    // 3. The state check is a fast-path optimization, not a guarantee
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
    // NOTE: Intentional TOCTOU gap - see SendText() for detailed explanation
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

bool WsClient::SendPing(const std::string& payload)
{
    // Check connection state before attempting to send
    {
        std::lock_guard<std::mutex> lock(mImpl->stateMutex);
        if (mImpl->state != ConnectionState::Connected)
        {
            Logger::Instance().Warning("WsClient", 
                "Cannot send ping: not connected");
            return false;
        }
    }

    // Send the ping frame (payload limited to 125 bytes per RFC 6455)
    std::string trimmedPayload = payload.substr(0, 125);
    mImpl->ws.ping(trimmedPayload);
    
    Logger::Instance().Debug("WsClient", 
        "[SEND][PING] " + (trimmedPayload.empty() ? "(empty)" : trimmedPayload));
    
    return true;
}

void WsClient::EnableCompression()
{
    mImpl->ws.enablePerMessageDeflate();
    Logger::Instance().Info("WsClient", 
        "Per-message deflate compression enabled");
}

void WsClient::DisableCompression()
{
    mImpl->ws.disablePerMessageDeflate();
    Logger::Instance().Info("WsClient", 
        "Per-message deflate compression disabled");
}

WsClient::ConnectionState WsClient::GetState() const
{
    // Atomic read of current state
    std::lock_guard<std::mutex> lock(mImpl->stateMutex);
    return mImpl->state;
}

std::string WsClient::GetStateString() const
{
    ConnectionState state = GetState();
    switch (state)
    {
    case ConnectionState::Disconnected: return "Disconnected";
    case ConnectionState::Connecting:   return "Connecting";
    case ConnectionState::Connected:    return "Connected";
    case ConnectionState::Closing:      return "Closing";
    case ConnectionState::Error:        return "Error";
    default:                            return "Unknown";
    }
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
        
        // Mark as closing and reset shutdown completion flag
        mImpl->state = ConnectionState::Closing;
        mImpl->shutdownComplete = false;
    }

    // Stop the WebSocket connection
    mImpl->ws.stop();
    
    // Wait for the internal thread to exit gracefully with proper synchronization
    // IXWebSocket will trigger OnClose callback when the thread has completed shutdown
    {
        std::unique_lock<std::mutex> lock(mImpl->stateMutex);
        
        // Wait up to 5 seconds for shutdown completion
        bool completed = mImpl->shutdownCV.wait_for(
            lock, 
            std::chrono::milliseconds(5000),
            [this] { return mImpl->shutdownComplete; }
        );
        
        if (!completed)
        {
            Logger::Instance().Warning("WsClient", 
                "Shutdown timeout - internal thread may still be running");
        }
    }

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
        
        // Thread-safe update of binary transfer state
        bool transferComplete = false;
        {
            std::lock_guard<std::mutex> lock(mImpl->binaryMutex);
            
            // Check for integer overflow before adding
            if (pMsg.size() > SIZE_MAX - mImpl->binaryBytesReceived)
            {
                Logger::Instance().Error("WsClient", 
                    "Binary transfer overflow detected - resetting");
                mImpl->binaryBytesReceived = 0;
                mImpl->binaryExpectedSize = 0;
                mImpl->messageRouter.RouteProtocolError(
                    "Binary transfer size overflow - possible attack or corruption");
                return;
            }
            
            // Update total bytes received for this transfer
            mImpl->binaryBytesReceived += pMsg.size();
            
            // Check if binary transfer is complete
            // (received >= expected size from BinaryStart message)
            if (mImpl->binaryBytesReceived >= mImpl->binaryExpectedSize &&
                mImpl->binaryExpectedSize > 0)
            {
                transferComplete = true;
            }
        }
        
        // Route binary chunk to application handler (outside lock to avoid deadlock)
        mImpl->messageRouter.RouteBinaryData(
            reinterpret_cast<const uint8_t*>(pMsg.data()), 
            pMsg.size());

        if (transferComplete)
        {
            // Notify handler that transfer is complete
            mImpl->messageRouter.RouteBinaryComplete();
            
            // Reset for next binary transfer (thread-safe)
            mImpl->ResetBinaryState();
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
            std::lock_guard<std::mutex> lock(mImpl->binaryMutex);
            
            // Validate against maximum payload size (security check)
            if (msg.binarySize > mImpl->config.maxBinaryPayloadSize)
            {
                Logger::Instance().Error("WsClient", 
                    "BinaryStart size exceeds max: " + 
                    std::to_string(msg.binarySize) + " > " + 
                    std::to_string(mImpl->config.maxBinaryPayloadSize));
                mImpl->messageRouter.RouteProtocolError(
                    "Binary payload size exceeds maximum allowed: " + 
                    std::to_string(msg.binarySize));
                return;
            }
            
            // Validate non-zero size
            if (msg.binarySize == 0)
            {
                Logger::Instance().Warning("WsClient", 
                    "BinaryStart with zero size - ignoring");
                mImpl->messageRouter.RouteProtocolError(
                    "BinaryStart message with zero size is invalid");
                return;
            }
            
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
        mImpl->shutdownComplete = true;  // Signal that shutdown is complete
    }
    
    // Reset binary transfer state to prevent stale values on reconnect
    mImpl->ResetBinaryState();
    
    // Notify all threads waiting in WaitForConnection()
    mImpl->stateCV.notify_all();
    
    // Notify any threads waiting in Close() for shutdown to complete
    mImpl->shutdownCV.notify_all();

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
