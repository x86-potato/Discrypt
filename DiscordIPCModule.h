#include <ixwebsocket/IXNetSystem.h> // Required for Windows sockets
#include <ixwebsocket/IXWebSocketServer.h>
#include <nlohmann/json.hpp>
#include <string>
#include <windows.h>

using json = nlohmann::json;

namespace Discrypt {

class DiscordIpcModule {
private:
    ix::WebSocketServer m_server;

public:
    // Bind to localhost on port 9090
    DiscordIpcModule(int port = 9090) : m_server(port, "127.0.0.1") {

        // Required strictly for Windows (initializes WSAStartup)
        ix::initNetSystem();
        OutputDebugStringW(L"[IPC] Network system initialized\n");

        // Register our message handler
        m_server.setOnClientMessageCallback([](std::shared_ptr<ix::ConnectionState> connectionState,
            ix::WebSocket& webSocket,
            const ix::WebSocketMessagePtr& msg) {

                OutputDebugStringW(L"[IPC] === CALLBACK INVOKED ===\n");
                OutputDebugStringW((L"[IPC] Message type: " + std::to_wstring((int)msg->type) + L"\n").c_str());

                if (msg->type == ix::WebSocketMessageType::Open) {
                    OutputDebugStringW(L"[IPC] Browser connected to WebSocket server!\n");
                }
                else if (msg->type == ix::WebSocketMessageType::Message) {
                    OutputDebugStringW(L"[IPC] Received message from client\n");
                    OutputDebugStringW((L"[IPC] Raw message: " + std::wstring(msg->str.begin(), msg->str.end()) + L"\n").c_str());

                    try {
                        auto incoming_data = json::parse(msg->str);

                        // Check what kind of event the browser sent
                        std::string event_type = incoming_data.value("type", "UNKNOWN");
                        OutputDebugStringW((L"[IPC] Event type: " + std::wstring(event_type.begin(), event_type.end()) + L"\n").c_str());

                        if (event_type == "CHAT_SWITCH") {
                            std::string current_handle = incoming_data["handle"];
                            OutputDebugStringW((L"[IPC] User switched chat to: " + std::wstring(current_handle.begin(), current_handle.end()) + L"\n").c_str());

                            // TODO: Update global state here (e.g., g_partnerHandle)
                        }
                        else if (event_type == "MESSAGE") {
                            std::string original_text = incoming_data["text"];
                            int msg_id = incoming_data["id"];

                            OutputDebugStringW((L"[IPC] Message: " + std::wstring(original_text.begin(), original_text.end()) + L"\n").c_str());

                            if (original_text.find("test") == 0) {
                                json response;
                                response["id"] = msg_id;
                                response["text"] = "🟢 [C++ WinUI] Hello from native code via IXWebSocket! (Original: " + original_text + ")";

                                webSocket.send(response.dump());
                                OutputDebugStringW(L"[IPC] Sent response to client\n");
                            }
                        }
                        else {
                            OutputDebugStringW((L"[IPC] Unknown event type: " + std::wstring(event_type.begin(), event_type.end()) + L"\n").c_str());
                        }
                    }
                    catch (const json::exception& e) {
                        std::string error = std::string("JSON Parsing error: ") + e.what();
                        OutputDebugStringW((L"[IPC] " + std::wstring(error.begin(), error.end()) + L"\n").c_str());
                    }
                }
                else if (msg->type == ix::WebSocketMessageType::Close) {
                    OutputDebugStringW(L"[IPC] Browser disconnected\n");
                }
                else if (msg->type == ix::WebSocketMessageType::Error) {
                    OutputDebugStringW(L"[IPC] WebSocket ERROR occurred\n");
                    OutputDebugStringW((L"[IPC] Error: " + std::wstring(msg->errorInfo.reason.begin(), msg->errorInfo.reason.end()) + L"\n").c_str());
                }
                else {
                    OutputDebugStringW(L"[IPC] Unknown message type\n");
                }
            });

        OutputDebugStringW(L"[IPC] Callback registered\n");
    }

    ~DiscordIpcModule() {
        stop();
        ix::uninitNetSystem();
        OutputDebugStringW(L"[IPC] Network system uninitialized\n");
    }

    bool start() {
        OutputDebugStringW(L"[IPC] Attempting to start WebSocket server...\n");

        // listen() binds the socket to the port
        auto res = m_server.listen();
        if (!res.first) {
            std::string error = "[IPC] Failed to start server: " + res.second;
            OutputDebugStringW((L"[IPC] Failed to start server: " + std::wstring(res.second.begin(), res.second.end()) + L"\n").c_str());
            return false;
        }

        OutputDebugStringW(L"[IPC] Server listen() succeeded, starting server threads...\n");

        // start() automatically spins up the background processing threads!
        m_server.start();
        OutputDebugStringW((L"[IPC] WebSocket Server listening on port " + std::to_wstring(m_server.getPort()) + L"\n").c_str());
        OutputDebugStringW(L"[IPC] Server is ready to accept connections at ws://127.0.0.1:9090\n");
        return true;
    }

    void stop() {
        m_server.stop();
        OutputDebugStringW(L"[IPC] WebSocket Server stopped\n");
    }
};

} // namespace Discrypt
