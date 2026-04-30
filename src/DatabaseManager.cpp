#include "DatabaseManager.h"
#include "sqlite3.h"
#include <iostream> // Added for std::wcout / std::cout
#include <windows.h> // Required for WideCharToMultiByte and MultiByteToWideChar

namespace Discrypt
{
    DatabaseManager::DatabaseManager() : m_db(nullptr)
    {
    }

    DatabaseManager::~DatabaseManager()
    {
        if (m_db)
        {
            sqlite3_close(m_db);
            m_db = nullptr;
        }
    }

    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------

    bool DatabaseManager::Initialize(const std::wstring& dbPath)
    {
        m_dbPath = dbPath;
        std::string utf8Path = WideToUTF8(dbPath);

        std::wcout << L"[DatabaseManager] Opening database at: " << dbPath << L"\n";

        int rc = sqlite3_open(utf8Path.c_str(), &m_db);
        if (rc != SQLITE_OK)
        {
            std::wcout << L"[DatabaseManager] sqlite3_open failed: " << UTF8ToWide(sqlite3_errmsg(m_db)) << L"\n";
            return false;
        }

        std::wcout << L"[DatabaseManager] Database opened successfully\n";

        if (!CreateTables())
        {
            std::wcout << L"[DatabaseManager] Failed to create tables\n";
            return false;
        }

        std::wcout << L"[DatabaseManager] Tables ready\n";
        return true;
    }

    bool DatabaseManager::ResetDatabase()
    {
        std::wcout << L"[DatabaseManager] Resetting database\n";

        bool ok = true;
        ok &= ExecuteSQL("DROP TABLE IF EXISTS sessions;");
        ok &= ExecuteSQL("DROP TABLE IF EXISTS settings;");

        if (!ok)
        {
            std::wcout << L"[DatabaseManager] Failed to drop tables\n";
            return false;
        }

        if (!CreateTables())
        {
            std::wcout << L"[DatabaseManager] Failed to recreate tables\n";
            return false;
        }

        std::wcout << L"[DatabaseManager] Database reset successfully\n";
        return true;
    }

    // -------------------------------------------------------------------------
    // Private helpers
    // -------------------------------------------------------------------------

    bool DatabaseManager::CreateTables()
    {
        const char* sql =
            "CREATE TABLE IF NOT EXISTS sessions ("
            "    partner_handle TEXT PRIMARY KEY,"
            "    state TEXT NOT NULL,"
            "    my_private_key BLOB,"
            "    my_public_key BLOB,"
            "    shared_secret BLOB,"
            "    last_updated DATETIME DEFAULT CURRENT_TIMESTAMP"
            ");"
            "CREATE TABLE IF NOT EXISTS settings ("
            "    key TEXT PRIMARY KEY,"
            "    value TEXT"
            ");";

        return ExecuteSQL(sql);
    }

    bool DatabaseManager::ExecuteSQL(const std::string& sql)
    {
        char* errMsg = nullptr;
        int rc = sqlite3_exec(m_db, sql.c_str(), nullptr, nullptr, &errMsg);
        if (rc != SQLITE_OK)
        {
            std::wcout << L"[DatabaseManager] SQL error: ";
            if (errMsg)
            {
                std::wcout << UTF8ToWide(errMsg);
                sqlite3_free(errMsg);
            }
            std::wcout << L"\n";
            return false;
        }
        return true;
    }

    std::string DatabaseManager::WideToUTF8(const std::wstring& wstr)
    {
        if (wstr.empty()) return {};
        int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.length()), nullptr, 0, nullptr, nullptr);
        std::string result(size, 0);
        WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.length()), &result[0], size, nullptr, nullptr);
        return result;
    }

    std::wstring DatabaseManager::UTF8ToWide(const std::string& str)
    {
        if (str.empty()) return {};
        int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.length()), nullptr, 0);
        std::wstring result(size, 0);
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.length()), &result[0], size);
        return result;
    }

    // -------------------------------------------------------------------------
    // Settings
    // -------------------------------------------------------------------------

    bool DatabaseManager::SaveSetting(const std::wstring& key, const std::wstring& value)
    {
        const char* sql = "INSERT OR REPLACE INTO settings (key, value) VALUES (?, ?);";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

        std::string k = WideToUTF8(key);
        std::string v = WideToUTF8(value);
        sqlite3_bind_text(stmt, 1, k.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, v.c_str(), -1, SQLITE_TRANSIENT);

        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return rc == SQLITE_DONE;
    }

    std::wstring DatabaseManager::LoadSetting(const std::wstring& key)
    {
        const char* sql = "SELECT value FROM settings WHERE key = ?;";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return L"";

        std::string k = WideToUTF8(key);
        sqlite3_bind_text(stmt, 1, k.c_str(), -1, SQLITE_TRANSIENT);

        std::wstring result;
        if (sqlite3_step(stmt) == SQLITE_ROW)
        {
            const char* v = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (v) result = UTF8ToWide(v);
        }

        sqlite3_finalize(stmt);
        return result;
    }

    bool DatabaseManager::SaveUserHandle(const std::wstring& handle)
    {
        return SaveSetting(L"user_handle", handle);
    }

    std::wstring DatabaseManager::LoadUserHandle()
    {
        return LoadSetting(L"user_handle");
    }

    // -------------------------------------------------------------------------
    // Sessions
    // -------------------------------------------------------------------------

    bool DatabaseManager::CreateSession(const std::wstring& partnerHandle, const std::wstring& state)
    {
        const char* sql =
            "INSERT OR REPLACE INTO sessions (partner_handle, state, last_updated) "
            "VALUES (?, ?, CURRENT_TIMESTAMP);";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

        std::string partner = WideToUTF8(partnerHandle);
        std::string st = WideToUTF8(state);
        sqlite3_bind_text(stmt, 1, partner.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, st.c_str(), -1, SQLITE_TRANSIENT);

        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return rc == SQLITE_DONE;
    }

    bool DatabaseManager::UpdateSessionState(const std::wstring& partnerHandle, const std::wstring& state)
    {
        const char* sql =
            "UPDATE sessions SET state = ?, last_updated = CURRENT_TIMESTAMP "
            "WHERE partner_handle = ?;";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

        std::string st = WideToUTF8(state);
        std::string partner = WideToUTF8(partnerHandle);
        sqlite3_bind_text(stmt, 1, st.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, partner.c_str(), -1, SQLITE_TRANSIENT);

        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return rc == SQLITE_DONE;
    }

    bool DatabaseManager::SaveSessionKeys(const std::wstring& partnerHandle,
        const std::vector<uint8_t>& privateKey,
        const std::vector<uint8_t>& publicKey)
    {
        const char* sql =
            "UPDATE sessions SET my_private_key = ?, my_public_key = ?, last_updated = CURRENT_TIMESTAMP "
            "WHERE partner_handle = ?;";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

        sqlite3_bind_blob(stmt, 1, privateKey.data(), static_cast<int>(privateKey.size()), SQLITE_TRANSIENT);
        sqlite3_bind_blob(stmt, 2, publicKey.data(), static_cast<int>(publicKey.size()), SQLITE_TRANSIENT);

        std::string partner = WideToUTF8(partnerHandle);
        sqlite3_bind_text(stmt, 3, partner.c_str(), -1, SQLITE_TRANSIENT);

        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return rc == SQLITE_DONE;
    }

    bool DatabaseManager::SaveSharedSecret(const std::wstring& partnerHandle,
        const std::vector<uint8_t>& sharedSecret)
    {
        const char* sql =
            "UPDATE sessions SET shared_secret = ?, last_updated = CURRENT_TIMESTAMP "
            "WHERE partner_handle = ?;";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

        sqlite3_bind_blob(stmt, 1, sharedSecret.data(), static_cast<int>(sharedSecret.size()), SQLITE_TRANSIENT);

        std::string partner = WideToUTF8(partnerHandle);
        sqlite3_bind_text(stmt, 2, partner.c_str(), -1, SQLITE_TRANSIENT);

        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return rc == SQLITE_DONE;
    }

    SessionRecord DatabaseManager::ReadSessionRow(sqlite3_stmt* stmt)
    {
        SessionRecord s;

        const char* partner = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        s.partnerHandle = partner ? UTF8ToWide(partner) : L"";

        const char* state = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        s.state = state ? UTF8ToWide(state) : L"";

        auto readBlob = [&](int col) -> std::vector<uint8_t>
            {
                const void* data = sqlite3_column_blob(stmt, col);
                int bytes = sqlite3_column_bytes(stmt, col);
                if (!data || bytes <= 0) return {};
                const uint8_t* p = static_cast<const uint8_t*>(data);
                return std::vector<uint8_t>(p, p + bytes);
            };

        s.myPrivateKey = readBlob(2);
        s.myPublicKey = readBlob(3);
        s.sharedSecret = readBlob(4);

        const char* ts = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        s.lastUpdated = ts ? UTF8ToWide(ts) : L"";

        return s;
    }

    SessionRecord DatabaseManager::GetSession(const std::wstring& partnerHandle)
    {
        const char* sql =
            "SELECT partner_handle, state, my_private_key, my_public_key, shared_secret, last_updated "
            "FROM sessions WHERE partner_handle = ?;";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return {};

        std::string partner = WideToUTF8(partnerHandle);
        sqlite3_bind_text(stmt, 1, partner.c_str(), -1, SQLITE_TRANSIENT);

        SessionRecord result;
        if (sqlite3_step(stmt) == SQLITE_ROW)
            result = ReadSessionRow(stmt);

        sqlite3_finalize(stmt);
        return result;
    }

    std::vector<SessionRecord> DatabaseManager::GetAllSessions()
    {
        const char* sql =
            "SELECT partner_handle, state, my_private_key, my_public_key, shared_secret, last_updated "
            "FROM sessions ORDER BY last_updated DESC;";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return {};

        std::vector<SessionRecord> sessions;
        while (sqlite3_step(stmt) == SQLITE_ROW)
            sessions.push_back(ReadSessionRow(stmt));

        sqlite3_finalize(stmt);
        return sessions;
    }

    bool DatabaseManager::DeleteSession(const std::wstring& partnerHandle)
    {
        const char* sql = "DELETE FROM sessions WHERE partner_handle = ?;";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

        std::string partner = WideToUTF8(partnerHandle);
        sqlite3_bind_text(stmt, 1, partner.c_str(), -1, SQLITE_TRANSIENT);

        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return rc == SQLITE_DONE;
    }

    bool DatabaseManager::ClearAllSessions()
    {
        return ExecuteSQL("DELETE FROM sessions;");
    }
}