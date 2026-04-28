#include <ixwebsocket/IXNetSystem.h> // Required for Windows sockets
#include <ixwebsocket/IXWebSocketServer.h>
#include <nlohmann/json.hpp>
#include "DatabaseManager.h"
#include "CryptoManager.h"
#include <string>
#include <windows.h>

using json = nlohmann::json;

// Global database instance (defined in App.xaml.cpp)
extern Discrypt::DatabaseManager g_database;
extern std::wstring g_partnerHandle;

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

                    if (msg->type == ix::WebSocketMessageType::Open) {
                        OutputDebugStringW(L"[IPC] Browser connected to WebSocket server!\n");
                    }
                    else if (msg->type == ix::WebSocketMessageType::Message) {
                        try {
                            auto incoming_data = json::parse(msg->str);

                            // Check what kind of event the browser sent
                            std::string event_type = incoming_data.value("type", "UNKNOWN");
                            OutputDebugStringW((L"[IPC] Event type: " + std::wstring(event_type.begin(), event_type.end()) + L"\n").c_str());

                            if (event_type == "CHAT_SWITCH") {
                                std::string current_handle = incoming_data["handle"];
                                g_partnerHandle = std::wstring(current_handle.begin(), current_handle.end());
                                OutputDebugStringW((L"[IPC] User switched chat to: " + g_partnerHandle + L"\n").c_str());
                            }
							else if (event_type == "INIT_HANDSHAKE") {
								std::string partner_handle = incoming_data.value("partnerHandle", "");
								std::wstring wPartnerHandle(partner_handle.begin(), partner_handle.end());
								OutputDebugStringW(L"[IPC] Command Intercepted: User initiated handshake (!handshake)\n");
								OutputDebugStringW((L"[IPC] Target partner handle: " + wPartnerHandle + L"\n").c_str());

								// Reset any existing session for this partner
								g_database.DeleteSession(wPartnerHandle);
								g_database.CreateSession(wPartnerHandle, L"PENDING_INITIATOR");

								// Generate ECDH key pair
								Discrypt::EncryptionSession session;
								if (!Discrypt::CryptoManager::GenerateDHKeyPair(session))
								{
									OutputDebugStringW(L"[IPC] INIT_HANDSHAKE: Failed to generate DH key pair\n");
									return;
								}

								// Persist keys into the session row
								std::vector<uint8_t> privKeyBlob = Discrypt::CryptoManager::ExportPrivateKeyBlob(session);
								std::vector<uint8_t> pubKey(session.publicKeyBlob.begin(), session.publicKeyBlob.end());
								g_database.SaveSessionKeys(wPartnerHandle, privKeyBlob, pubKey);

								// Base64-encode the public key for the wire format
								std::wstring pubKeyB64 = Discrypt::CryptoManager::Base64Encode(session.publicKeyBlob);

								OutputDebugStringW((L"[IPC] Generated public key (base64): " + pubKeyB64 + L"\n").c_str());

								// Send handshake initiation message: [HANDSHAKE_INIT]:<base64_pubkey>
								std::wstring messageW = L"[HANDSHAKE_INIT]:" + pubKeyB64;
								std::string messageUTF8(messageW.begin(), messageW.end());

								json response;
								response["type"] = "SEND_DISCORD_MESSAGE";
								response["text"] = messageUTF8;

								std::string out = response.dump();
								webSocket.send(out);

								OutputDebugStringW((L"[IPC] Sent handshake init to Discord: " + messageW + L"\n").c_str());

								Discrypt::CryptoManager::CleanupSession(session);
							}
                            else if (event_type == "OUTGOING_MESSAGE") {
                                std::string plaintext = incoming_data.value("text", "");
                                OutputDebugStringW((L"[IPC] Outgoing message intercepted: " + std::wstring(plaintext.begin(), plaintext.end()) + L"\n").c_str());

                                // TODO: Encrypt plaintext here before sending to Discord.
                                // For now, pass through unchanged.
                                json response;
                                response["type"] = "SEND_DISCORD_MESSAGE";
                                response["text"] = "secret";

                                webSocket.send(response.dump());
                                OutputDebugStringW(L"[IPC] Outgoing message forwarded to Discord (plaintext passthrough)\n");
                            }
                            else if (event_type == "ACCEPT_HANDSHAKE") {
                                std::string partner_handle = incoming_data.value("partnerHandle", "");
                                std::string partnerPubKeyB64Utf8 = incoming_data.value("partnerPublicKey", "");
                                std::wstring wPartnerHandle(partner_handle.begin(), partner_handle.end());
                                std::wstring partnerPubKeyB64(partnerPubKeyB64Utf8.begin(), partnerPubKeyB64Utf8.end());

                                OutputDebugStringW(L"[IPC] ACCEPT_HANDSHAKE: generating keypair and deriving shared secret\n");
                                OutputDebugStringW((L"[IPC] Partner: " + wPartnerHandle + L"\n").c_str());

                                // Decode partner's (initiator's) public key
                                std::vector<BYTE> partnerPubKeyBlob = Discrypt::CryptoManager::Base64Decode(partnerPubKeyB64);
                                if (partnerPubKeyBlob.empty())
                                {
                                    OutputDebugStringW(L"[IPC] ACCEPT_HANDSHAKE: failed to decode partner public key\n");
                                    return;
                                }

                                // Create fresh session row
                                g_database.DeleteSession(wPartnerHandle);
                                g_database.CreateSession(wPartnerHandle, L"PENDING_RECEIVER");

                                // Generate own keypair
                                Discrypt::EncryptionSession session;
                                if (!Discrypt::CryptoManager::GenerateDHKeyPair(session))
                                {
                                    OutputDebugStringW(L"[IPC] ACCEPT_HANDSHAKE: failed to generate DH key pair\n");
                                    return;
                                }

                                // Derive shared secret immediately — we have both keys
                                if (!Discrypt::CryptoManager::DeriveSharedSecret(session, partnerPubKeyBlob))
                                {
                                    OutputDebugStringW(L"[IPC] ACCEPT_HANDSHAKE: failed to derive shared secret\n");
                                    Discrypt::CryptoManager::CleanupSession(session);
                                    return;
                                }

                                // Persist everything
                                std::vector<uint8_t> privBlob = Discrypt::CryptoManager::ExportPrivateKeyBlob(session);
                                std::vector<uint8_t> pubBlob(session.publicKeyBlob.begin(), session.publicKeyBlob.end());
                                std::vector<uint8_t> secret(session.sharedSecretData.begin(), session.sharedSecretData.end());

                                g_database.SaveSessionKeys(wPartnerHandle, privBlob, pubBlob);
                                g_database.SaveSharedSecret(wPartnerHandle, secret);
                                g_database.UpdateSessionState(wPartnerHandle, L"SECURED");

                                OutputDebugStringW((L"[IPC] ACCEPT_HANDSHAKE: shared secret derived and stored. Secret (hex): " +
                                    Discrypt::CryptoManager::GetSharedSecretHex(session) + L"\n").c_str());

                                // Send our public key back: [HANDSHAKE_ACK]:<our_pubkey_b64>
                                std::wstring ownPubKeyB64 = Discrypt::CryptoManager::Base64Encode(session.publicKeyBlob);
                                std::wstring messageW = L"[HANDSHAKE_ACK]:" + ownPubKeyB64;
                                std::string messageUTF8(messageW.begin(), messageW.end());

                                json response;
                                response["type"] = "SEND_DISCORD_MESSAGE";
                                response["text"] = messageUTF8;
                                webSocket.send(response.dump());

                                OutputDebugStringW((L"[IPC] ACCEPT_HANDSHAKE: sent ACK to Discord: " + messageW + L"\n").c_str());

                                Discrypt::CryptoManager::CleanupSession(session);
                            }
                            else if (event_type == "MESSAGE") {
                                std::string original_text = incoming_data.value("text", "");
                                int msg_id = incoming_data.value("id", 0);
                                std::string msg_partner = incoming_data.value("partnerHandle", "");

                                OutputDebugStringW((L"[IPC] MESSAGE: " + std::wstring(original_text.begin(), original_text.end()) + L"\n").c_str());

                                const std::string ackPrefix = "[HANDSHAKE_ACK]:";
                                if (original_text.rfind(ackPrefix, 0) == 0)
                                {
                                    std::wstring wPartnerHandle = msg_partner.empty() ? g_partnerHandle
                                        : std::wstring(msg_partner.begin(), msg_partner.end());

                                    // Only the initiator (PENDING_INITIATOR) needs to derive the shared secret here.
                                    // Acceptors are already SECURED from ACCEPT_HANDSHAKE — skip.
                                    SessionRecord dbSession = g_database.GetSession(wPartnerHandle);
                                    if (dbSession.state != L"PENDING_INITIATOR")
                                    {
                                        OutputDebugStringW((L"[IPC] HANDSHAKE_ACK: skipping — session state is '" + dbSession.state + L"' (not PENDING_INITIATOR)\n").c_str());
                                        return;
                                    }

                                    OutputDebugStringW((L"[IPC] HANDSHAKE_ACK received from: " + wPartnerHandle + L"\n").c_str());

                                    std::string partnerPubKeyB64Utf8 = original_text.substr(ackPrefix.size());
                                    std::wstring partnerPubKeyB64(partnerPubKeyB64Utf8.begin(), partnerPubKeyB64Utf8.end());
                                    std::vector<BYTE> partnerPubKeyBlob = Discrypt::CryptoManager::Base64Decode(partnerPubKeyB64);

                                    if (partnerPubKeyBlob.empty())
                                    {
                                        OutputDebugStringW(L"[IPC] HANDSHAKE_ACK: failed to decode partner public key\n");
                                        return;
                                    }

                                    // Load session — also used for state guard and private key
                                    if (dbSession.myPrivateKey.empty())
                                    {
                                        OutputDebugStringW(L"[IPC] HANDSHAKE_ACK: no private key found in DB for this session\n");
                                        return;
                                    }

                                    // Reconstruct BCrypt session from stored private key
                                    Discrypt::EncryptionSession session;
                                    std::vector<BYTE> privBlob(dbSession.myPrivateKey.begin(), dbSession.myPrivateKey.end());
                                    if (!Discrypt::CryptoManager::ImportPrivateKey(session, privBlob))
                                    {
                                        OutputDebugStringW(L"[IPC] HANDSHAKE_ACK: failed to import private key from DB\n");
                                        return;
                                    }

                                    // Derive shared secret
                                    if (!Discrypt::CryptoManager::DeriveSharedSecret(session, partnerPubKeyBlob))
                                    {
                                        OutputDebugStringW(L"[IPC] HANDSHAKE_ACK: failed to derive shared secret\n");
                                        Discrypt::CryptoManager::CleanupSession(session);
                                        return;
                                    }

                                    // Persist shared secret and finalize
                                    std::vector<uint8_t> secret(session.sharedSecretData.begin(), session.sharedSecretData.end());
                                    g_database.SaveSharedSecret(wPartnerHandle, secret);
                                    g_database.UpdateSessionState(wPartnerHandle, L"SECURED");

                                    OutputDebugStringW((L"[IPC] HANDSHAKE_ACK: handshake complete. Shared secret (hex): " +
                                        Discrypt::CryptoManager::GetSharedSecretHex(session) + L"\n").c_str());

                                    Discrypt::CryptoManager::CleanupSession(session);
                                }
                                else if (original_text.rfind("[ENC]:", 0) == 0)
                                {
                                    // TODO: decrypt with shared secret
                                    json response;
                                    response["id"] = msg_id;
                                    response["text"] = "🔒 [Decryption not yet implemented]";
                                    webSocket.send(response.dump());
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

            auto res = m_server.listen();
            if (!res.first) {
                std::string error = "[IPC] Failed to start server: " + res.second;
                OutputDebugStringW((L"[IPC] Failed to start server: " + std::wstring(res.second.begin(), res.second.end()) + L"\n").c_str());
                return false;
            }

            OutputDebugStringW(L"[IPC] Server listen() succeeded, starting server threads...\n");
            m_server.start();
            OutputDebugStringW((L"[IPC] WebSocket Server listening on port " + std::to_wstring(m_server.getPort()) + L"\n").c_str());
            return true;
        }

        void stop() {
            m_server.stop();
            OutputDebugStringW(L"[IPC] WebSocket Server stopped\n");
        }
    };

} // namespace Discrypt