#pragma once

#include <string>
#include <vector>
#include <cstdint>

// Forward declarations
struct sqlite3;
struct sqlite3_stmt;

namespace Discrypt
{
    // Mirrors the 'sessions' table
    struct SessionRecord
    {
        std::wstring partnerHandle;
        std::wstring state;                   // PENDING_INITIATOR | PENDING_RECEIVER | SECURED
        std::vector<uint8_t> myPrivateKey;
        std::vector<uint8_t> myPublicKey;
        std::vector<uint8_t> sharedSecret;
        std::wstring lastUpdated;
    };

    class DatabaseManager
    {
    public:
        DatabaseManager();
        ~DatabaseManager();

        // Open/create the database and ensure tables exist
        bool Initialize(const std::wstring& dbPath);

        // Drop all tables and recreate them (useful during development)
        bool ResetDatabase();

        // ---- Settings (key/value store) ----
        bool SaveSetting(const std::wstring& key, const std::wstring& value);
        std::wstring LoadSetting(const std::wstring& key);

        // Convenience wrappers over SaveSetting / LoadSetting
        bool         SaveUserHandle(const std::wstring& handle);
        std::wstring LoadUserHandle();

        // ---- Sessions ----
        bool                       CreateSession(const std::wstring& partnerHandle, const std::wstring& state);
        bool                       UpdateSessionState(const std::wstring& partnerHandle, const std::wstring& state);
        bool                       SaveSessionKeys(const std::wstring& partnerHandle,
            const std::vector<uint8_t>& privateKey,
            const std::vector<uint8_t>& publicKey);
        bool                       SaveSharedSecret(const std::wstring& partnerHandle,
            const std::vector<uint8_t>& sharedSecret);
        SessionRecord              GetSession(const std::wstring& partnerHandle);
        std::vector<SessionRecord> GetAllSessions();
        bool                       DeleteSession(const std::wstring& partnerHandle);
        bool                       ClearAllSessions();

    private:
        sqlite3* m_db;
        std::wstring  m_dbPath;

        bool          CreateTables();
        bool          ExecuteSQL(const std::string& sql);
        SessionRecord ReadSessionRow(sqlite3_stmt* stmt);

        // Windows string conversions
        std::string   WideToUTF8(const std::wstring& wstr);
        std::wstring  UTF8ToWide(const std::string& str);
    };
}