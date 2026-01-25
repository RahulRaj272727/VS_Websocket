#pragma once

#include <string>
#include <memory>
#include <mutex>
#include <condition_variable>
#include "Protocol.hpp"
#include "MessageHandler.hpp"

/**
 * @class WsClient
 * @brief Production-grade WebSocket client with thread-safe synchronization.
 * 
 * This class provides a high-level WebSocket client interface for TallyIX communication.
 * It manages connection state, sends/receives messages, and routes protocol messages to
 * application handlers. The client is designed to be:
 * 
 * - **Thread-Safe**: Multiple threads can call Send* methods safely without coordination
 * - **Async-Friendly**: Connection is non-blocking; use WaitForConnection() for sync
 * - **State-Managed**: Internal state machine prevents invalid operations (e.g., sending before connected)
 * - **Exception-Safe**: Uses smart pointers; proper cleanup on error or destruction
 * 
 * Architecture:
 * - Uses Pimpl (Pointer to Implementation) pattern for clean separation
 * - IXWebSocket library handles low-level WebSocket protocol
 * - State changes guarded by mutex + condition variable
 * - Callbacks dispatched to IMessageHandler via MessageRouter
 * 
 * Usage Pattern:
 * @code
 *   WsClient client(config);
 *   client.SetMessageHandler(&myHandler);
 *   client.Open();                          // Initialize system
 *   client.Connect("ws://server.com");      // Start connection (non-blocking)
 *   client.WaitForConnection(5000);         // Wait up to 5 seconds
 *   client.SendText(json_message);          // Send when connected
 *   client.Close();                         // Graceful shutdown
 * @endcode
 * 
 * Thread Safety:
 * - Constructor/Destructor: Main thread only
 * - Open/Connect/Close: Main thread (called before/after using Send*)
 * - SendText/SendBinary: Any thread (thread-safe)
 * - GetState: Any thread (atomic read)
 * - Callbacks (OnOpen, OnMessage, etc.): WebSocket internal thread
 * 
 * @note The WebSocket client runs an internal thread managed by IXWebSocket.
 *       All callbacks (OnOpen, OnMessage, OnClose, OnError) are called from
 *       this thread, not your main thread.
 * 
 * @see IMessageHandler for how to handle incoming messages
 * @see Protocol for message structure and configuration
 */
class WsClient
{
public:
    /**
     * @enum ConnectionState
     * @brief Enumeration of possible WebSocket connection states.
     * 
     * These states represent the lifecycle of a WebSocket connection:
     */
    enum class ConnectionState
    {
        Disconnected,  ///< Not connected or connection was closed
        Connecting,    ///< Connection in progress (after Connect(), before OnOpen())
        Connected,     ///< Connection established and ready for communication
        Closing,       ///< Graceful shutdown in progress
        Error          ///< Error state (connection failed or error received)
    };

    /**
     * @brief Construct a WebSocket client with protocol configuration.
     * 
     * @param config Protocol configuration including timeouts and size limits.
     *               Defaults to Protocol::Config() if not provided.
     * 
     * @note The client is initially in Disconnected state and requires Open() before use.
     * 
     * @example
     *   Protocol::Config config;
     *   config.connectionTimeoutMs = 30000;  // 30 second timeout
     *   WsClient client(config);
     */
    WsClient(const Protocol::Config& config = Protocol::Config());

    /**
     * @brief Destructor - closes connection and cleans up resources.
     * 
     * @note If the connection is still open, it will be closed gracefully.
     *       The destructor blocks briefly to allow the background thread to exit.
     */
    ~WsClient();

    // Prevent copying and moving (singleton-like usage per connection)
    /// @brief Copy constructor - deleted (not copyable)
    WsClient(const WsClient&) = delete;
    
    /// @brief Assignment operator - deleted (not assignable)
    WsClient& operator=(const WsClient&) = delete;

    /**
     * @brief Initialize the WebSocket network system (must be called once).
     * 
     * This function initializes the underlying IXWebSocket library, which sets up
     * the network stack. Must be called before any connection attempts. Safe to call
     * multiple times, but subsequent calls have no effect.
     * 
     * @return true if initialization succeeded, false on error
     * 
     * @note Must be called from the main thread before calling Connect()
     * 
     * @example
     *   if (!client.Open()) {
     *       Logger::Instance().Error("App", "Failed to open network system");
     *       return;
     *   }
     */
    bool Open();

    /**
     * @brief Initiate connection to WebSocket server (non-blocking).
     * 
     * This starts a connection attempt to the given URL. The function returns
     * immediately; use WaitForConnection() to block until the connection is
     * established or times out.
     * 
     * @param pUrl WebSocket URL (e.g., "ws://localhost:9001" or "wss://secure.host")
     * @return true if connection was initiated, false if already connecting/connected
     * 
     * @note This is non-blocking. Call WaitForConnection() to wait for actual connection.
     * @note Returns false if not in Disconnected state (to prevent multiple connections)
     * 
     * @example
     *   client.Connect("ws://127.0.0.1:9001");
     *   bool connected = client.WaitForConnection(10000);  // Wait up to 10 seconds
     * 
     * @see WaitForConnection
     */
    bool Connect(const std::string& pUrl);

    /**
     * @brief Block until connection is established (with timeout).
     * 
     * This function blocks the calling thread until one of three things happens:
     * 1. Connection succeeds (returns true)
     * 2. Connection fails (returns false)
     * 3. Timeout expires (returns false)
     * 
     * Uses a condition variable for efficient waiting (not busy-polling).
     * 
     * @param timeoutMs Timeout in milliseconds (how long to wait)
     * @return true if connected successfully, false if timeout or error
     * 
     * @note This blocks the calling thread; call from non-critical paths
     * @note Must be called after Connect(); otherwise waits for the timeout and returns false
     * 
     * @example
     *   client.Connect("ws://server");
     *   if (!client.WaitForConnection(5000)) {
     *       Logger::Instance().Error("App", "Connection timeout");
     *       return;
     *   }
     *   // Connection now established
     * 
     * @see Connect
     */
    bool WaitForConnection(int timeoutMs);

    /**
     * @brief Send a text message (thread-safe).
     * 
     * Sends a text message to the connected server. This is the typical way to send
     * protocol messages (which are JSON format). Thread-safe - can be called from
     * any thread without synchronization.
     * 
     * @param pText The message text to send (e.g., JSON string)
     * @return true if message was queued for sending, false if not connected or error
     * 
     * @note Returns false silently if not connected; no exception is thrown
     * @note Message is queued for sending; actual transmission is asynchronous
     * 
     * @example
     *   Protocol::Message msg(MessageType::Hello, "123", "Hello Server");
     *   std::string json = Protocol::SerializeJsonMessage(msg);
     *   if (!client.SendText(json)) {
     *       Logger::Instance().Error("App", "Failed to send hello message");
     *   }
     * 
     * @see SendBinary
     */
    bool SendText(const std::string& pText);

    /**
     * @brief Send a binary message (thread-safe).
     * 
     * Sends binary data to the connected server. Thread-safe - can be called from
     * any thread without synchronization. The data is typically preceded by a
     * BinaryStart text message containing size metadata.
     * 
     * @param pData Pointer to binary data to send
     * @param pSize Size of data in bytes
     * @return true if message was queued for sending, false if not connected or too large
     * 
     * @note Returns false if data exceeds maxBinaryPayloadSize from config
     * @note Data must remain valid until this function returns; it's copied internally
     * @note Message is queued for sending; actual transmission is asynchronous
     * 
     * @example
     *   // First send metadata
     *   Protocol::Message meta(MessageType::BinaryStart, "456");
     *   meta.binarySize = 1024;
     *   client.SendText(Protocol::SerializeJsonMessage(meta));
     *   
     *   // Then send binary data
     *   uint8_t data[1024];
     *   if (!client.SendBinary(data, 1024)) {
     *       Logger::Instance().Error("App", "Failed to send binary");
     *   }
     * 
     * @see SendText
     */
    bool SendBinary(const void* pData, size_t pSize);

    /**
     * @brief Get the current connection state.
     * 
     * Returns the current state of the connection. This is an atomic read and can
     * be safely called from any thread.
     * 
     * @return Current ConnectionState enum value
     * 
     * @note This is informational only; checking state before Send* may not be
     *       reliable due to race conditions. Send* methods will safely fail if
     *       not connected.
     * 
     * @example
     *   if (client.GetState() == WsClient::ConnectionState::Connected) {
     *       // We might be connected, but still check SendText return value
     *   }
     */
    ConnectionState GetState() const;

    /**
     * @brief Get the current connection state as a human-readable string.
     * 
     * Useful for debugging and logging purposes.
     * 
     * @return String representation of current state (e.g., "Connected", "Disconnected")
     */
    std::string GetStateString() const;

    /**
     * @brief Set the handler for receiving parsed protocol messages.
     * 
     * Registers an IMessageHandler implementation that will receive callbacks
     * when protocol messages arrive. The handler is called from the WebSocket
     * thread, not your main thread.
     * 
     * @param handler Pointer to IMessageHandler implementation, or nullptr to disable routing
     * 
     * @note Should be called before connecting to avoid missing initial messages
     * @note The handler must outlive the client (or call again with nullptr before handler is destroyed)
     * @note All handler callbacks come from the WebSocket thread
     * 
     * @example
     *   class MyHandler : public IMessageHandler {
     *       // ... implement pure virtual methods
     *   };
     *   MyHandler handler;
     *   client.SetMessageHandler(&handler);
     *   client.Connect("ws://...");
     * 
     * @see IMessageHandler
     */
    void SetMessageHandler(IMessageHandler* handler);

    /**
     * @brief Close the connection gracefully.
     * 
     * Gracefully closes the WebSocket connection and waits briefly for the
     * background thread to exit. After Close(), the client cannot be reused;
     * create a new WsClient to reconnect.
     * 
     * @note Blocks briefly (up to 100ms) for thread cleanup
     * @note Safe to call multiple times (subsequent calls do nothing)
     * @note Called automatically by destructor
     * 
     * @example
     *   client.Connect("ws://server");
     *   // ... use client ...
     *   client.Close();  // Graceful shutdown
     */
    void Close();

private:
    /**
     * @class Impl
     * @brief Private implementation class (Pimpl pattern).
     * 
     * Contains all internal state for the WebSocket client. Hidden here to avoid
     * exposing IXWebSocket headers in the public interface.
     */
    class Impl;
    
    /// @brief Unique pointer to implementation (RAII - automatic cleanup)
    std::unique_ptr<Impl> mImpl;

    // Private callback handlers (invoked from IXWebSocket internal thread)
    
    /// @brief Callback when connection is established
    void OnOpen();
    
    /// @brief Callback when message is received
    void OnMessage(const std::string& pMsg, bool pIsBinary);
    
    /// @brief Callback when connection is closed by server
    void OnClose();
    
    /// @brief Callback when connection error occurs
    void OnError(const std::string& pReason);

    /// @brief Allow Impl to call private callback methods
    friend class Impl;
};
