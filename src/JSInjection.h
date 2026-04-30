#pragma once
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXHttpClient.h>
#include <ixwebsocket/IXWebSocket.h>
#include <nlohmann/json.hpp>
#include <string>
#include <future>
#include <memory>
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <windows.h>

using json = nlohmann::json;

namespace Discrypt {

    class DiscordInjector {
    public:
        static bool Inject() {
            std::wcout << L"[Injector] --- STARTING INJECTION SEQUENCE ---\n";
            ix::initNetSystem();

            // 1. Discover the Discord Tab via HTTP
            ix::HttpClient httpClient;
            ix::HttpRequestArgsPtr args = httpClient.createRequest();

            std::wcout << L"[Injector] Requesting CDP targets from http://127.0.0.1:9222/json/list...\n";
            auto response = httpClient.get("http://127.0.0.1:9222/json/list", args);

            if (response->statusCode != 200) {
                std::wcout << L"[Injector] FAIL: HTTP GET returned status code: ";
                //OutputDebugStringW(std::to_wstring(response->statusCode).c_str());
                //OutputDebugStringW(L"\n[Injector] -> Is Discord running with --remote-debugging-port=9222 ?\n");
                return false;
            }

            std::string targetWsUrl = "";
            try {
                auto targets = json::parse(response->body);
                for (const auto& target : targets) {
                    if (target.contains("type") && target["type"] == "page") {
                        targetWsUrl = target["webSocketDebuggerUrl"];
                        //OutputDebugStringW(L"[Injector] SUCCESS: Found Discord page target!\n");
                        break;
                    }
                }
            }
            catch (const json::exception& e) {
                //OutputDebugStringW(L"[Injector] FAIL: Could not parse JSON from port 9222.\n");
                return false;
            }

            if (targetWsUrl.empty()) {
                //OutputDebugStringW(L"[Injector] FAIL: No 'page' target found in the CDP list.\n");
                return false;
            }

            size_t pos = targetWsUrl.find("127.0.0.1");
            if (pos != std::string::npos) {
                targetWsUrl.replace(pos, 9, "localhost");
            }

            //OutputDebugStringW((L"[Injector] Bypassing Chromium Security. Target: " + std::wstring(targetWsUrl.begin(), targetWsUrl.end()) + L"\n").c_str());

            // 2. Connect to the DevTools WebSocket
            ix::WebSocket webSocket;
            webSocket.setUrl(targetWsUrl);

            // 3. Spoof the exact headers of the built-in Chrome DevTools window
            ix::WebSocketHttpHeaders extraHeaders;
            extraHeaders["Host"] = "localhost:9222";
            extraHeaders["Origin"] = "devtools://devtools"; // The Master Key
            extraHeaders["User-Agent"] = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";

            webSocket.setExtraHeaders(extraHeaders);
            // FIX: Use shared_ptr so the background thread doesn't access a destroyed promise on timeout
            auto sharedPromise = std::make_shared<std::promise<bool>>();
            auto injectionFuture = sharedPromise->get_future();

            webSocket.setOnMessageCallback([&webSocket, sharedPromise](const ix::WebSocketMessagePtr& msg) {
                if (msg->type == ix::WebSocketMessageType::Open) {
                    //OutputDebugStringW(L"[Injector] CDP WebSocket OPEN. Sending JS payload...\n");

                    json cdpRequest;
                    cdpRequest["id"] = 1;
                    cdpRequest["method"] = "Runtime.evaluate";
                    cdpRequest["params"]["awaitPromise"] = true;
                    cdpRequest["params"]["expression"] = GetJavascriptPayload();
                    cdpRequest["params"]["returnByValue"] = true;

                    webSocket.send(cdpRequest.dump());
                }
                else if (msg->type == ix::WebSocketMessageType::Message) {
                    try {
                        auto res = json::parse(msg->str);
                        if (res.contains("id") && res["id"] == 1) {

                            if (res.contains("result") && res["result"].contains("exceptionDetails")) {
                                //OutputDebugStringW(L"[Injector] FAIL: JS threw an exception during execution!\n");
                                std::string ex = res["result"]["exceptionDetails"].dump();
                                //OutputDebugStringW((L"[Injector] Exception details: " + std::wstring(ex.begin(), ex.end()) + L"\n").c_str());
                                try { sharedPromise->set_value(false); }
                                catch (...) {}
                            }
                            else {
                                //OutputDebugStringW(L"[Injector] SUCCESS: JS payload executed and returned.\n");
                                try { sharedPromise->set_value(true); }
                                catch (...) {}
                            }
                        }
                    }
                    catch (...) {}
                }
                else if (msg->type == ix::WebSocketMessageType::Error) {
                    auto errorMsg = msg->errorInfo.reason;
                    //OutputDebugStringW(L"[Injector] FAIL: CDP WebSocket encountered an error.\n");
                    //OutputDebugStringW((L"[Injector] Error details: " + std::wstring(errorMsg.begin(), errorMsg.end()) + L"\n").c_str());
                    try { sharedPromise->set_value(false); }
                    catch (...) {}
                }
                });

            webSocket.start();

            // Wait for up to 5 seconds
            auto status = injectionFuture.wait_for(std::chrono::seconds(5));
            webSocket.stop();

            if (status == std::future_status::ready) {
                return injectionFuture.get();
            }
            else {
                //OutputDebugStringW(L"[Injector] FAIL: Injection timed out waiting for CDP response.\n");
                return false;
            }
        }

    private:
        static std::string GetJavascriptPayload() {
            // Get the full path of the current executable
            wchar_t buffer[MAX_PATH];
            GetModuleFileNameW(NULL, buffer, MAX_PATH);

            // Resolve the path to the executable's directory
            std::filesystem::path exePath(buffer);
            std::filesystem::path payloadPath = exePath.parent_path() / L"injection.js";

            // Open the file
            std::ifstream jsFile(payloadPath);
            if (!jsFile.is_open()) {
                std::wcout << L"[Injector] FAIL: Could not open payload file at " << payloadPath.wstring() << L"\n";
                // Return a safe fallback to prevent devtools from crashing or parsing empty expressions
                return "console.error('[Discrypt] FAIL: injection.js not found in directory');";
            }

            // Read the file contents into a string
            std::stringstream fileStream;
            fileStream << jsFile.rdbuf();
            return fileStream.str();
        }
    };
}; // namespace Discrypt