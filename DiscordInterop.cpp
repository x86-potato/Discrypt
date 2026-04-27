#include "pch.h"
#include "DiscordInterop.h"
#include <UIAutomation.h>
#include <wrl/client.h>

using namespace Microsoft::WRL;

namespace Discrypt
{
	HWND DiscordInterop::s_cachedDiscordWindow = nullptr;

	struct FindDiscordData
	{
		HWND hwnd;
	};

	static BOOL CALLBACK FindDiscordProc(HWND hwnd, LPARAM lParam)
	{
		// We only care about visible windows
		if (!IsWindowVisible(hwnd)) return TRUE;

		int length = GetWindowTextLengthW(hwnd);
		if (length == 0) return TRUE;

		std::wstring title(length, L'\0');
		GetWindowTextW(hwnd, title.data(), length + 1);

		// Check if the title contains "Discord"
		if (title.find(L"Discord") != std::wstring::npos)
		{
			auto* data = reinterpret_cast<FindDiscordData*>(lParam);
			data->hwnd = hwnd;
			return FALSE; // Stop enumerating, we found it!
		}
		return TRUE;
	}

	HWND DiscordInterop::FindDiscordWindowByTitle()
	{
		FindDiscordData data = { nullptr };
		EnumWindows(FindDiscordProc, reinterpret_cast<LPARAM>(&data));
		return data.hwnd;
	}

	HWND DiscordInterop::GetDiscordWindow()
	{
		// Check if cached handle is still valid
		if (s_cachedDiscordWindow != nullptr && IsWindow(s_cachedDiscordWindow))
		{
			return s_cachedDiscordWindow;
		}

		// Cache is invalid, search for Discord window
		s_cachedDiscordWindow = FindDiscordWindowByTitle();

		if (s_cachedDiscordWindow != nullptr)
		{
			OutputDebugStringW(L"[Discrypt] Discord window found and cached.\n");
		}

		return s_cachedDiscordWindow;
	}

	void DiscordInterop::InvalidateCache()
	{
		s_cachedDiscordWindow = nullptr;
	}

	std::wstring DiscordInterop::ReadTextBox()
	{
		CoInitializeEx(nullptr, COINIT_MULTITHREADED);

		ComPtr<IUIAutomation> automation;
		CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation));

		HWND hwnd = GetDiscordWindow();
		if (hwnd == nullptr)
		{
			OutputDebugStringW(L"[Discrypt] ReadTextBox: Failed to find Discord window.\n");
			CoUninitialize();
			return L"";
		}

		ComPtr<IUIAutomationElement> TextBoxElement;
		automation->ElementFromHandle(hwnd, &TextBoxElement);

		// Create conditions for Edit and Document controls
		ComPtr<IUIAutomationCondition> editCondition;
		VARIANT varPropEdit;
		varPropEdit.vt = VT_I4;
		varPropEdit.lVal = UIA_EditControlTypeId;
		automation->CreatePropertyCondition(UIA_ControlTypePropertyId, varPropEdit, &editCondition);

		ComPtr<IUIAutomationCondition> docCondition;
		VARIANT varPropDoc;
		varPropDoc.vt = VT_I4;
		varPropDoc.lVal = UIA_DocumentControlTypeId;
		automation->CreatePropertyCondition(UIA_ControlTypePropertyId, varPropDoc, &docCondition);

		ComPtr<IUIAutomationCondition> combinedCondition;
		automation->CreateOrCondition(editCondition.Get(), docCondition.Get(), &combinedCondition);

		// Find all text boxes
		ComPtr<IUIAutomationElementArray> TextBoxArray;
		TextBoxElement->FindAll(TreeScope_Subtree, combinedCondition.Get(), &TextBoxArray);

		if (TextBoxArray == nullptr)
		{
			OutputDebugStringW(L"[Discrypt] ReadTextBox: No text boxes found.\n");
			CoUninitialize();
			return L"";
		}

		int length = 0;
		TextBoxArray->get_Length(&length);

		if (length == 0)
		{
			OutputDebugStringW(L"[Discrypt] ReadTextBox: Text box array is empty.\n");
			CoUninitialize();
			return L"";
		}

		// Get the LAST element (the input box)
		ComPtr<IUIAutomationElement> lastTextBox;
		TextBoxArray->GetElement(length - 1, &lastTextBox);

		if (lastTextBox == nullptr)
		{
			OutputDebugStringW(L"[Discrypt] ReadTextBox: Failed to get last text box.\n");
			CoUninitialize();
			return L"";
		}

		// Get the current value using ValuePattern
		ComPtr<IUIAutomationValuePattern> valuePattern;
		HRESULT hrValue = lastTextBox->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&valuePattern));

		std::wstring result = L"";
		if (SUCCEEDED(hrValue) && valuePattern != nullptr)
		{
			BSTR value = nullptr;
			valuePattern->get_CurrentValue(&value);

			if (value != nullptr)
			{
				result = value;
				SysFreeString(value);
			}

			OutputDebugStringW((L"[Discrypt] ReadTextBox: " + result + L"\n").c_str());
		}
		else
		{
			OutputDebugStringW(L"[Discrypt] ReadTextBox: Failed to get ValuePattern.\n");
		}

		CoUninitialize();
		return result;
	}

	bool DiscordInterop::WriteTextBox(const std::wstring& text)
	{
		CoInitializeEx(nullptr, COINIT_MULTITHREADED);

		ComPtr<IUIAutomation> automation;
		CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation));

		HWND hwnd = GetDiscordWindow();
		if (hwnd == nullptr)
		{
			OutputDebugStringW(L"[Discrypt] WriteTextBox: Failed to find Discord window.\n");
			CoUninitialize();
			return false;
		}

		ComPtr<IUIAutomationElement> TextBoxElement;
		automation->ElementFromHandle(hwnd, &TextBoxElement);

		// Create conditions for Edit and Document controls
		ComPtr<IUIAutomationCondition> editCondition;
		VARIANT varPropEdit;
		varPropEdit.vt = VT_I4;
		varPropEdit.lVal = UIA_EditControlTypeId;
		automation->CreatePropertyCondition(UIA_ControlTypePropertyId, varPropEdit, &editCondition);

		ComPtr<IUIAutomationCondition> docCondition;
		VARIANT varPropDoc;
		varPropDoc.vt = VT_I4;
		varPropDoc.lVal = UIA_DocumentControlTypeId;
		automation->CreatePropertyCondition(UIA_ControlTypePropertyId, varPropDoc, &docCondition);

		ComPtr<IUIAutomationCondition> combinedCondition;
		automation->CreateOrCondition(editCondition.Get(), docCondition.Get(), &combinedCondition);

		// Find all text boxes
		ComPtr<IUIAutomationElementArray> TextBoxArray;
		TextBoxElement->FindAll(TreeScope_Subtree, combinedCondition.Get(), &TextBoxArray);

		if (TextBoxArray == nullptr)
		{
			OutputDebugStringW(L"[Discrypt] WriteTextBox: No text boxes found.\n");
			CoUninitialize();
			return false;
		}

		int length = 0;
		TextBoxArray->get_Length(&length);

		if (length == 0)
		{
			OutputDebugStringW(L"[Discrypt] WriteTextBox: Text box array is empty.\n");
			CoUninitialize();
			return false;
		}

		// Get the LAST element (the input box)
		ComPtr<IUIAutomationElement> lastTextBox;
		TextBoxArray->GetElement(length - 1, &lastTextBox);

		if (lastTextBox == nullptr)
		{
			OutputDebugStringW(L"[Discrypt] WriteTextBox: Failed to get last text box.\n");
			CoUninitialize();
			return false;
		}

		// Set focus to the text box first
		HRESULT hrFocus = lastTextBox->SetFocus();
		if (FAILED(hrFocus))
		{
			OutputDebugStringW(L"[Discrypt] WriteTextBox: Failed to focus text box.\n");
		}

		Sleep(20);

		// Select all existing text with Ctrl+A
		INPUT inputs[4] = {};

		// Ctrl down
		inputs[0].type = INPUT_KEYBOARD;
		inputs[0].ki.wVk = VK_CONTROL;

		// A down
		inputs[1].type = INPUT_KEYBOARD;
		inputs[1].ki.wVk = 'A';

		// A up
		inputs[2].type = INPUT_KEYBOARD;
		inputs[2].ki.wVk = 'A';
		inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;

		// Ctrl up
		inputs[3].type = INPUT_KEYBOARD;
		inputs[3].ki.wVk = VK_CONTROL;
		inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;

		SendInput(4, inputs, sizeof(INPUT));
		Sleep(20);

		// Type the new text using Unicode input
		for (wchar_t ch : text)
		{
			INPUT charInputs[2] = {};

			// Key down
			charInputs[0].type = INPUT_KEYBOARD;
			charInputs[0].ki.wScan = ch;
			charInputs[0].ki.dwFlags = KEYEVENTF_UNICODE;

			// Key up
			charInputs[1].type = INPUT_KEYBOARD;
			charInputs[1].ki.wScan = ch;
			charInputs[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;

			SendInput(2, charInputs, sizeof(INPUT));
		}

		OutputDebugStringW((L"[Discrypt] WriteTextBox: Successfully simulated typing: " + text + L"\n").c_str());

		CoUninitialize();
		return true;
	}
}
