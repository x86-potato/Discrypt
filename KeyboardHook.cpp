#include "pch.h"
#include "KeyboardHook.h"
#include "DiscordInterop.h"

namespace Discrypt
{
	HHOOK KeyboardHook::s_hookHandle = nullptr;
	KeyboardHook::AltEnterCallback KeyboardHook::s_callback = nullptr;

	bool KeyboardHook::Install(AltEnterCallback callback)
	{
		if (s_hookHandle != nullptr)
		{
			OutputDebugStringW(L"[Discrypt] Keyboard hook already installed.\n");
			return false;
		}

		s_callback = callback;
		s_hookHandle = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, nullptr, 0);

		if (s_hookHandle != nullptr)
		{
			OutputDebugStringW(L"[Discrypt] Keyboard hook installed successfully.\n");
			return true;
		}
		else
		{
			OutputDebugStringW(L"[Discrypt] Failed to install keyboard hook.\n");
			return false;
		}
	}

	void KeyboardHook::Uninstall()
	{
		if (s_hookHandle)
		{
			UnhookWindowsHookEx(s_hookHandle);
			s_hookHandle = nullptr;
			s_callback = nullptr;
			OutputDebugStringW(L"[Discrypt] Keyboard hook uninstalled.\n");
		}
	}

	void KeyboardHook::SimulateKeyPress(WORD vkCode, bool isExtended)
	{
		INPUT inputs[2] = {};

		// Key down
		inputs[0].type = INPUT_KEYBOARD;
		inputs[0].ki.wVk = vkCode;
		inputs[0].ki.dwFlags = isExtended ? KEYEVENTF_EXTENDEDKEY : 0;

		// Key up
		inputs[1].type = INPUT_KEYBOARD;
		inputs[1].ki.wVk = vkCode;
		inputs[1].ki.dwFlags = (isExtended ? KEYEVENTF_EXTENDEDKEY : 0) | KEYEVENTF_KEYUP;

		SendInput(2, inputs, sizeof(INPUT));
	}

	LRESULT CALLBACK KeyboardHook::KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
	{
		if (nCode == HC_ACTION)
		{
			KBDLLHOOKSTRUCT* pKeyboard = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
			if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)
			{
				// Check if Enter key is pressed
				if (pKeyboard->vkCode == VK_RETURN)
				{
					// Check if Alt key is also held down (Alt+Enter combination)
					bool altPressed = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;

					if (altPressed)
					{
						// Skip if this is an injected Alt+Enter (our own simulated keypress to prevent recursion)
						// But allow plain injected Enter to pass through (for sending messages)
						if (pKeyboard->flags & LLKHF_INJECTED)
						{
							OutputDebugStringW(L"[Discrypt] Ignoring injected Alt+Enter key.\n");
							return CallNextHookEx(s_hookHandle, nCode, wParam, lParam);
						}

						// Check if Discord window is active
						HWND foregroundWindow = GetForegroundWindow();
						HWND discordWindow = DiscordInterop::GetDiscordWindow();

						if (foregroundWindow == discordWindow && discordWindow != nullptr)
						{
							OutputDebugStringW(L"[Discrypt] Alt+Enter pressed in Discord! Processing...\n");

							// Call the registered callback
							if (s_callback)
							{
								s_callback();
							}

							// BLOCK the Alt+Enter from reaching Discord
							return 1;
						}
					}
				}

				OutputDebugStringW((L"[Discrypt] Key Pressed: " + std::to_wstring(pKeyboard->vkCode) + L"\n").c_str());
			}
		}
		return CallNextHookEx(s_hookHandle, nCode, wParam, lParam);
	}
}
