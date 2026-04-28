(() => {
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

    ws.onmessage = (event) => {
        const data = JSON.parse(event.data);
        if (data.id && pendingRequests.has(data.id)) {
            const targetNode = pendingRequests.get(data.id);
            targetNode.innerText = data.text; 
            pendingRequests.delete(data.id);
        }
    };

    ws.onopen = () => console.log('[Discrypt] Connected to local C++ server.');

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

    function modifyNode(node) {
        if (node.nodeType !== 1) return;
        
        const contents = node.querySelectorAll?.('[class*="messageContent"]');
        const targetNodes = contents?.length ? Array.from(contents) : 
                            (node.className?.includes?.('messageContent') ? [node] : []);

        targetNodes.forEach(c => {
            const originalText = c.innerText;
            
            if (originalText.toLowerCase().startsWith('test') && !c.dataset.ipcProcessed) {
                c.dataset.ipcProcessed = "true"; 
                const id = ++messageIdCounter;
                pendingRequests.set(id, c);
                
                c.innerText = "⏳ Awaiting C++..."; 
                
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

    // Initialize hooks
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
    // 2. EXPORT CLEANUP FUNCTION FOR NEXT TIME
    // ==========================================
    window.__discrypt_cleanup = () => {
        // Close the dead socket
        if (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING) {
            ws.close(); 
        }
        // Kill the observers
        if (currentMessageObserver) currentMessageObserver.disconnect();
        if (titleObserver) titleObserver.disconnect();
        
        // Save state
        window.__discrypt_msgId = messageIdCounter;
    };

    return "Success: Cleaned old hooks, installed fresh IPC payload.";
})();
