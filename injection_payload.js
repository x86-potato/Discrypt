(() => {
    const DISCRYPT_VERSION = '6';
    console.log(`[Discrypt] 🔄 Injecting version ${DISCRYPT_VERSION}`);

    // ==========================================
    // 1. PREVENT DOUBLE-INJECTION GHOSTS
    // ==========================================
    if (window.__discrypt_cleanup) {
        console.log('[Discrypt] Destroying previous ghost hooks...');
        window.__discrypt_cleanup();
    }

    const IPC_PORT = 9090;
    const ws = new WebSocket('ws://127.0.0.1:9090');
    const pendingRequests = new Map();

    // Carry over the ID counter if we are re-injecting
    let messageIdCounter = window.__discrypt_msgId || 0;

    let currentMessageObserver = null;
    let titleObserver = null;

    // ==========================================
    // 2. WEBSOCKET HANDLERS
    // ==========================================
    ws.onerror = (err) => console.error('[Discrypt] ❌ WebSocket Error:', err);
    ws.onclose = (event) => console.warn('[Discrypt] ⚠️ WebSocket Closed:', event.reason);

    ws.onopen = () => {
        console.log('[Discrypt] ✅ Connected to local C++ server.');
        ws.send(JSON.stringify({ type: "DEBUG_PING", text: "JS is alive" }));
    };

    ws.onmessage = (event) => {
        console.log('[Discrypt] 📥 RAW DATA RECEIVED:', event.data);
        try {
            const data = JSON.parse(event.data);

            if (data.type === "SEND_DISCORD_MESSAGE") {
                console.log('[Discrypt] 📋 Copying response to clipboard:', data.text);
                // Zero DOM interaction — just write to clipboard.
                // User presses Ctrl+V to paste and Enter to send.
                navigator.clipboard.writeText(data.text).catch(err => {
                    console.error('[Discrypt] ❌ Clipboard write failed:', err);
                });
            }
            // ... (rest of your existing logic)
        } catch (e) {
            console.error('[Discrypt] ❌ Parse Error:', e);
        }
    };

    // ==========================================
    // 3. CHAT SWITCH EVENT
    // ==========================================
    const sendChatSwitchEvent = () => {
        const rawTitle = document.title;
        const handle = rawTitle.split(' - ')[0];

        const eventPayload = JSON.stringify({
            type: "CHAT_SWITCH",
            handle: handle
        });

        if (ws.readyState === WebSocket.OPEN) {
            ws.send(eventPayload);
        } else {
            ws.addEventListener('open', () => ws.send(eventPayload), { once: true });
        }
    };

    // ==========================================
    // 4. MESSAGE OBSERVER
    // ==========================================
    function modifyNode(node) {
        if (node.nodeType !== 1) return;

        const contents = node.querySelectorAll?.('[class*="messageContent"]');
        const targetNodes = contents?.length ? Array.from(contents) :
                            (node.className?.includes?.('messageContent') ? [node] : []);

        targetNodes.forEach(c => {
            const originalText = c.innerText;

            if ((originalText.includes('[HANDSHAKE') || originalText.startsWith('[ENC]:')) && !c.dataset.ipcProcessed) {
                c.dataset.ipcProcessed = "true";
                const id = ++messageIdCounter;
                pendingRequests.set(id, c);

                c.innerText = "🔒 Processing secure payload...";

                const sendPayload = () => {
                    ws.send(JSON.stringify({
                        type: "MESSAGE",
                        id: id,
                        text: originalText
                    }));
                };

                if (ws.readyState === WebSocket.OPEN) {
                    sendPayload();
                } else {
                    ws.addEventListener('open', sendPayload, { once: true });
                }
            }
        });
    }

    function hookCurrentChat() {
        if (currentMessageObserver) currentMessageObserver.disconnect();

        const container = document.querySelector('[class*="messagesWrapper"]') ||
                          document.querySelector('[class*="chatContent"]');

        if (container) {
            modifyNode(container);

            currentMessageObserver = new MutationObserver(mutations => {
                for (const mutation of mutations) {
                    for (const node of mutation.addedNodes) {
                        modifyNode(node);
                    }
                }
            });

            currentMessageObserver.observe(container, { childList: true, subtree: true });
        }
    }

    hookCurrentChat();
    sendChatSwitchEvent();

    const titleElement = document.querySelector('title');
    if (titleElement) {
        titleObserver = new MutationObserver(() => {
            setTimeout(() => {
                hookCurrentChat();
                sendChatSwitchEvent();
            }, 500);
        });
        titleObserver.observe(titleElement, { childList: true });
    }

    // ==========================================
    // 5. COMMAND INTERCEPTION (Alt + Enter)
    // ==========================================
    const handleCommandInput = (e) => {
        if (e.altKey && e.key === 'Enter') {
            const chatBox = document.querySelector('[class*="slateTextArea"]');

            if (chatBox && chatBox.contains(e.target)) {
                const currentText = chatBox.innerText.trim();

                if (currentText === '!handshake' || currentText === '!reply') {
                    e.preventDefault();
                    e.stopPropagation();

                    console.log(`[Discrypt] v${DISCRYPT_VERSION} ✅ Alt+Enter intercepted — command: "${currentText}" @ ${new Date().toISOString()}`);

                    const eventType = (currentText === '!handshake') ? "INIT_HANDSHAKE" : "ACCEPT_HANDSHAKE";

                    if (ws.readyState === WebSocket.OPEN) {
                        ws.send(JSON.stringify({ type: eventType }));
                    } else {
                        console.warn('[Discrypt] WebSocket not ready. Command dropped.');
                    }

                    // Select all text in the box — selectAll only moves the cursor,
                    // it never writes to any text node so Slate's model stays intact.
                    // When the user presses Ctrl+V, Discord's own paste handler will
                    // replace the selection with the clipboard content cleanly.
                    chatBox.focus();
                    document.execCommand('selectAll', false, null);
                }
            }
        }
    };

    document.addEventListener('keydown', handleCommandInput, true);

    // ==========================================
    // 6. CLEANUP
    // ==========================================
    window.__discrypt_cleanup = () => {
        if (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING) {
            ws.close();
        }
        if (currentMessageObserver) currentMessageObserver.disconnect();
        if (titleObserver) titleObserver.disconnect();
        document.removeEventListener('keydown', handleCommandInput, true);
        window.__discrypt_msgId = messageIdCounter;
    };

    return "Success: IPC payload injected with Slate.js command interception.";
})();