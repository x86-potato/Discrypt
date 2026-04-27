# Discrypt - Refactored Code Structure

## Overview
The codebase has been reorganized into separate modules for better maintainability and separation of concerns.

## File Structure

### Core Application Files
- **App.xaml.cpp** - Application lifecycle and Alt+Enter handler logic
- **MainWindow.xaml.cpp** - GUI event handlers

### Modular Components

#### 1. CryptoManager (CryptoManager.h/cpp)
**Purpose:** Handles all cryptographic operations

**Key Classes:**
- `EncryptionSession` - Struct holding session state (keys, shared secret, handshake status)
- `CryptoManager` - Static class with crypto operations

**Functions:**
- `Base64Encode()` / `Base64Decode()` - Convert binary data for transmission
- `GenerateDHKeyPair()` - Generate ECDH P-256 key pair
- `DeriveSharedSecret()` - Compute shared secret from partner's public key
- `GetPublicKeyHex()` / `GetSharedSecretHex()` - Display keys in GUI
- `CleanupSession()` - Clean up BCrypt resources

**Dependencies:**
- `bcrypt.lib` - Windows cryptography
- `crypt32.lib` - Base64 encoding/decoding

---

#### 2. DiscordInterop (DiscordInterop.h/cpp)
**Purpose:** Manages all Discord window interactions

**Key Class:**
- `DiscordInterop` - Static class for Discord communication

**Functions:**
- `GetDiscordWindow()` - Find and cache Discord window handle
- `InvalidateCache()` - Clear cached window handle
- `ReadTextBox()` - Read current text from Discord input box using UI Automation
- `WriteTextBox()` - Write text to Discord input box with keyboard simulation

**Dependencies:**
- UI Automation COM API
- Windows GDI (EnumWindows, GetWindowText)

**Implementation Details:**
- Searches for windows with "Discord" in title
- Caches window handle for performance
- Uses UI Automation to find last text box (input box)
- Simulates Ctrl+A + typing to write text

---

#### 3. KeyboardHook (KeyboardHook.h/cpp)
**Purpose:** Global keyboard monitoring and input simulation

**Key Class:**
- `KeyboardHook` - Static class managing keyboard hooks

**Functions:**
- `Install(callback)` - Install WH_KEYBOARD_LL hook with Alt+Enter callback
- `Uninstall()` - Remove keyboard hook
- `SimulateKeyPress()` - Simulate virtual key press (e.g., VK_RETURN)

**Implementation Details:**
- Detects Alt+Enter in Discord window
- Ignores injected keypresses (LLKHF_INJECTED) to avoid loops
- Blocks Alt+Enter by returning 1 from hook callback
- Calls registered callback function when Alt+Enter detected

---

## Data Flow

### Startup:
```
App::OnLaunched()
  → KeyboardHook::Install(HandleAltEnter)
  → MainWindow created and displayed
```

### Alt+Enter Press:
```
KeyboardProc (global hook)
  → Detects Alt+Enter in Discord
  → Calls HandleAltEnter() callback
    → DiscordInterop::ReadTextBox()
    → Check message type (HANDSHAKE_INIT, HANDSHAKE_RESPONSE, etc.)
    → CryptoManager::GenerateDHKeyPair() (if needed)
    → CryptoManager::Base64Encode() / Base64Decode()
    → CryptoManager::DeriveSharedSecret() (if handshake message)
    → DiscordInterop::WriteTextBox()
    → KeyboardHook::SimulateKeyPress(VK_RETURN)
  → Returns 1 to block original Alt+Enter
```

### GUI Refresh:
```
RefreshButton_Click()
  → CryptoManager::GetPublicKeyHex(g_session)
  → CryptoManager::GetSharedSecretHex(g_session)
  → Update TextBoxes in GUI
```

### Shutdown:
```
App::~App()
  → KeyboardHook::Uninstall()
  → CryptoManager::CleanupSession(g_session)
    → BCryptDestroyKey()
    → BCryptDestroySecret()
    → BCryptCloseAlgorithmProvider()
```

---

## Global State

**Defined in App.xaml.cpp:**
```cpp
std::wstring g_encryptionKey;              // Password from GUI
Discrypt::EncryptionSession g_session;     // Crypto session state
```

**Accessed by:**
- `MainWindow.xaml.cpp` - Updates `g_encryptionKey`, reads session for GUI
- `HandleAltEnter()` - Reads/writes session for handshake logic

---

## Namespace Organization

- **`Discrypt`** - C++ classes (CryptoManager, DiscordInterop, KeyboardHook)
- **`winrt::Discrypt::implementation`** - WinRT/WinUI app code (App, MainWindow)

---

## Benefits of New Structure

### 1. **Separation of Concerns**
- Cryptography logic isolated in CryptoManager
- Discord interaction isolated in DiscordInterop
- Keyboard handling isolated in KeyboardHook
- App.xaml.cpp only contains app lifecycle and business logic

### 2. **Reusability**
- Each module can be tested independently
- Classes can be reused in other projects
- Clear interfaces with static methods

### 3. **Maintainability**
- Easier to find and fix bugs
- Changes to crypto don't affect Discord code
- Clear responsibility for each file

### 4. **Readability**
- App.xaml.cpp went from ~800 lines to ~150 lines
- Each file has a single, clear purpose
- Function names clearly indicate what they do

---

## Adding New Features

### To add message encryption:
1. Add `EncryptMessage()` / `DecryptMessage()` to **CryptoManager.cpp**
2. Call from `HandleAltEnter()` in **App.xaml.cpp**

### To add incoming message monitoring:
1. Add `MonitorMessages()` to **DiscordInterop.cpp**
2. Start monitoring thread in **App::OnLaunched()**

### To add different hotkeys:
1. Modify `KeyboardProc()` in **KeyboardHook.cpp**
2. Add callback parameters for different keys

---

## Testing Strategy

Each module can be tested independently:

**CryptoManager:**
- Test key generation produces valid ECDH keys
- Test Base64 encode/decode round-trip
- Test shared secret derivation produces same value for both parties

**DiscordInterop:**
- Test window finding with Discord running/closed
- Test read/write text box operations
- Test cache invalidation

**KeyboardHook:**
- Test Alt+Enter detection
- Test callback invocation
- Test injected key filtering

---

## Build Configuration

Make sure all new .cpp files are included in your project:
- CryptoManager.cpp
- DiscordInterop.cpp  
- KeyboardHook.cpp

Required libraries (linked automatically via #pragma comment):
- bcrypt.lib
- crypt32.lib

---

## Next Steps

1. ✅ Code refactored into modules
2. ⏳ Test handshake functionality
3. ⏳ Implement AES message encryption
4. ⏳ Add incoming message monitoring
5. ⏳ Add message authentication (HMAC/GCM)
