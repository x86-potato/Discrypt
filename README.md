# 🔒 Discrypt

**Discrypt** is a proof-of-concept application that provides true End-to-End Encryption (E2EE) for Discord direct messages. It works by injecting a secure JavaScript payload directly into the Discord desktop client via the Chrome DevTools Protocol (CDP), intercepting outgoing messages to encrypt them locally, and decrypting incoming secure messages on the fly.

Because Discrypt operates entirely locally via an injected script and a local C++ WebSocket server, Discord's servers only ever see Base64 ciphertext.

---

## ⚠️ CRITICAL SECURITY WARNING

**Read this before installing or using Discrypt.**

To allow Discrypt to communicate with Discord, you are required to enable Discord's remote debugging port and unlock the Developer Tools. **Doing this inherently weakens the security of your Discord client.**

1. **Remote Debugging (`--remote-debugging-port=9222`)**: This opens a local network port that allows *any* program running on your computer to take complete control over your Discord client, read your messages, or send messages on your behalf.
2. **Developer Tools (`DANGEROUS_ENABLE_DEVTOOLS...`)**: Discord deliberately disables DevTools to prevent self-XSS (Cross-Site Scripting) attacks where malicious actors trick users into pasting code that steals their login tokens. By enabling this, you bypass those protections.

**Do not use this setup on a shared computer, and ensure your PC is free of malware before enabling these flags. Use at your own risk.**

---

## ⚙️ Setup Instructions

To allow Discrypt to inject its encryption engine, you must configure Discord to allow external debugging. 

### Step 1: Enable Developer Tools in `settings.json`
You need to manually edit Discord's core settings file to allow DevTools to run.

1. Close Discord completely (ensure it is not running in the system tray).
2. Press `Win + R`, type `%appdata%\discord`, and press Enter.
3. Open the `settings.json` file in a text editor (like Notepad or VS Code).
4. Add the following line to the JSON object (make sure to add a comma to the previous line if necessary):
   ```json
   "DANGEROUS_ENABLE_DEVTOOLS_ONLY_ENABLE_IF_YOU_KNOW_WHAT_YOURE_DOING": true
   ```
5. Save and close the file.

### Step 2: Add Launch Options to Discord
You must launch Discord with specific Chromium flags to open the debugging port.

1. Find your Discord desktop shortcut (e.g., on your Desktop or in the Start Menu).
2. Right-click the shortcut and select **Properties**.
3. In the **Shortcut** tab, locate the **Target** field.
4. Go to the very end of the text in the Target box, add a single space, and paste the following:
   ```text
   --remote-debugging-port=9222 --remote-allow-origins=*
   ```
   *(Example: `C:\Users\Username\AppData\Local\Discord\Update.exe --processStart Discord.exe --process-start-args "--remote-debugging-port=9222 --remote-allow-origins=*"`)*
5. Click **Apply** and **OK**.

---

## 🚀 How to Use Discrypt

Once the setup is complete, you can start using Discrypt to send encrypted messages.

### 1. Launching the App
1. Launch Discord using your newly modified shortcut.
2. Run the **Discrypt executable**.
3. Discrypt will automatically locate Discord, bypass the security locks, and inject the secure JavaScript payload. The console will display: `[Discrypt] ✅ Connected to local C++ server.`

### 2. Establishing a Secure Session (Handshake)
Before you can chat securely, you and your partner must exchange encryption keys.

1. Open a Direct Message with your friend.
2. Type the following command into the chat box:
   ```text
   !handshake
   ```
3. **DO NOT press Enter.** Instead, press `Alt + Enter`. Discrypt will intercept this command and generate a secure `[HANDSHAKE_INIT]:` message containing your public key.
4. Your partner must see this message, type `!accept` into their chat box, and press `Alt + Enter`.
5. Once accepted, both clients will generate a shared secret.

### 3. Sending Encrypted Messages
To send a message that Discord cannot read:

1. Type your message normally into the chat box (e.g., *Hello, this is a secret!*).
2. Press `Alt + Enter` instead of standard Enter.
3. Discrypt will intercept the text, route it to your local C++ server for encryption, and paste the resulting `[ENC]:` Base64 ciphertext into Discord.
4. When your partner receives the message, their Discrypt client will automatically detect the `[ENC]:` tag, decrypt the payload using the shared secret, and display the readable text in the Discord UI marked with a 🔒 or 🔓 emoji.

---

## 🧠 How it Works (For Developers)

1. **CDP Injection:** Discrypt connects to Discord's embedded Chromium instance via `localhost:9222`. It spoofs the `devtools://devtools` origin header to bypass security restrictions and injects a custom JavaScript payload.
2. **DOM Hooking:** The JavaScript uses a `MutationObserver` to watch the DOM for new messages in real-time, instantly catching incoming encrypted texts.
3. **IPC WebSocket:** The JS payload communicates over a local WebSocket (`ws://127.0.0.1:9090`) to the C++ backend.
4. **Local Cryptography:** All shared secret generation and AES encryption/decryption happens securely in the compiled C++ backend, keeping keys out of the browser's memory space.

---

**Disclaimer:** Discrypt is an independent project and is not affiliated with, endorsed by, or associated with Discord Inc. Modifying the Discord client violates Discord's Terms of Service. This tool is provided for educational and research purposes only.
