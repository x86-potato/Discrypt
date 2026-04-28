#pragma once
#include <string>
#include <vector>
#include <memory>

// Forward declare sqlite3 types to avoid including sqlite3.h here
struct sqlite3;

namespace Discrypt
{
    struct MessageRecord
    {
        int messageId;
        std::wstring senderHandle;
        std::wstring content;
        std::wstring timestamp;
        bool isSent;
    };

    struct ConversationRecord
    {
        std::wstring partnerHandle;
        std::wstring lastMessage;
        std::wstring lastTimestamp;
        int unreadCount;
    };

    class DatabaseManager
    {
    public:
        DatabaseManager();
        ~DatabaseManager();

        // Initialize database (create tables if needed)
        bool Initialize(const std::wstring& dbPath);

        // User operations
        bool SaveUserHandle(const std::wstring& handle);
        std::wstring LoadUserHandle();

        // Message operations
        bool SaveMessage(const std::wstring& partnerHandle, const std::wstring& content, bool isSent);
        std::vector<MessageRecord> LoadMessages(const std::wstring& partnerHandle);
        bool ClearAllMessages();
        bool ClearAllData(); // Clear all user data (messages, conversations, user handle)

        // Conversation operations
        std::vector<ConversationRecord> GetAllConversations();
        bool DeleteConversation(const std::wstring& partnerHandle);

        // Migration from file-based storage
        bool MigrateFromFiles();

    private:
        sqlite3* m_db;
        std::wstring m_dbPath;

        // Helper functions
        bool CreateTables();
        bool ExecuteSQL(const std::string& sql);
        int GetOrCreateUserId(const std::wstring& handle);
        std::string WideToUTF8(const std::wstring& wstr);
        std::wstring UTF8ToWide(const std::string& str);
    };
}
