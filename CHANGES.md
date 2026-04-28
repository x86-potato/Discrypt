# Discrypt - Recent Changes

## Latest Fixes (Current Session)

### Critical Bug Fixes:
1. **Fixed heap buffer overrun in Base64Encode** (CryptoManager.cpp line 25-36)
   - Changed from `std::wstring base64(base64Length, L'\0')` to `std::vector<wchar_t> buffer(base64Length)`
   - CryptBinaryToStringW includes null terminator in length, causing write past end of wstring buffer
   - Now properly handles null terminator and constructs string with explicit length

2. **Fixed heap corruption in UTF-8 conversion during encryption** (CryptoManager.cpp EncryptMessage)
   - **Issue**: WideCharToMultiByte called with `-1` for length (includes null terminator)
   - Buffer allocated as `utf8Size - 1` but conversion passed `utf8Size`, writing past buffer end
   - **Fix**: Changed to explicit length conversion without null terminator inclusion
   - Added validation and error handling for empty/failed conversions
   - Now uses `plaintext.length()` instead of `-1` to avoid null terminator issues

3. **Fixed heap corruption in UTF-8 conversion during decryption** (CryptoManager.cpp DecryptMessage)
   - Similar issue with MultiByteToWideChar and wstring sizing
   - **Fix**: Use `vector<wchar_t>` buffer instead of `wstring` with pre-sized allocation
   - Construct final string from buffer with explicit character count
   - Added validation and error handling

4. **Fixed sent message logging to wrong conversation** (App.xaml.cpp)
   - Added `g_partnerHandle` global variable to track current conversation partner
   - Stored partner handle in both HANDSHAKE_INIT and HANDSHAKE_RESPONSE handlers
   - Changed line 303 from `AddSentMessageToHistory(g_userHandle, ...)` to `AddSentMessageToHistory(g_partnerHandle, ...)`
   - Sent messages now appear in the correct recipient's conversation
   - Added debug output to track message logging

5. **Fixed manual decryption username prefix handling** (MainWindow.xaml.cpp DecryptButton_Click)
   - **Issue**: When manually pasting encrypted messages and clicking Decrypt, the `@sender:` prefix wasn't stripped
   - **Symptom**: "cipher too short" error because Base64 decoder received `@sender:data` instead of just `data`
   - **Fix**: Added username prefix parsing logic before calling DecryptMessage()
   - Extracts sender handle between first and second colon: `[ENC]:@sender:data`
   - Reconstructs as `[ENC]:data` (same logic as automatic Alt+Enter decryption)
   - Manual decryption now works correctly

4. **Automatic decryption username prefix handling** (App.xaml.cpp line 240-243)
   - Already correctly implemented: strips `@sender:` prefix from `[ENC]:@sender:data` format
   - Reconstructs as `[ENC]:data` before passing to DecryptMessage()
   - Alt+Enter in-place decryption works correctly

## Username Management Refactoring

### Changes Made:
1. **Removed ContentDialog-based username prompt** that was causing COM circular reference crashes
2. **Added persistent username input field** on the homepage (UserHandleInput TextBox)
3. **Auto-load username** from storage on application startup
4. **Auto-save username** when user types in the field (with @ prefix enforcement)
5. **Added validation guards** to prevent Alt+Enter encryption/handshake operations when username is not set

### Files Modified:
- `MainWindow.xaml` - Added UserHandleInput TextBox to "Your Profile" section on homepage
- `MainWindow.xaml.h` - Added UserHandleInput_TextChanged event handler declaration
- `MainWindow.xaml.cpp` - Implemented UserHandleInput_TextChanged to save username on change
- `App.xaml.h` - Removed PromptForUserHandle() declaration
- `App.xaml.cpp` - Removed PromptForUserHandle() function, added validation guards in HandleAltEnter(), added code to populate UserHandleInput on startup

### How It Works:
1. On application startup, `LoadUserHandle()` reads the saved username from `%TEMP%\Discrypt\user_handle.txt`
2. If found, the username is populated into the `UserHandleInput` field on the homepage
3. When the user types in the field, `UserHandleInput_TextChanged` is triggered
4. The handler ensures the username starts with @, updates the global `g_userHandle`, and saves to storage
5. When Alt+Enter is pressed:
   - Checks if `g_userHandle` is empty before allowing handshake initiation
   - Checks if `g_userHandle` is empty before allowing message encryption
   - Displays debug warnings if username is not set

## Memory Warning Investigation

### Issue:
A memory warning appears in debug mode before encrypted messages are typed out to Discord.

### Potential Causes:
1. **OutputDebugStringW with very long strings** (line 285 in DiscordInterop.cpp)
   - The encrypted message can be very long (Base64-encoded)
   - Debug output of the entire encrypted string may cause buffer issues

2. **COM Initialization Pattern**
   - `CoInitializeEx(nullptr, COINIT_MULTITHREADED)` called in WriteTextBox()
   - May cause issues if COM is already initialized on the calling thread
   - Should check return value and handle RPC_E_CHANGED_MODE

3. **BCrypt Buffer Management** (in CryptoManager.cpp)
   - Vector reallocations during `combined.insert()` operations (lines 346-348)
   - May cause temporary memory spikes during encryption

### Recommended Fixes:
1. **Limit debug output length**:
   ```cpp
   std::wstring debugText = text.length() > 100 ? text.substr(0, 100) + L"..." : text;
   OutputDebugStringW((L"[Discrypt] WriteTextBox: Successfully simulated typing: " + debugText + L"\n").c_str());
   ```

2. **Improve COM initialization**:
   ```cpp
   HRESULT hrCom = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
   bool needsUninit = SUCCEEDED(hrCom);
   // ... do work ...
   if (needsUninit) CoUninitialize();
   ```

3. **Reserve vector capacity**:
   ```cpp
   std::vector<BYTE> combined;
   combined.reserve(iv.size() + ciphertext.size() + tag.size());
   combined.insert(combined.end(), iv.begin(), iv.end());
   // ...
   ```

## SQLite Integration (Planned)

### Current Storage:
- User handle: `%TEMP%\Discrypt\user_handle.txt`
- Messages: `%TEMP%\Discrypt\messages_{username}.txt` (pipe-delimited format)

### Planned SQLite Schema:
```sql
CREATE TABLE users (
    user_id INTEGER PRIMARY KEY AUTOINCREMENT,
    handle TEXT UNIQUE NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE conversations (
    conversation_id INTEGER PRIMARY KEY AUTOINCREMENT,
    user1_id INTEGER NOT NULL,
    user2_id INTEGER NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (user1_id) REFERENCES users(user_id),
    FOREIGN KEY (user2_id) REFERENCES users(user_id),
    UNIQUE (user1_id, user2_id)
);

CREATE TABLE messages (
    message_id INTEGER PRIMARY KEY AUTOINCREMENT,
    conversation_id INTEGER NOT NULL,
    sender_user_id INTEGER NOT NULL,
    content TEXT NOT NULL,
    is_encrypted BOOLEAN DEFAULT 1,
    timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (conversation_id) REFERENCES conversations(conversation_id),
    FOREIGN KEY (sender_user_id) REFERENCES users(user_id)
);

CREATE INDEX idx_messages_conversation ON messages(conversation_id, timestamp);
```

### Benefits:
- Proper relational structure for multi-user conversations
- Better query performance for message history
- Support for future features (search, filtering, export)
- Atomic transactions for data integrity
- No manual file parsing

### Implementation Steps:
1. Add `sqlite3.h` and `sqlite3.c` to project (or link sqlite3.lib)
2. Create `DatabaseManager.h/cpp` with RAII connection wrapper
3. Implement schema creation on first run
4. Create migration function to import existing .txt message files
5. Update `SaveMessagesToFile()` and `LoadMessagesFromFile()` to use database
6. Add `GetAllConversations()` to populate NavigationView menu dynamically

## Testing Checklist:
- [x] Application builds successfully
- [ ] Username persists across application restarts
- [ ] Username validation (@ prefix) works correctly
- [ ] Alt+Enter shows warning when username is empty
- [ ] Handshake initiation blocked when username is empty
- [ ] Message encryption blocked when username is empty
- [ ] Memory warning is resolved or documented
- [ ] SQLite integration (future)

## Notes:
- The ContentDialog approach was fundamentally flawed due to COM circular references when TextBox event handlers captured the dialog reference
- Moving to a persistent UI field eliminates the coroutine complexity and crash issues
- Username is now always visible and editable on the homepage, improving UX
- Validation guards prevent cryptographic operations without proper identity setup
