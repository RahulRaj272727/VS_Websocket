# TallyIX WebSocket POC

A production-grade C++ WebSocket client proof-of-concept for TallyIX with a focus on **thread safety**, **synchronization**, and **extensibility**.

[![CodeRabbit Review](https://img.shields.io/badge/CodeRabbit-Reviewed-green)](https://coderabbit.ai)

## Overview

This project demonstrates a robust WebSocket client built on top of **IXWebSocket** library, providing a higher-level abstraction suitable for evolving into production-level TallyIX integration.

### Key Features

- ✅ **Thread-Safe Logging**: Mutex-protected stdout access with configurable log levels
- ✅ **State Machine**: Proper connection lifecycle management (Disconnected → Connecting → Connected → Closing)
- ✅ **Synchronization Primitives**: Condition variables for blocking waits with timeout and error detection
- ✅ **Protocol Abstraction**: Typed messages decouple application from JSON wire format
- ✅ **Message Routing**: Observer pattern separates transport from business logic
- ✅ **Exception-Safe**: `std::unique_ptr` for automatic resource cleanup
- ✅ **Automatic DLL Deployment**: Post-build events copy OpenSSL DLLs to output directory
- ✅ **Helper Functions**: `MessageTypeToString()`, `IsValidMessage()`, `GetStateString()` for debugging
- ✅ **Configurable Logging**: `SetMinLevel()` to filter log output for production/debug builds
- ✅ **Connection Retry Logic**: Exponential backoff for resilient connections (NEW)
- ✅ **Binary Transfer Security**: Overflow protection and size validation (NEW)
- ✅ **Thread-Safe Shutdown**: Condition variable-based deterministic cleanup (NEW)
- ✅ **Protocol Error Propagation**: Application notification for protocol violations (NEW)
- ✅ **Configuration Validation**: `Config::IsValid()` for bounds checking (NEW)

## Architecture

### Core Components

```
Logger (Thread-safe singleton)
├── Mutex-protected console access
├── Timestamp and severity levels
└── Used by all components

Protocol (Message abstraction)
├── Typed message enums
├── JSON serialization/parsing
├── Configuration struct
└── No external dependencies

MessageHandler (Observer pattern)
├── IMessageHandler interface
├── MessageRouter for dispatch
└── Separates transport from app logic

WsClient (Production WebSocket client)
├── State machine with mutex + CV
├── IXWebSocket wrapper
├── Thread-safe send methods
├── Binary reassembly tracking
└── Pimpl pattern for encapsulation

main.cpp (Example application)
├── TallyIXMessageHandler implementation
├── Demonstrates proper usage flow
├── Error handling at each step
└── 3-second wait for echo response
```

### Data Flow

```
IXWebSocket thread:        Main thread:
  OnMessage() ──────────>  WaitForConnection()
    ↓                      ↓
  Protocol::Parse()        SendText()
    ↓                      SendBinary()
  MessageRouter            ↓
    ↓                      IXWebSocket::send()
  IMessageHandler::On*()
```

## Building

### Prerequisites

- **Visual Studio 2022** (v143, C++17)
- **IXWebSocket**: Compiled at `D:\ALL_BINS\TallyIXWebSocketPOC\IXWebSocket\build\{Debug|Release}\ixwebsocket.lib`
- **OpenSSL**: `C:\Program Files\OpenSSL-Win64` (for secure WebSocket support)

### Build Steps

1. Open `VS_Websocket.sln` in Visual Studio
2. Select configuration (Debug or Release) and platform (x64)
3. Build → Rebuild Solution
4. Executable: `x64\{Debug|Release}\VS_Websocket.exe`
   - OpenSSL DLLs are automatically copied to output directory via post-build events

### Command-Line Build

```bash
msbuild VS_Websocket.sln /p:Configuration=Debug /p:Platform=x64
```

## Running

### 1. Start Mock WebSocket Server

```bash
pip install websockets
python src/server_mock/echo_server.py
# WebSocket echo server running on ws://127.0.0.1:9001
```

### 2. Run the Client

```bash
x64\Debug\VS_Websocket.exe
```

### Expected Output

```
16:18:37.668 [INF][Main] ====================================================
16:18:37.671 [INF][Main]   TallyIX WebSocket POC - v2.0
16:18:37.672 [INF][Main] ====================================================
16:18:37.673 [INF][Main] Configuration: timeout=10000ms, maxBinarySize=100MB
16:18:37.677 [INF][Main] Network initialization attempt 1/3
16:18:37.679 [INF][WsClient] Network system initialized successfully
16:18:37.680 [INF][Main] Starting connection attempts (max: 5, exponential backoff)
16:18:37.681 [INF][Main] Connection attempt 1/5 to ws://127.0.0.1:9001
16:18:37.702 [INF][WsClient] Connected to server
16:18:37.703 [INF][Main] Connected to server successfully on attempt 1!
16:18:37.705 [INF][Main] Sending Hello message...
16:18:37.709 [INF][App] Received text message - Type: Hello, MsgID: msg_001
16:18:37.713 [INF][App] Binary transfer starting - Expected size: 1048576 bytes
16:18:38.041 [DBG][App] Received binary chunk: 1048576 bytes (Total: 1048576)
16:18:38.042 [INF][App] Binary transfer complete - 1048576 bytes received
16:18:41.034 [INF][Main] Closing connection...
16:18:41.039 [INF][Main] Final state: Disconnected
16:18:41.042 [INF][Main]   TallyIX WebSocket POC - Complete
```

## Code Patterns

### Creating a WsClient

```cpp
Protocol::Config config;
config.connectionTimeoutMs = 10000;
config.maxBinaryPayloadSize = 100 * 1024 * 1024;

// Validate configuration before use (NEW)
if (!config.IsValid()) {
    // Handle invalid configuration
}

WsClient client(config);
MyMessageHandler handler;
client.SetMessageHandler(&handler);

// Configure logging level for production
Logger::Instance().SetMinLevel(Logger::Level::Info);

client.Open();                         // Initialize

// Connection with retry logic (IMPROVED)
const int maxAttempts = 5;
int backoffMs = 500;
bool connected = false;

for (int attempt = 1; attempt <= maxAttempts && !connected; ++attempt) {
    client.Connect("ws://host:port");      // Non-blocking
    if (client.WaitForConnection(10000)) {
        connected = true;
    } else {
        // Exponential backoff: 500ms, 1s, 2s, 4s, 8s
        std::this_thread::sleep_for(std::chrono::milliseconds(backoffMs));
        backoffMs *= 2;
    }
}

// Check connection state
std::cout << "State: " << client.GetStateString() << std::endl;

client.SendText(json);
client.SendBinary(data, size);
client.Close();  // Graceful shutdown with proper synchronization
```

### Implementing IMessageHandler

```cpp
class MyHandler : public IMessageHandler {
public:
    void OnTextMessage(const Protocol::Message& msg) override {
        // Use helper function for better logging
        std::cout << "Type: " << Protocol::MessageTypeToString(msg.type) << std::endl;
        
        // Validate message before processing
        if (!Protocol::IsValidMessage(msg)) {
            // Handle invalid message
            return;
        }
        // Handle hello, ack, error
    }
    
    void OnBinaryStart(const Protocol::Message& msg) override {
        // msg.binarySize tells you expected bytes
    }
    
    void OnBinaryChunk(const uint8_t* data, size_t size) override {
        // Called multiple times as data arrives
    }
    
    void OnBinaryComplete() override {
        // All binary data received
    }
    
    void OnProtocolError(const std::string& reason) override {
        // Handle protocol-level errors
    }
};
```

## Protocol

### Message Format

All messages are JSON with the following structure:

```json
{
  "type": "hello|binary_start|binary_data|ack|error",
  "msg_id": "unique-message-id",
  "content": "optional-content",
  "size": 1048576
}
```

### Message Types

| Type | Direction | Purpose |
|------|-----------|---------|
| `hello` | Bi | Initial handshake |
| `binary_start` | Bi | Signals incoming binary with size metadata |
| `binary_data` | Bi | Raw binary payload (not wrapped in JSON) |
| `ack` | Bi | Acknowledgment of received message |
| `error` | Bi | Error response |

## Thread Safety

| Component | Access Model | Synchronization |
|-----------|--------------|-----------------|
| Logger | Singleton | Mutex (lock_guard) + Min Level Filter |
| WsClient state | Shared (main + IXWs) | Mutex + Condition Variable |
| WsClient binary state | Shared (callback thread) | Dedicated binaryMutex (NEW) |
| WsClient shutdown | Shared (main + callback) | shutdownCV + shutdownComplete flag (NEW) |
| MessageRouter | Called from IXWs | No mutex (callback-only writes) |
| Protocol | Stateless | None needed |
| IMessageHandler | Called from IXWs | App responsible for internal sync |

### Thread Safety Guarantees (NEW)

- **Binary Transfer**: `binaryBytesReceived` and `binaryExpectedSize` are protected by `binaryMutex`
- **Shutdown Synchronization**: `Close()` waits on `shutdownCV` for deterministic cleanup (up to 5s timeout)
- **State Validation**: `WaitForConnection()` validates state before waiting
- **TOCTOU Documented**: `SendText()`/`SendBinary()` intentionally release lock before I/O (documented)

## API Quick Reference

### Protocol Helpers
```cpp
// Convert MessageType to readable string
std::string typeStr = Protocol::MessageTypeToString(msg.type);  // e.g., "Hello"

// Validate a message has required fields
bool valid = Protocol::IsValidMessage(msg);  // type != Unknown && msgId not empty
```

### Logger Configuration
```cpp
// Set minimum log level (Debug < Info < Warning < Error)
Logger::Instance().SetMinLevel(Logger::Level::Warning);  // Only warnings and errors
```

### WsClient State
```cpp
// Get state as enum
WsClient::ConnectionState state = client.GetState();

// Get state as string for logging
std::string stateStr = client.GetStateString();  // e.g., "Connected"
```

## Project Structure

```
VS_Websocket/
├── .github/
│   └── copilot-instructions.md    # AI agent guidance
├── src/
│   ├── main.cpp                   # Example application
│   ├── WsClient.hpp/.cpp          # WebSocket client
│   ├── Logger.hpp/.cpp            # Thread-safe logging
│   ├── Protocol.hpp/.cpp          # Message protocol
│   ├── MessageHandler.hpp/.cpp    # Observer pattern
│   └── server_mock/
│       └── echo_server.py         # Mock server for testing
├── VS_Websocket.sln               # Visual Studio solution
├── VS_Websocket.vcxproj           # Project configuration
├── .gitignore                      # Git exclusions
├── README.md                       # This file
└── TODO.md                         # Future work
```

## Extending the Client

### Adding a New Message Type

1. Add to `Protocol::MessageType` enum
2. Update `Protocol::ParseJsonMessage()` to recognize it
3. Update `Protocol::SerializeJsonMessage()` to emit it
4. Add handler method to `IMessageHandler`
5. Call it from `MessageRouter::RouteMessage()`

### Adding Configuration Options

1. Add field to `Protocol::Config` struct
2. Pass config in `WsClient` constructor
3. Use in appropriate method

## Known Limitations

- Simple JSON parsing (no external library); breaks on special characters
- No automatic reconnection built-in (retry logic example provided in main.cpp)
- Binary fragmentation handled per-message only
- No compression support yet
- Mock server just echoes (no validation)
- Memory usage during binary transfers can be 2-3x payload size due to internal copies

## Security Considerations (NEW)

- **Integer Overflow Protection**: Binary reassembly checks for overflow before accumulating
- **Size Validation**: `BinaryStart` size validated against `maxBinaryPayloadSize`
- **Zero Size Rejection**: `BinaryStart` with size 0 is rejected as invalid
- **State Reset on Disconnect**: Binary transfer state cleared on connection close
- **Configuration Bounds**: `Config::IsValid()` enforces reasonable limits (max 1GB binary)

## Future Work

See [TODO.md](TODO.md) for planned enhancements.

## References

- **IXWebSocket**: https://github.com/machinezone/IXWebSocket
- **OpenSSL**: https://www.openssl.org/
- **WebSocket RFC**: https://tools.ietf.org/html/rfc6455

## License

This is a proof-of-concept for TallyIX. All rights reserved.
