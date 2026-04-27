#pragma once
#include <string>
#include <windows.h>

namespace Discrypt
{
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

	private:
		static HWND FindDiscordWindowByTitle();
		static HWND s_cachedDiscordWindow;
	};
}
