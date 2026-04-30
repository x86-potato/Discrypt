(() => {
	const DISCRYPT_VERSION = '13'; // Bumped version to verify update
	console.log(`[Discrypt] 🔄 Injecting version ${DISCRYPT_VERSION}`);

	// 1. PREVENT DOUBLE-INJECTION GHOSTS
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

	const getAccuratePartnerHandle = () => {
		// Attempt 1: Grab from the top chat header (most accurate, instant update)
		const headerElement = document.querySelector('[class*="title-"] h1');
		if (headerElement) {
			return headerElement.innerText.trim();
		}
		// Attempt 2: Fallback to the document title
		return document.title.split(' - ')[0].trim();
	};

	// 2. WEBSOCKET HANDLERS
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
				console.log('[Discrypt] 📋 Pasting response via ClipboardEvent:', data.text);
				const chatBox = document.querySelector('[class*="slateTextArea"]');
				if (!chatBox) {
					console.warn('[Discrypt] ⚠️ chatBox not found.');
					return;
				}

				chatBox.focus();
				// Re-select-all so Slate has a valid selection to replace on paste
				document.execCommand('selectAll', false, null);

				setTimeout(() => {
					const dt = new DataTransfer();
					dt.setData('text/plain', data.text);
					chatBox.dispatchEvent(new ClipboardEvent('paste', {
						clipboardData: dt,
						bubbles: true,
						cancelable: true
					}));

					// Enter to send
						setTimeout(() => {
							chatBox.dispatchEvent(new KeyboardEvent('keydown', {
								key: 'Enter', code: 'Enter', keyCode: 13, which: 13,
								bubbles: true, cancelable: true
							}));
						}, 150);
					}, 50);
			} 
			// NEW LOGIC: Handle incoming decrypted message updates
			else if (data.id !== undefined && data.text !== undefined) {
				const node = pendingRequests.get(data.id);
				if (node) {
					node.innerText = data.text;
					pendingRequests.delete(data.id); // Clean up map to prevent memory leaks
					console.log(`[Discrypt] ✅ Decrypted message rendered for ID: ${data.id}`);
				} else {
					console.warn(`[Discrypt] ⚠️ Received payload for unknown message ID: ${data.id}`);
				}
			}
		} catch (e) {
			console.error('[Discrypt] ❌ Parse Error:', e);
		}
	};
	// 3. CHAT SWITCH EVENT
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
	// 4. MESSAGE OBSERVER
	function modifyNode(node) {
		if (node.nodeType !== 1) return;

		const contents = node.querySelectorAll?.('[class*="messageContent"]');
		const targetNodes = contents?.length ? Array.from(contents) :
							(node.className?.includes?.('messageContent') ? [node] : []);

		targetNodes.forEach(c => {
			const originalText = c.innerText;
			if (c.dataset.ipcProcessed) return;

			if (originalText.startsWith('[HANDSHAKE_ACK]:')) {
				// Handshake protocol message — notify C++ but leave the text unchanged
				c.dataset.ipcProcessed = "true";
				const id = ++messageIdCounter;
				pendingRequests.set(id, c);

				const sendPayload = () => {
					ws.send(JSON.stringify({
						type: "MESSAGE",
						id: id,
						text: originalText,
						partnerHandle: document.title.split(' - ')[0].trim()
					}));
				};

				if (ws.readyState === WebSocket.OPEN) sendPayload();
				else ws.addEventListener('open', sendPayload, { once: true });

		} else if (originalText.startsWith('[ENC]:')) {
			c.dataset.ipcProcessed = "true";
			const id = ++messageIdCounter;
			pendingRequests.set(id, c);

			// MODIFIED HERE: Now displays the ciphertext alongside an emoji
			c.innerText = "🔒 " + originalText;

			const sendPayload = () => {
				ws.send(JSON.stringify({
					type: "MESSAGE",
					id: id,
					text: originalText,
					// Fix: Dynamically fetch the current accurate handle
					partnerHandle: getAccuratePartnerHandle() 
				}));
			};

			if (ws.readyState === WebSocket.OPEN) sendPayload();
			else ws.addEventListener('open', sendPayload, { once: true });

		}                        });
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
	// 5. COMMAND INTERCEPTION (Alt + Enter)
	const handleCommandInput = (e) => {
		if (e.altKey && e.key === 'Enter') {
			const chatBox = document.querySelector('[class*="slateTextArea"]');

			if (chatBox && chatBox.contains(e.target)) {
				const currentText = chatBox.innerText.trim();

				if (currentText === '!handshake' || currentText === '!accept') {
					e.preventDefault();
					e.stopPropagation();

					console.log(`[Discrypt] v${DISCRYPT_VERSION} ✅ Alt+Enter intercepted — command: "${currentText}" @ ${new Date().toISOString()}`);

					const partnerHandle = document.title.split(' - ')[0].trim();

					if (currentText === '!handshake') {
						if (ws.readyState === WebSocket.OPEN) {
							ws.send(JSON.stringify({ type: 'INIT_HANDSHAKE', partnerHandle: partnerHandle }));
						} else {
							console.warn('[Discrypt] WebSocket not ready. Command dropped.');
						}
					} else if (currentText === '!accept') {
						// Scan all visible message nodes for the earliest [HANDSHAKE_INIT]: message
						const allMessageNodes = document.querySelectorAll('[class*="messageContent"]');
						let initNode = null;
						for (let i = allMessageNodes.length - 1; i >= 0; i--) {
							if (allMessageNodes[i].innerText.includes('[HANDSHAKE_INIT]:')) {
								initNode = allMessageNodes[i];
								break; // most recent = last in DOM order
							}
						}

						if (!initNode) {
							console.warn('[Discrypt] !accept — no [HANDSHAKE_INIT]: message found in chat.');
						} else {
							const rawText = initNode.innerText.trim();
							const prefix = '[HANDSHAKE_INIT]:';
							const partnerPublicKey = rawText.substring(rawText.indexOf(prefix) + prefix.length).trim();
							console.log('[Discrypt] !accept — found partner public key:', partnerPublicKey);

							if (ws.readyState === WebSocket.OPEN) {
								ws.send(JSON.stringify({
									type: 'ACCEPT_HANDSHAKE',
									partnerHandle: partnerHandle,
									partnerPublicKey: partnerPublicKey
								}));
							} else {
								console.warn('[Discrypt] WebSocket not ready. Command dropped.');
							}
						}
					}

					chatBox.focus();
					document.execCommand('selectAll', false, null);

				} else if (currentText.length > 0) {
					// Regular message — route through C++ before sending.
					// C++ will echo it back as SEND_DISCORD_MESSAGE (encrypted in future).
					e.preventDefault();
					e.stopPropagation();

					console.log(`[Discrypt] v${DISCRYPT_VERSION} ✉️ Routing outgoing message through C++: "${currentText}" @ ${new Date().toISOString()}`);

					if (ws.readyState === WebSocket.OPEN) {
						ws.send(JSON.stringify({ type: "OUTGOING_MESSAGE", text: currentText }));
					} else {
						console.warn('[Discrypt] WebSocket not ready. Message dropped.');
					}

					// Select all so the paste from C++ response replaces the typed text
					chatBox.focus();
					document.execCommand('selectAll', false, null);
				}
			}
		}
	};

	document.addEventListener('keydown', handleCommandInput, true);

	// 6. CLEANUP
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