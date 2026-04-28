#include "pch.h"
#include "DatabaseManager.h"
#include "sqlite3.h"
#include <fstream>
#include <filesystem>
#include <sstream>

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

    bool DatabaseManager::Initialize(const std::wstring& dbPath)
    {
        m_dbPath = dbPath;
        std::string utf8Path = WideToUTF8(dbPath);

        OutputDebugStringW(L"[DatabaseManager] Attempting to open database at: ");
        OutputDebugStringW(dbPath.c_str());
        OutputDebugStringW(L"\n");

        int rc = sqlite3_open(utf8Path.c_str(), &m_db);
        if (rc != SQLITE_OK)
        {
            std::string errorMsg = sqlite3_errmsg(m_db);
            OutputDebugStringA("[DatabaseManager] SQLite open failed: ");
            OutputDebugStringA(errorMsg.c_str());
            OutputDebugStringA("\n");
            OutputDebugStringW(L"[DatabaseManager] SQLite error code: ");
            OutputDebugStringW(std::to_wstring(rc).c_str());
            OutputDebugStringW(L"\n");
            return false;
        }

        OutputDebugStringW(L"[DatabaseManager] SQLite database opened successfully\n");

        bool tablesCreated = CreateTables();
        if (!tablesCreated)
        {
            OutputDebugStringW(L"[DatabaseManager] Failed to create tables\n");
        }
        else
        {
            OutputDebugStringW(L"[DatabaseManager] Tables created successfully\n");
        }

        return tablesCreated;
    }

    bool DatabaseManager::CreateTables()
    {
        const char* sql =
            // 1. SESSIONS TABLE: The core state machine for the handshake
            "CREATE TABLE IF NOT EXISTS sessions ("
            "    partner_handle TEXT PRIMARY KEY,"
            "    state TEXT NOT NULL,"           // 'PENDING_INITIATOR', 'PENDING_RECEIVER', 'SECURED'
            "    my_private_key BLOB,"           // Generated when handshake starts
            "    my_public_key BLOB,"            // Sent to partner
            "    shared_secret BLOB,"            // The final AES key (Null until SECURED)
            "    last_updated DATETIME DEFAULT CURRENT_TIMESTAMP"
            ");"

            // 2. SETTINGS TABLE: App configuration (Your handle, auto-inject, etc.)
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
            OutputDebugStringA("[DatabaseManager] SQL execution failed: ");
            if (errMsg)
            {
                OutputDebugStringA(errMsg);
                OutputDebugStringA("\n");
                sqlite3_free(errMsg);
            }
            else
            {
                OutputDebugStringA("(no error message)\n");
            }
            OutputDebugStringW(L"[DatabaseManager] SQLite error code: ");
            OutputDebugStringW(std::to_wstring(rc).c_str());
            OutputDebugStringW(L"\n");
            return false;
        }

        return true;
    }

    bool DatabaseManager::SaveUserHandle(const std::wstring& handle)
    {
        std::string sql = "INSERT OR REPLACE INTO settings (key, value) VALUES ('user_handle', ?);";
        
        sqlite3_stmt* stmt;
        int rc = sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK)
        {
            return false;
        }

        std::string utf8Handle = WideToUTF8(handle);
        sqlite3_bind_text(stmt, 1, utf8Handle.c_str(), -1, SQLITE_TRANSIENT);

        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        return rc == SQLITE_DONE;
    }

    std::wstring DatabaseManager::LoadUserHandle()
    {
        std::string sql = "SELECT value FROM settings WHERE key = 'user_handle';";
        
        sqlite3_stmt* stmt;
        int rc = sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK)
        {
            return L"";
        }

        std::wstring result;
        if (sqlite3_step(stmt) == SQLITE_ROW)
        {
            const char* value = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (value)
            {
                result = UTF8ToWide(value);
            }
        }

        sqlite3_finalize(stmt);
        return result;
    }

    bool DatabaseManager::SaveMessage(const std::wstring& partnerHandle, const std::wstring& content, bool isSent)
    {
        std::string sql = "INSERT INTO messages (partner_handle, sender_handle, content, is_sent) VALUES (?, '', ?, ?);";
        
        sqlite3_stmt* stmt;
        int rc = sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK)
        {
            return false;
        }

        std::string utf8Partner = WideToUTF8(partnerHandle);
        std::string utf8Content = WideToUTF8(content);

        sqlite3_bind_text(stmt, 1, utf8Partner.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, utf8Content.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, isSent ? 1 : 0);

        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        return rc == SQLITE_DONE;
    }

    std::vector<MessageRecord> DatabaseManager::LoadMessages(const std::wstring& partnerHandle)
    {
        std::vector<MessageRecord> messages;
        std::string sql = "SELECT message_id, sender_handle, content, timestamp, is_sent FROM messages WHERE partner_handle = ? ORDER BY timestamp ASC;";
        
        sqlite3_stmt* stmt;
        int rc = sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK)
        {
            return messages;
        }

        std::string utf8Partner = WideToUTF8(partnerHandle);
        sqlite3_bind_text(stmt, 1, utf8Partner.c_str(), -1, SQLITE_TRANSIENT);

        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            MessageRecord msg;
            msg.messageId = sqlite3_column_int(stmt, 0);
            
            const char* sender = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            msg.senderHandle = sender ? UTF8ToWide(sender) : L"";
            
            const char* content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            msg.content = content ? UTF8ToWide(content) : L"";
            
            const char* timestamp = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            msg.timestamp = timestamp ? UTF8ToWide(timestamp) : L"";
            
            msg.isSent = sqlite3_column_int(stmt, 4) != 0;

            messages.push_back(msg);
        }

        sqlite3_finalize(stmt);
        return messages;
    }

    bool DatabaseManager::ClearAllMessages()
    {
        return ExecuteSQL("DELETE FROM messages;");
    }

    bool DatabaseManager::ClearAllData()
    {
        // Clear all tables
        bool success = true;
        success &= ExecuteSQL("DELETE FROM messages;");
        success &= ExecuteSQL("DELETE FROM users;");
        success &= ExecuteSQL("DELETE FROM user_settings;");

        if (success)
        {
            OutputDebugStringW(L"[DatabaseManager] All data cleared successfully\n");
        }
        else
        {
            OutputDebugStringW(L"[DatabaseManager] Failed to clear all data\n");
        }

        return success;
    }

    std::vector<ConversationRecord> DatabaseManager::GetAllConversations()
    {
        std::vector<ConversationRecord> conversations;
        std::string sql = 
            "SELECT partner_handle, "
            "       (SELECT content FROM messages m2 WHERE m2.partner_handle = m.partner_handle ORDER BY timestamp DESC LIMIT 1) as last_message, "
            "       (SELECT timestamp FROM messages m2 WHERE m2.partner_handle = m.partner_handle ORDER BY timestamp DESC LIMIT 1) as last_timestamp, "
            "       0 as unread_count "
            "FROM messages m "
            "GROUP BY partner_handle "
            "ORDER BY last_timestamp DESC;";
        
        sqlite3_stmt* stmt;
        int rc = sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK)
        {
            return conversations;
        }

        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            ConversationRecord conv;
            
            const char* partner = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            conv.partnerHandle = partner ? UTF8ToWide(partner) : L"";
            
            const char* lastMsg = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            conv.lastMessage = lastMsg ? UTF8ToWide(lastMsg) : L"";
            
            const char* lastTime = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            conv.lastTimestamp = lastTime ? UTF8ToWide(lastTime) : L"";
            
            conv.unreadCount = sqlite3_column_int(stmt, 3);

            conversations.push_back(conv);
        }

        sqlite3_finalize(stmt);
        return conversations;
    }

    bool DatabaseManager::DeleteConversation(const std::wstring& partnerHandle)
    {
        std::string sql = "DELETE FROM messages WHERE partner_handle = ?;";
        
        sqlite3_stmt* stmt;
        int rc = sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK)
        {
            return false;
        }

        std::string utf8Partner = WideToUTF8(partnerHandle);
        sqlite3_bind_text(stmt, 1, utf8Partner.c_str(), -1, SQLITE_TRANSIENT);

        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        return rc == SQLITE_DONE;
    }

    bool DatabaseManager::MigrateFromFiles()
    {
        WCHAR tempPath[MAX_PATH];
        if (SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, tempPath) != S_OK)
        {
            return false;
        }

        std::wstring basePath = std::wstring(tempPath) + L"\\Discrypt";
        
        if (!std::filesystem::exists(basePath))
        {
            return true; // No files to migrate
        }

        // Check for user_handle.txt
        std::wstring handleFile = basePath + L"\\user_handle.txt";
        if (std::filesystem::exists(handleFile))
        {
            std::wifstream file(handleFile);
            if (file.is_open())
            {
                std::wstring handle;
                std::getline(file, handle);
                file.close();
                
                if (!handle.empty())
                {
                    SaveUserHandle(handle);
                }
            }
        }

        return true;
    }

    int DatabaseManager::GetOrCreateUserId(const std::wstring& handle)
    {
        std::string utf8Handle = WideToUTF8(handle);
        
        // Try to find existing user
        std::string selectSql = "SELECT user_id FROM users WHERE handle = ?;";
        sqlite3_stmt* stmt;
        int rc = sqlite3_prepare_v2(m_db, selectSql.c_str(), -1, &stmt, nullptr);
        if (rc == SQLITE_OK)
        {
            sqlite3_bind_text(stmt, 1, utf8Handle.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) == SQLITE_ROW)
            {
                int userId = sqlite3_column_int(stmt, 0);
                sqlite3_finalize(stmt);
                return userId;
            }
            sqlite3_finalize(stmt);
        }

        // Create new user
        std::string insertSql = "INSERT INTO users (handle) VALUES (?);";
        rc = sqlite3_prepare_v2(m_db, insertSql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK)
        {
            return -1;
        }

        sqlite3_bind_text(stmt, 1, utf8Handle.c_str(), -1, SQLITE_TRANSIENT);
        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (rc == SQLITE_DONE)
        {
            return static_cast<int>(sqlite3_last_insert_rowid(m_db));
        }

        return -1;
    }

    std::string DatabaseManager::WideToUTF8(const std::wstring& wstr)
    {
        if (wstr.empty())
        {
            return std::string();
        }

        int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.length()), nullptr, 0, nullptr, nullptr);
        std::string result(size, 0);
        WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.length()), &result[0], size, nullptr, nullptr);

        return result;
    }

    std::wstring DatabaseManager::UTF8ToWide(const std::string& str)
    {
        if (str.empty())
        {
            return std::wstring();
        }

        int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.length()), nullptr, 0);
        std::wstring result(size, 0);
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.length()), &result[0], size);

        return result;
    }
}
