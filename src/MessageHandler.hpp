// CodeRabbit: Please review this file thoroughly for production readiness
#pragma once

#include <string>
#include <functional>
#include "Protocol.hpp"

/**
 * @class IMessageHandler
 * @brief Abstract interface for application-level message handling.
 * 
 * This is the primary way your application interacts with incoming protocol messages.
 * Instead of dealing with raw WebSocket callbacks, you implement this interface to
 * define how your application responds to specific message types.
 * 
 * The WebSocket client calls these methods from its internal thread, so if you need
 * to update UI or access shared state, you may need to post the work to your main
 * application thread (thread-safety is your responsibility).
 * 
 * @note All callback methods are invoked from the WebSocket thread, not the main thread.
 * 
 * @example
 *   class MyHandler : public IMessageHandler {
 *       void OnTextMessage(const Protocol::Message& msg) override {
 *           // Handle hello/ack messages
 *       }
 *       void OnBinaryStart(const Protocol::Message& msg) override {
 *           // Prepare to receive binary data
 *           reserve_buffer(msg.binarySize);
 *       }
 *       // ... implement other methods
 *   };
 */
class IMessageHandler
{
public:
    /// @brief Virtual destructor - required for proper cleanup of derived classes
    virtual ~IMessageHandler() = default;

    /**
     * @brief Called when a text protocol message is received.
     * 
     * This handles hello messages, acknowledgments, and other text-based protocol
     * messages. The specific message type is indicated in msg.type.
     * 
     * @param msg The parsed protocol message containing type, ID, and content
     * 
     * @note This is called from the WebSocket thread, not your main application thread.
     * @note Binary data transfers start with a BinaryStart text message, then OnBinaryStart
     *       is called separately.
     */
    virtual void OnTextMessage(const Protocol::Message& msg) = 0;

    /**
     * @brief Called when a binary data transfer begins.
     * 
     * This message signals the start of a binary data transfer. The expected size
     * is provided in msg.binarySize, allowing you to pre-allocate buffers or resources.
     * 
     * After this call, one or more OnBinaryChunk calls will follow with the actual data,
     * and finally OnBinaryComplete will be called when all data is received.
     * 
     * @param msg The BinaryStart message containing size information
     * 
     * @note The binary size is provided as a hint; always be prepared for a call to
     *       OnBinaryChunk with less or more data than expected (error tolerance).
     */
    virtual void OnBinaryStart(const Protocol::Message& msg) = 0;

    /**
     * @brief Called when a chunk of binary data is received.
     * 
     * Binary transfers may arrive in multiple chunks. This method is called for each
     * chunk. You should accumulate or process the data as it arrives.
     * 
     * Multiple chunks may arrive before OnBinaryComplete is called.
     * For example: OnBinaryStart -> OnBinaryChunk(64KB) -> OnBinaryChunk(64KB) 
     *             -> OnBinaryChunk(remaining) -> OnBinaryComplete
     * 
     * @param data Pointer to the binary data chunk
     * @param size Size of this chunk in bytes
     * 
     * @note Do not modify the data buffer; it may be freed immediately after this returns.
     * @note Copy data if you need to store it beyond this function call.
     */
    virtual void OnBinaryChunk(const uint8_t* data, size_t size) = 0;

    /**
     * @brief Called when a complete binary transfer is finished.
     * 
     * All expected data for the current binary transfer has been received and passed
     * to OnBinaryChunk calls. At this point, you can finalize processing, validate
     * checksums, etc.
     * 
     * After this call, new text or binary messages may arrive.
     */
    virtual void OnBinaryComplete() = 0;

    /**
     * @brief Called when a protocol error occurs.
     * 
     * The server or protocol layer detected an error condition. The reason string
     * provides details about what went wrong.
     * 
     * Common error reasons:
     * - "Invalid message format" - JSON parsing failed
     * - "Unsupported message type" - Unknown message type received
     * - "Binary size exceeded" - Binary transfer exceeds max size limit
     * 
     * @param reason Human-readable error description
     */
    virtual void OnProtocolError(const std::string& reason) = 0;

    /**
     * @brief Called when a ping frame is received from the server.
     * 
     * Note: The WebSocket library automatically responds with a pong frame,
     * so you don't need to do anything. This is just for informational purposes.
     * 
     * @param payload The ping payload data (may be empty)
     * 
     * @note Default implementation does nothing - override if you need ping notifications
     */
    virtual void OnPing(const std::string& payload) { (void)payload; }

    /**
     * @brief Called when a pong frame is received from the server.
     * 
     * This is typically in response to a ping sent by SendPing() or automatic heartbeat.
     * You can use this to measure round-trip latency.
     * 
     * @param payload The pong payload data (echoes the ping payload)
     * 
     * @note Default implementation does nothing - override if you need pong notifications
     */
    virtual void OnPong(const std::string& payload) { (void)payload; }
};

/**
 * @class MessageRouter
 * @brief Routes parsed protocol messages to the application message handler.
 * 
 * This class acts as a bridge between the WebSocket client's low-level message
 * handling and your application's business logic (IMessageHandler). It takes parsed
 * Protocol::Message objects and routes them to the appropriate handler method based
 * on message type.
 * 
 * Thread Safety:
 * - SetMessageHandler() should only be called before connecting
 * - All Route* methods are called from the WebSocket thread
 * - Your IMessageHandler implementation must be thread-safe if accessed from other threads
 * 
 * @example
 *   MessageRouter router;
 *   MyHandler myHandler;
 *   router.SetMessageHandler(&myHandler);
 *   
 *   // Router will now dispatch messages to myHandler
 *   Protocol::Message msg = ...;
 *   router.RouteMessage(msg);
 */
class MessageRouter
{
public:
    /// @brief Construct an empty message router with no handler
    MessageRouter();

    /**
     * @brief Set the handler to receive routed messages.
     * 
     * @param handler Pointer to IMessageHandler implementation, or nullptr to disable routing
     * 
     * @note Should be set before connecting to avoid missing messages
     * @note Changing handlers while messages are being routed may cause messages to be ignored
     */
    void SetMessageHandler(IMessageHandler* handler);

    /**
     * @brief Route a parsed protocol message to the handler.
     * 
     * This examines the message type and calls the appropriate handler method.
     * 
     * @param msg The message to route (type determines which handler method is called)
     * 
     * @example
     *   Protocol::Message msg(MessageType::Hello, "id1");
     *   router.RouteMessage(msg);  // Calls handler->OnTextMessage(msg)
     */
    void RouteMessage(const Protocol::Message& msg);

    /**
     * @brief Route a binary data chunk to the handler.
     * 
     * Passes binary data to the handler's OnBinaryChunk method. Call RouteMessage
     * with a BinaryStart message first, then this one or more times, then RouteBinaryComplete.
     * 
     * @param data Pointer to binary data
     * @param size Size of the data in bytes
     * 
     * @see RouteMessage, RouteBinaryComplete
     */
    void RouteBinaryData(const uint8_t* data, size_t size);

    /**
     * @brief Signal that binary transfer is complete.
     * 
     * Call this after the final OnBinaryChunk to signal completion. The handler's
     * OnBinaryComplete method will be called.
     * 
     * @see RouteMessage, RouteBinaryData
     */
    void RouteBinaryComplete();
    
    /**
     * @brief Route a protocol error directly to the handler.
     * 
     * Used by WsClient to report protocol-level errors (overflow, invalid sizes, etc.)
     * to the application handler.
     * 
     * @param errorMsg Human-readable error description
     */
    void RouteProtocolError(const std::string& errorMsg);

    /**
     * @brief Route a ping notification to the handler.
     * 
     * Called when a ping frame is received from the server.
     * 
     * @param payload The ping payload data
     */
    void RoutePing(const std::string& payload);

    /**
     * @brief Route a pong notification to the handler.
     * 
     * Called when a pong frame is received from the server.
     * 
     * @param payload The pong payload data
     */
    void RoutePong(const std::string& payload);

private:
    /// @brief Pointer to the application's message handler (may be null)
    /// @note Access is NOT synchronized - caller must ensure SetMessageHandler
    ///       is only called before message routing begins (during setup phase)
    IMessageHandler* mHandler = nullptr;
};
