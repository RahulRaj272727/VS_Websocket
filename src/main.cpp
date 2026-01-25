#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include "WsClient.hpp"
#include "MessageHandler.hpp"
#include "Logger.hpp"

/**
 * @file main.cpp
 * @brief Example application demonstrating TallyIX WebSocket client usage.
 * 
 * This is a complete working example that shows the recommended pattern for using
 * the WebSocket client:
 * 
 * 1. Implement IMessageHandler to define how your app handles messages
 * 2. Create and configure a WsClient instance
 * 3. Connect to the server
 * 4. Send and receive protocol messages
 * 5. Clean up and disconnect
 * 
 * To test this example:
 * - Start the mock server: python src/server_mock/echo_server.py
 * - Run this application: x64\Debug\VS_Websocket.exe
 * - Observe: Connection -> Send Hello -> Send Binary Metadata -> Send 1MB Data -> Echo Response
 * 
 * @author Your Name
 * @date 2024
 * 
 * @note All logging output is printed to console with timestamps and severity levels
 * @note The WebSocket client uses an internal thread for message handling
 */

/**
 * @class TallyIXMessageHandler
 * @brief Example implementation of IMessageHandler for the TallyIX application.
 * 
 * This class demonstrates how to implement the message handler interface to:
 * - Track incoming messages
 * - Handle different message types appropriately
 * - Accumulate binary data transfers
 * - Report errors and status
 * 
 * In a real application, this would update UI, save to database, trigger business logic, etc.
 */
class TallyIXMessageHandler : public IMessageHandler
{
public:
    /**
     * @brief Called when a text protocol message is received.
     * 
     * This example logs the message type and ID. In a real application,
     * you would examine the message type and content to decide what to do.
     * 
     * @param msg The received text message
     */
    void OnTextMessage(const Protocol::Message& msg) override
    {
        // Log the message details
        Logger::Instance().Info("App", 
            "Received text message - Type: " + 
            std::to_string(static_cast<int>(msg.type)) + 
            ", MsgID: " + msg.msgId + 
            ", Content: " + msg.content);
        
        // In a real application, switch on message type and handle accordingly
        // Example:
        //   if (msg.type == MessageType::Hello)
        //       HandleHello(msg);
        //   else if (msg.type == MessageType::Acknowledge)
        //       HandleAck(msg);
    }

    /**
     * @brief Called when binary data transfer is about to start.
     * 
     * The size of the incoming transfer is provided so you can pre-allocate
     * buffers or prepare for receiving large amounts of data.
     * 
     * @param msg Message containing binarySize (total bytes to expect)
     */
    void OnBinaryStart(const Protocol::Message& msg) override
    {
        // Log the start of binary transfer
        Logger::Instance().Info("App", 
            "Binary transfer starting - Expected size: " + 
            std::to_string(msg.binarySize) + " bytes");
        
        // Reset accumulated bytes counter
        mTotalBytesReceived = 0;
        
        // In a real application, you might:
        // - Pre-allocate a buffer
        // - Open a file for writing
        // - Prepare a decompression context
        // - Etc.
    }

    /**
     * @brief Called when a chunk of binary data is received.
     * 
     * This may be called multiple times for a single transfer.
     * Accumulate or process the data as it arrives.
     * 
     * @param data Pointer to binary chunk
     * @param size Size of this chunk in bytes
     */
    void OnBinaryChunk(const uint8_t* data, size_t size) override
    {
        // Update total received (for progress reporting)
        mTotalBytesReceived += size;
        
        // Log progress (every chunk)
        Logger::Instance().Debug("App", 
            "Received binary chunk: " + std::to_string(size) + " bytes " +
            "(Total: " + std::to_string(mTotalBytesReceived) + ")");
        
        // In a real application, you would:
        // - Write to file
        // - Append to buffer
        // - Process data (e.g., decompression, decryption)
        // - Update progress UI
        // - Validate checksums
        // - Etc.
        
        // This example just accumulates the count
    }

    /**
     * @brief Called when a complete binary transfer finishes.
     * 
     * All expected data has been received and passed to OnBinaryChunk calls.
     * Now validate or finalize the transfer.
     */
    void OnBinaryComplete() override
    {
        // Log completion
        Logger::Instance().Info("App", 
            "Binary transfer complete - " + 
            std::to_string(mTotalBytesReceived) + " bytes received");
        
        // In a real application, you would:
        // - Close the file
        // - Finalize decompression/decryption
        // - Validate checksum
        // - Update database
        // - Notify user
        // - Etc.
    }

    /**
     * @brief Called when a protocol error occurs.
     * 
     * Report the error and take appropriate action (retry, abort, etc.)
     * 
     * @param reason Human-readable error description
     */
    void OnProtocolError(const std::string& reason) override
    {
        // Log the protocol error with details
        Logger::Instance().Error("App", 
            "Protocol error: " + reason);
        
        // In a real application, you would:
        // - Abort ongoing operations
        // - Request reconnection
        // - Log to error tracking system
        // - Notify user
        // - Implement retry logic
        // - Etc.
    }

private:
    /// Running total of bytes received in current binary transfer
    size_t mTotalBytesReceived = 0;
};

/**
 * @brief Main application entry point.
 * 
 * Demonstrates the complete lifecycle of WebSocket client usage:
 * 1. Configure the client
 * 2. Set up message handler
 * 3. Initialize and connect
 * 4. Exchange protocol messages
 * 5. Gracefully shutdown
 * 
 * @return 0 on success, -1 on error
 */
int main()
{
    // === INITIALIZATION ===
    
    Logger::Instance().Info("Main", 
        "====================================");
    Logger::Instance().Info("Main", 
        "TallyIX WebSocket POC - Starting");
    Logger::Instance().Info("Main", 
        "====================================");

    // Create protocol configuration
    // These settings can be adjusted based on your network conditions and data sizes
    Protocol::Config config;
    config.connectionTimeoutMs = 10000;      // Wait up to 10 seconds for connection
    config.messageTimeoutMs = 5000;          // Wait up to 5 seconds for responses
    config.maxBinaryPayloadSize = 100 * 1024 * 1024;  // Allow 100MB binary messages

    Logger::Instance().Info("Main", 
        "Configuration: timeout=" + 
        std::to_string(config.connectionTimeoutMs) + "ms, " +
        "maxBinarySize=" + 
        std::to_string(config.maxBinaryPayloadSize / (1024 * 1024)) + "MB");

    // Create WebSocket client with configuration
    WsClient client(config);

    // Create and attach the message handler
    // The handler will receive all protocol messages from the server
    TallyIXMessageHandler appHandler;
    client.SetMessageHandler(&appHandler);

    Logger::Instance().Info("Main", "Message handler attached");

    // === NETWORK INITIALIZATION ===

    // Initialize the network system (must be called once per application)
    if (!client.Open())
    {
        Logger::Instance().Error("Main", 
            "FATAL: Failed to initialize WebSocket client");
        return -1;  // Fatal error - cannot continue
    }

    Logger::Instance().Info("Main", 
        "Network system initialized");

    // === CONNECTION ===

    // Initiate connection to the server (non-blocking)
    if (!client.Connect("ws://127.0.0.1:9001"))
    {
        Logger::Instance().Error("Main", 
            "FATAL: Failed to initiate connection to server");
        return -1;  // Fatal error - cannot continue
    }

    Logger::Instance().Info("Main", 
        "Waiting for connection to establish...");

    // Block until connection is established or times out
    if (!client.WaitForConnection(config.connectionTimeoutMs))
    {
        Logger::Instance().Error("Main", 
            "FATAL: Connection failed or timed out after " + 
            std::to_string(config.connectionTimeoutMs) + "ms");
        return -1;  // Fatal error - cannot continue
    }

    Logger::Instance().Info("Main", 
        "Connected to server successfully!");

    // === PROTOCOL COMMUNICATION ===

    // Send a Hello message to initiate communication
    Logger::Instance().Info("Main", 
        "Sending Hello message...");
    
    Protocol::Message helloMsg(
        Protocol::MessageType::Hello, 
        "msg_001",                           // Unique message ID
        "Hello from TallyIX POC");           // Message content
    std::string helloJson = Protocol::SerializeJsonMessage(helloMsg);
    
    if (!client.SendText(helloJson))
    {
        Logger::Instance().Error("Main", 
            "ERROR: Failed to send hello message");
        client.Close();
        return -1;
    }

    Logger::Instance().Info("Main", 
        "Hello message sent: " + helloJson);

    // === BINARY TRANSFER EXAMPLE ===

    // Send metadata about upcoming binary data
    Logger::Instance().Info("Main", 
        "Preparing to send 1MB binary data...");
    
    Protocol::Message binaryMetaMsg(
        Protocol::MessageType::BinaryStart,
        "msg_002");                          // Unique message ID
    binaryMetaMsg.binarySize = 1024 * 1024; // 1MB of binary data coming
    std::string metaJson = Protocol::SerializeJsonMessage(binaryMetaMsg);
    
    if (!client.SendText(metaJson))
    {
        Logger::Instance().Error("Main", 
            "ERROR: Failed to send binary metadata");
        client.Close();
        return -1;
    }

    Logger::Instance().Info("Main", 
        "Binary metadata sent - 1MB transfer announced");

    // Create 1MB of binary payload
    // In a real application, this would be actual data (images, files, etc.)
    // Using 0xAB (171) as a test pattern
    std::vector<uint8_t> binaryData(1024 * 1024, 0xAB);

    Logger::Instance().Info("Main", 
        "Sending 1MB binary payload...");
    
    if (!client.SendBinary(binaryData.data(), binaryData.size()))
    {
        Logger::Instance().Error("Main", 
            "ERROR: Failed to send binary data");
        client.Close();
        return -1;
    }

    Logger::Instance().Info("Main", 
        "Binary payload sent: " + 
        std::to_string(binaryData.size()) + " bytes");

    // === WAIT FOR SERVER RESPONSE ===

    Logger::Instance().Info("Main", 
        "Waiting for server response (3 seconds)...");
    
    // Wait for the server to echo back the messages
    // The handler will log all received messages
    std::this_thread::sleep_for(std::chrono::seconds(3));

    Logger::Instance().Info("Main", 
        "Wait period complete");

    // === CLEANUP ===

    Logger::Instance().Info("Main", 
        "Closing connection...");
    
    // Gracefully close the connection
    client.Close();

    Logger::Instance().Info("Main", 
        "====================================");
    Logger::Instance().Info("Main", 
        "TallyIX WebSocket POC - Complete");
    Logger::Instance().Info("Main", 
        "====================================");

    return 0;  // Success!
}
