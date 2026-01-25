# Copilot Instructions - TallyIX WebSocket POC

## Project Overview

This is a production-grade C++ WebSocket client for TallyIX with a focus on **thread safety**, **synchronization**, and **extensibility**. The project uses **IXWebSocket** library for low-level WebSocket protocol handling while providing a higher-level abstraction for application code.

## Architecture

### Core Components

1. **Logger** (`src/Logger.hpp/.cpp`): Thread-safe singleton logging system
   - Mutex-protected `std::cout` access to prevent interleaved output
   - Timestamp and severity levels (Debug, Info, Warning, Error)
   - Used by all components for diagnostics

2. **Protocol** (`src/Protocol.hpp/.cpp`): Protocol definition and parsing
   - Enumerates message types: Hello, BinaryStart, BinaryData, Acknowledge, Error
   - `Config` struct for configurable limits (connection timeout, max binary size)
   - JSON parsing/serialization without external dependencies
   - Used to convert between wire format (JSON) and typed messages

3. **MessageHandler** (`src/MessageHandler.hpp/.cpp`): Message routing interface
   - `IMessageHandler`: Abstract interface for application message handling
   - `MessageRouter`: Routes parsed protocol messages to application handler
   - Separates transport (WsClient) from application logic

4. **WsClient** (`src/WsClient.hpp/.cpp`): Production WebSocket client (refactored)
   - **Pimpl pattern** with `std::unique_ptr<Impl>` for clean separation
   - **State machine**: Disconnected → Connecting → Connected → Closing
   - **Synchronization**:
     - Mutex protects connection state, preventing concurrent state corruption
     - Condition variable for `WaitForConnection()` with timeout
     - All send methods check state before attempting dispatch
   - **Thread-safe callbacks**: State updates and message routing happen safely
   - Binary reassembly: Tracks expected size vs. received bytes
   - No blocking operations in callbacks (async design)

5. **main.cpp**: Complete example demonstrating proper usage
   - Custom `TallyIXMessageHandler` implementing protocol handling
   - Shows proper initialization → connect → wait → send → close sequence
   - Protocol message construction using `Protocol::Message`
   - Error handling at each step

### Key Architectural Decisions

1. **State Machine + Condition Variables**: Replaces arbitrary sleep calls
   - `Connect()` is non-blocking; `WaitForConnection(timeout)` blocks with timeout
   - Callback thread signals state changes via condition variable
   
2. **Thread-Safe Logging**: All logs protected by mutex
   - Callbacks and main thread can log simultaneously without corruption
   
3. **Protocol-First Design**: Typed messages decouple application from JSON format
   - Easy to evolve protocol (add fields, new types) without changing app code
   
4. **Message Router Pattern**: Application code implements `IMessageHandler` interface
   - Can swap implementations without modifying WsClient
   - Supports multi-handler scenarios (logging, metrics, business logic)
   
5. **No Raw Pointers**: Uses `std::unique_ptr` for exception-safe cleanup

### Data Flow

```
IXWebSocket thread:        Main thread:
  OnMessage() ──────────>  SetMessageHandler()
    ↓                      ↓
  Protocol::Parse()        WsClient::SendText()
    ↓                      WsClient::SendBinary()
  MessageRouter            ↓
    ↓                      IXWebSocket::send()
  IMessageHandler::On*()
```

## Build & Environment

- **Toolchain**: Visual Studio 2022 (v143, C++17)
- **Platforms**: Win32 and x64 configurations (Debug/Release)
- **External Dependencies**:
  - **IXWebSocket**: Compiled at `D:\ALL_BINS\TallyIXWebSocketPOC\IXWebSocket\build\{Debug|Release}\ixwebsocket.lib`
  - **OpenSSL**: `C:\Program Files\OpenSSL-Win64` (WSS support)
  - **System**: Ws2_32.lib, Crypt32.lib

### Include Paths (from `.vcxproj`)
- Debug|x64: `IXWebSocket/` and `IXWebSocket/third_party/`
- Release|x64: `IXWebSocket/ixwebsocket/` and `IXWebSocket/`

## Testing & Local Development

### Mock Server
Run `src/server_mock/echo_server.py`:
```bash
pip install websockets
python echo_server.py
# WebSocket echo server running on ws://127.0.0.1:9001
```

### Local Workflow
1. Start mock server in terminal
2. Build VS_Websocket.sln in Visual Studio (or `msbuild VS_Websocket.sln /p:Configuration=Debug /p:Platform=x64`)
3. Run x64\Debug\VS_Websocket.exe
4. Observe sequence: Connect → Wait → Send Hello → Send Binary Metadata → Send 1MB → Echo Response → Close

## Code Patterns & Conventions

### Creating a WsClient (main.cpp pattern)
```cpp
Protocol::Config config;
config.connectionTimeoutMs = 10000;
config.maxBinaryPayloadSize = 100 * 1024 * 1024;

WsClient client(config);
MyMessageHandler handler;
client.SetMessageHandler(&handler);

client.Open();                    // Initialize IXWebSocket
client.Connect("ws://host:port"); // Non-blocking
if (!client.WaitForConnection(10000)) { /* timeout */ }  // Blocking with sync

client.SendText(json);
client.SendBinary(data, size);
client.Close();  // Graceful shutdown
```

### Implementing IMessageHandler
```cpp
class MyHandler : public IMessageHandler {
    void OnTextMessage(const Protocol::Message& msg) override {
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

### Message Protocol
- **Text messages**: JSON with `type`, `msg_id`, optional `content`
- **Binary transfer**: Preceded by JSON `{"type":"binary_start","size":N}`
- Parsing via `Protocol::ParseJsonMessage()` yields typed `Protocol::Message`
- Serialization via `Protocol::SerializeJsonMessage()`

### Logging Convention
All components log to thread-safe Logger with tags:
- `[DBG][WsClient]` - debug details
- `[INF][WsClient]` - connection state changes
- `[WRN][App]` - recoverable issues
- `[ERR][WsClient]` - connection errors

## Extending the Client

### Adding a New Message Type
1. Add to `Protocol::MessageType` enum
2. Update `Protocol::ParseJsonMessage()` to recognize it
3. Update `Protocol::SerializeJsonMessage()` to emit it
4. Add handler method to `IMessageHandler` (e.g., `OnNewMessage()`)
5. Call it from `MessageRouter::RouteMessage()`

### Adding Protocol Configuration
1. Add field to `Protocol::Config` struct
2. Pass config in `WsClient` constructor
3. Use in appropriate method (e.g., `mImpl->config.maxBinaryPayloadSize` in `SendBinary()`)

### Integrating Custom Logging
1. Implement custom `ILogger` interface (optional future work)
2. Inject via `WsClient` constructor
3. Replace `Logger::Instance()` calls with injected logger

## Known Limitations & Future Work

- Simple JSON parsing (no external library); breaks on special characters in content
- No automatic reconnection (design choice—app decides on retry policy)
- Binary fragmentation handled per-message only (no multi-message buffering)
- No compression support yet (reserved in `Protocol::Config`)
- Mock server just echoes (doesn't validate protocol)

## Thread Safety Summary

| Component      | Access Model          | Synchronization |
|----------------|----------------------|-----------------|
| Logger         | Singleton             | Mutex (lock_guard) |
| WsClient state | Shared (main + IXWs)  | Mutex + Condition Variable |
| MessageRouter  | Called from IXWs      | No mutex (write from callback only) |
| Protocol       | Stateless             | None needed |
| IMessageHandler| Called from IXWs      | App responsible for internal sync |
