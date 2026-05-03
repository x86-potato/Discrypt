# 🔒 Discrypt

**Discrypt** is a proof-of-concept application that brings true, local End-to-End Encryption (E2EE) to Discord direct messages. 

### 🎯 Project Goal
The primary goal of Discrypt is to ensure that your private conversations remain strictly between you and your recipient. By handling all key generation, exchange, and encryption entirely on your local machine before a message is ever sent, Discord's servers—and anyone who might compromise them—only ever see unreadable, Base64 ciphertext. 

This project aims to demonstrate how native application injection and local cryptography can be used to overlay privacy features onto existing, non-E2EE chat platforms without relying on third-party cloud services for key management.

### 📸 Discrypt in Action
#### Client view:
<img width="794" height="448" alt="image" src="https://github.com/user-attachments/assets/8e18aab2-1670-491a-a24b-09734a1495e6" />

#### Discord server view:

<img width="794" height="580" alt="image" src="https://github.com/user-attachments/assets/b9af69b0-e4c3-43b5-a3cd-cd2849a67409" />

---

## 🔐 Cryptographic Architecture
Discrypt relies on the robust Windows Cryptography API: Next Generation (CNG) to handle all secure operations. Keys never leave the local C++ process memory.

* **Key Exchange:** Elliptic Curve Diffie-Hellman (**ECDH**) using the **NIST P-256** curve.
* **Key Derivation:** **SHA-256** is used as the Key Derivation Function (KDF) to generate the final symmetric key from the ECDH shared secret.
* **Message Encryption:** **AES-256-GCM** (Galois/Counter Mode). This provides both confidentiality and authenticated encryption, meaning messages cannot be tampered with in transit without the decryption failing.
* **Encoding:** Standard Base64 for safe transport through Discord's text infrastructure.

---

## ⚠️ Security Notice & Threat Model

To allow Discrypt to communicate with Discord, you must enable Discord's remote debugging port (`9222`) and unlock the Developer Tools. **It is important to understand what this means for your security:**

* **The Reality:** A random website in your normal web browser *cannot* simply hack your Discord account because you opened this port. Modern web browsers use strict CORS (Cross-Origin Resource Sharing) policies and sandboxing that prevent websites from arbitrarily interacting with local debugging ports.
* **The Risk:** Opening this port *does* allow other local programs running on your computer to take control of your Discord client. If your computer is infected with malware, that malware could easily read your messages or steal your session token. However, if malicious software is already executing locally on your machine, your system is fully compromised anyway. 

**Bottom Line:** This setup removes Discord's internal protections against local process tampering. Do not use this setup on a public or shared computer, and ensure your PC is free of malicious software. Use at your own risk.

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
   --process-start-args "--remote-debugging-port=9222 --remote-allow-origins=* --remote-debugging-address=127.0.0.1"
   ```
   *(Example: `C:\Users\Username\AppData\Local\Discord\Update.exe --processStart Discord.exe --process-start-args "--remote-debugging-port=9222 --remote-allow-origins=* --remote-debugging-address=127.0.0.1"`)*
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
5. Once accepted, both clients will silently generate the shared secret.

### 3. Sending Encrypted Messages
To send a message that Discord cannot read:

1. Type your message normally into the chat box (e.g., *Hello, this is a secret!*).
2. Press `Alt + Enter` instead of standard Enter.
3. Discrypt will intercept the text, route it to your local C++ server for AES-256-GCM encryption, and paste the resulting `[ENC]:` Base64 ciphertext into Discord.
4. When your partner receives the message, their Discrypt client will automatically detect the `[ENC]:` tag, decrypt the payload using the shared secret, and display the readable text natively in the Discord UI.

---

## 🧠 How it Works (For Developers)

1. **CDP Injection:** Discrypt connects to Discord's embedded Chromium instance via `localhost:9222`. It spoofs the `devtools://devtools` origin header to bypass security restrictions and injects a custom JavaScript payload.
2. **DOM Hooking:** The JavaScript uses a `MutationObserver` to watch the DOM for new messages in real-time, instantly catching incoming encrypted texts.
3. **IPC WebSocket:** The JS payload communicates over a local WebSocket (`ws://127.0.0.1:9090`) to the C++ backend.
4. **Local Cryptography:** All shared secret generation and AES encryption/decryption happens securely in the compiled C++ backend via the Windows Cryptography API (CNG), keeping private keys completely isolated from the browser's memory space.

---

**Disclaimer:** Discrypt is an independent project and is not affiliated with, endorsed by, or associated with Discord Inc. Modifying the Discord client violates Discord's Terms of Service. This tool is provided for educational and research purposes only.
