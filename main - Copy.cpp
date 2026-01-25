#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>

#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>

static void SleepMs(int ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

int main()
{
    std::cout << "Starting IXWebSocket POC\n";

    // Required on Windows
    if (!ix::initNetSystem())
    {
        std::cerr << "Failed to init network system\n";
        return -1;
    }

    // Our websocket object
    ix::WebSocket ws;

    std::string url("ws://127.0.0.1:9001");
    ws.setUrl(url);

    ws.disableAutomaticReconnection(); //TODO: Is this required?

    ws.setOnMessageCallback(
        [&](const ix::WebSocketMessagePtr& msg)
        {
            switch (msg->type)
            {
                case ix::WebSocketMessageType::Open:
                {
                    std::cout << "[OPEN] Connected\n";

                    std::string json =
                        R"({"type":"hello","msg_id":"1","content":"Hello from POC"})";

                    ws.send(json);
                    std::cout << "[SEND][TEXT] " << json << "\n";

                    std::string meta =
                        R"({"type":"binary_start","msg_id":"2","size":1048576})";

                    ws.send(meta);
                    std::cout << "[SEND][TEXT] " << meta << "\n";

                    std::vector<uint8_t> bin(1024 * 1024, 0xAB);

                    ws.sendBinary(std::string(
                        reinterpret_cast<const char*>(bin.data()),
                        bin.size()));

                    std::cout << "[SEND][BINARY] 1MB\n";
                    break;
                }

                case ix::WebSocketMessageType::Message:
                {
                    if (msg->binary)
                    {
                        std::cout << "[RECV][BINARY] size="
                                  << msg->str.size() << "\n";
                    }
                    else
                    {
                        std::cout << "[RECV][TEXT] " << msg->str << "\n";
                    }
                    break;
                }

                case ix::WebSocketMessageType::Close:
                    std::cout << "[CLOSE]\n";
                    break;

                case ix::WebSocketMessageType::Error:
                    std::cout << "[ERROR] " << msg->errorInfo.reason << "\n";
                    break;

                default:
                    break;
            }
        });

    // Now that our callback is setup, we can start our background thread and receive messages
    ws.start();
    std::cout << "Connecting...\n";

    SleepMs(15000);

    // Send a message to the server
    ws.send("hello world");

    // Display a prompt
    std::cout << "> " << std::flush;


    ws.stop();
    SleepMs(1000);

    std::cout << "Exiting\n";
    return 0;
}
