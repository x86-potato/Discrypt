# SQLite Integration Guide for Discrypt

## Adding SQLite to the Project

### Option 1: Using SQLite Amalgamation (Recommended for simplicity)

1. **Download SQLite**:
   - Go to https://www.sqlite.org/download.html
   - Download "sqlite-amalgamation" ZIP file (e.g., sqlite-amalgamation-3450000.zip)
   - Extract `sqlite3.h` and `sqlite3.c` to your project directory

2. **Add to Visual Studio Project**:
   - Right-click project → Add → Existing Item
   - Add both `sqlite3.h` and `sqlite3.c`
   - Right-click `sqlite3.c` → Properties → Precompiled Headers → "Not Using Precompiled Headers"

3. **Build and Link**:
   - SQLite will be compiled directly into your executable
   - No additional linking required

### Option 2: Using Pre-built SQLite Library

1. **Download SQLite**:
   - Download "Precompiled Binaries" from https://www.sqlite.org/download.html
   - Get `sqlite-dll-win-x64-*.zip` for 64-bit Windows

2. **Add to Project**:
   - Extract `sqlite3.dll`, `sqlite3.def`, `sqlite3.lib`
   - Copy to your project directory
   - Project Properties → Linker → Input → Additional Dependencies: Add `sqlite3.lib`
   - Project Properties → Linker → General → Additional Library Directories: Add path to sqlite3.lib

3. **Deployment**:
   - Copy `sqlite3.dll` to output directory (where Discrypt.exe is located)

## Using DatabaseManager

### 1. Initialize Database

```cpp
#include "DatabaseManager.h"

// In App.xaml.cpp or MainWindow.xaml.cpp
Discrypt::DatabaseManager g_database;

// On startup
std::wstring appDataPath = /* Get AppData path */;
std::wstring dbPath = appDataPath + L"\\Discrypt\\messages.db";
if (!g_database.Initialize(dbPath))
{
    OutputDebugStringW(L"[Discrypt] Failed to initialize database\n");
}

// Optional: Migrate existing file-based messages
if (g_database.MigrateFromFiles())
{
    OutputDebugStringW(L"[Discrypt] Successfully migrated message files to database\n");
}
```

### 2. Save/Load User Handle

```cpp
// Save
g_database.SaveUserHandle(L"@myusername");

// Load
std::wstring handle = g_database.LoadUserHandle();
```

### 3. Save Messages

```cpp
// Save a sent message
g_database.SaveMessage(L"@partner", L"Hello, world!", true);

// Save a received message
g_database.SaveMessage(L"@partner", L"Hi there!", false);
```

### 4. Load Message History

```cpp
std::vector<Discrypt::MessageRecord> messages = g_database.LoadMessages(L"@partner");
for (const auto& msg : messages)
{
    // msg.senderHandle, msg.content, msg.timestamp, msg.isSent
    // Display in UI
}
```

### 5. Get All Conversations

```cpp
std::vector<Discrypt::ConversationRecord> conversations = g_database.GetAllConversations();
for (const auto& conv : conversations)
{
    // Add to NavigationView menu
    mainWindow->AddConversation(winrt::hstring(conv.partnerHandle));
}
```

## Migration from File-Based Storage

The `MigrateFromFiles()` function will:
1. Scan `%TEMP%\Discrypt\` for existing message files
2. Parse each `messages_{username}.txt` file
3. Import all messages into the SQLite database
4. Optionally delete the old .txt files after successful migration

## Database Schema

### users table
- `user_id` (INTEGER PRIMARY KEY AUTOINCREMENT)
- `handle` (TEXT UNIQUE NOT NULL) - Discord @handle
- `created_at` (TIMESTAMP)

### messages table
- `message_id` (INTEGER PRIMARY KEY AUTOINCREMENT)
- `partner_handle` (TEXT NOT NULL) - Who you're chatting with
- `sender_handle` (TEXT NOT NULL) - Who sent this message (you or partner)
- `content` (TEXT NOT NULL) - Decrypted message content
- `is_sent` (BOOLEAN) - True if you sent it, False if received
- `timestamp` (TIMESTAMP DEFAULT CURRENT_TIMESTAMP)

### settings table
- `key` (TEXT PRIMARY KEY)
- `value` (TEXT)
- Used for storing app settings like current user handle

## Updating Existing Code

### Replace in MainWindow.xaml.cpp

**Old**:
```cpp
void MainWindow::SaveMessagesToFile(const std::wstring& user)
{
    // File I/O code...
}

void MainWindow::LoadMessagesFromFile(const std::wstring& user)
{
    // File I/O code...
}
```

**New**:
```cpp
void MainWindow::SaveMessagesToFile(const std::wstring& user)
{
    // Now handled automatically by AddSentMessageToHistory/AddReceivedMessageToHistory
    // which call g_database.SaveMessage()
}

void MainWindow::LoadMessagesFromFile(const std::wstring& user)
{
    std::vector<Discrypt::MessageRecord> records = g_database.LoadMessages(user);
    m_userMessages[user].clear();
    
    for (const auto& record : records)
    {
        MessageData msg;
        msg.timestamp = record.timestamp;
        msg.content = record.content;
        msg.isDecrypted = true;
        msg.isSent = record.isSent;
        m_userMessages[user].push_back(msg);
    }
}
```

### Replace in App.xaml.cpp

**Old**:
```cpp
std::wstring LoadUserHandle()
{
    // File I/O code...
}

void SaveUserHandle(const std::wstring& handle)
{
    // File I/O code...
}
```

**New**:
```cpp
std::wstring LoadUserHandle()
{
    extern Discrypt::DatabaseManager g_database;
    return g_database.LoadUserHandle();
}

void SaveUserHandle(const std::wstring& handle)
{
    extern Discrypt::DatabaseManager g_database;
    g_database.SaveUserHandle(handle);
}
```

## Benefits of SQLite

1. **Structured Data**: Proper relational schema instead of text parsing
2. **Performance**: Indexed queries for fast message retrieval
3. **Scalability**: Handles thousands of messages efficiently
4. **Transactions**: Atomic operations prevent data corruption
5. **Features**: Easy to add search, filtering, pagination
6. **Backup**: Single file to backup/restore all data
7. **Standard**: Well-tested, widely used, extensive documentation

## Next Steps

1. Download and add SQLite to project (choose Option 1 or 2 above)
2. Implement `DatabaseManager.cpp` with SQLite API calls
3. Update `App.xaml.cpp` to use database for user handle
4. Update `MainWindow.xaml.cpp` to use database for messages
5. Test migration from existing .txt files
6. Dynamically populate NavigationView with all conversations from database
