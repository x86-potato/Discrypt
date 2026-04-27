#pragma once
#include <windows.h>
#include <functional>

namespace Discrypt
{
	/// <summary>
	/// Manages keyboard hooks and input simulation
	/// </summary>
	class KeyboardHook
	{
	public:
		using AltEnterCallback = std::function<void()>;

		// Hook management
		static bool Install(AltEnterCallback callback);
		static void Uninstall();

		// Input simulation
		static void SimulateKeyPress(WORD vkCode, bool isExtended = false);

	private:
		static LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
		static HHOOK s_hookHandle;
		static AltEnterCallback s_callback;
	};
}
