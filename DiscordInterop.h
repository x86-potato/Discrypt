#pragma once
#include <string>
#include <windows.h>
#include <vector>

namespace Discrypt
{
	/// <summary>
	/// Represents an encrypted message with its screen position
	/// </summary>
	struct EncryptedMessageData
	{
		std::wstring text;
		RECT position;
	};

	/// <summary>
	/// Handles all Discord window interaction and UI Automation
	/// </summary>
	class DiscordInterop
	{
	public:
		// Window management
		static HWND GetDiscordWindow();
		static void InvalidateCache();

		// Text box interaction
		static std::wstring ReadTextBox();
		static bool WriteTextBox(const std::wstring& text);

		// Message scanning
		static void ScanAndModifyMessages();
		static std::vector<std::wstring> ScanForEncryptedMessages();
		static std::vector<EncryptedMessageData> ScanForEncryptedMessagesWithPositions();

		// Experimental: Test various message modification techniques
		static void TestMessageModification();

	private:
		static HWND FindDiscordWindowByTitle();
		static HWND s_cachedDiscordWindow;
	};
}
