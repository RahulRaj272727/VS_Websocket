#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include "WsClient.hpp"
#include "MessageHandler.hpp"
#include "Logger.hpp"

// Example application message handler for TallyIX
class TallyIXMessageHandler : public IMessageHandler
{
public:
    void OnTextMessage(const Protocol::Message& msg) override
    {
        Logger::Instance().Info("App", "Text message - Type: " + 
            std::to_string(static_cast<int>(msg.type)) + ", MsgID: " + msg.msgId);
    }

    void OnBinaryStart(const Protocol::Message& msg) override
    {
        Logger::Instance().Info("App", 
            "Binary transfer starting - Size: " + std::to_string(msg.binarySize) + " bytes");
        mTotalBytesReceived = 0;
    }

    void OnBinaryChunk(const uint8_t* data, size_t size) override
    {
        mTotalBytesReceived += size;
        Logger::Instance().Debug("App", 
            "Received binary chunk: " + std::to_string(size) + " bytes " +
            "(Total: " + std::to_string(mTotalBytesReceived) + ")");
    }

    void OnBinaryComplete() override
    {
        Logger::Instance().Info("App", 
            "Binary transfer complete - " + std::to_string(mTotalBytesReceived) + " bytes received");
    }

    void OnProtocolError(const std::string& reason) override
    {
        Logger::Instance().Error("App", "Protocol error: " + reason);
    }

private:
    size_t mTotalBytesReceived = 0;
};

int main()
{
    Logger::Instance().Info("Main", "Starting TallyIX WebSocket POC");

    // Create configuration
    Protocol::Config config;
    config.connectionTimeoutMs = 10000;
    config.messageTimeoutMs = 5000;
    config.maxBinaryPayloadSize = 100 * 1024 * 1024;  // 100MB

    // Create client
    WsClient client(config);

    // Set up message handler
    TallyIXMessageHandler appHandler;
    client.SetMessageHandler(&appHandler);

    // Initialize network system
    if (!client.Open())
    {
        Logger::Instance().Error("Main", "Failed to initialize WebSocket client");
        return -1;
    }

    // Connect to server
    if (!client.Connect("ws://127.0.0.1:9001"))
    {
        Logger::Instance().Error("Main", "Failed to initiate connection");
        return -1;
    }

    // Wait for connection with timeout
    if (!client.WaitForConnection(config.connectionTimeoutMs))
    {
        Logger::Instance().Error("Main", "Connection failed or timed out");
        return -1;
    }

    // Connection established - send hello message
    Protocol::Message helloMsg(Protocol::MessageType::Hello, "msg_001", "Hello from TallyIX POC");
    std::string helloJson = Protocol::SerializeJsonMessage(helloMsg);
    
    if (!client.SendText(helloJson))
    {
        Logger::Instance().Error("Main", "Failed to send hello message");
        return -1;
    }

    // Simulate sending binary data with metadata
    Protocol::Message binaryMetaMsg(Protocol::MessageType::BinaryStart, "msg_002");
    binaryMetaMsg.binarySize = 1024 * 1024;  // 1MB
    std::string metaJson = Protocol::SerializeJsonMessage(binaryMetaMsg);
    
    if (!client.SendText(metaJson))
    {
        Logger::Instance().Error("Main", "Failed to send binary metadata");
        return -1;
    }

    // Send 1MB binary payload
    std::vector<uint8_t> binaryData(1024 * 1024, 0xAB);
    if (!client.SendBinary(binaryData.data(), binaryData.size()))
    {
        Logger::Instance().Error("Main", "Failed to send binary data");
        return -1;
    }

    // Wait for echo response
    Logger::Instance().Info("Main", "Waiting for server response...");
    std::this_thread::sleep_for(std::chrono::seconds(3));

    // Clean shutdown
    client.Close();

    Logger::Instance().Info("Main", "POC completed successfully");
    return 0;
}
