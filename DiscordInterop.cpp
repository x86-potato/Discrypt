#include "pch.h"
#include "DiscordInterop.h"
#include <UIAutomation.h>
#include <wrl/client.h>
#include <algorithm>
#include <vector>

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
		else
		{
			OutputDebugStringW(L"[Discrypt] WriteTextBox: Successfully focused text box.\n");
		}

		Sleep(100);

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
		Sleep(50);

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

	void DiscordInterop::ScanAndModifyMessages()
	{
		CoInitializeEx(nullptr, COINIT_MULTITHREADED);

		ComPtr<IUIAutomation> automation;
		HRESULT hr = CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation));
		if (FAILED(hr))
		{
			OutputDebugStringW(L"[Discrypt] ScanMessages: Failed to create IUIAutomation instance.\n");
			CoUninitialize();
			return;
		}

		HWND hwnd = GetDiscordWindow();
		if (hwnd == nullptr)
		{
			OutputDebugStringW(L"[Discrypt] ScanMessages: Failed to find Discord window.\n");
			CoUninitialize();
			return;
		}

		ComPtr<IUIAutomationElement> rootElement;
		hr = automation->ElementFromHandle(hwnd, &rootElement);
		if (FAILED(hr) || rootElement == nullptr)
		{
			OutputDebugStringW(L"[Discrypt] ScanMessages: Failed to get root element.\n");
			CoUninitialize();
			return;
		}

		OutputDebugStringW(L"[Discrypt] ========== SCANNING FOR MESSAGES ==========\n");

		// Create condition for Text controls (where messages are displayed)
		ComPtr<IUIAutomationCondition> textCondition;
		VARIANT varProp;
		varProp.vt = VT_I4;
		varProp.lVal = UIA_TextControlTypeId;
		automation->CreatePropertyCondition(UIA_ControlTypePropertyId, varProp, &textCondition);

		// Find all text elements
		ComPtr<IUIAutomationElementArray> textElements;
		hr = rootElement->FindAll(TreeScope_Descendants, textCondition.Get(), &textElements);
		if (FAILED(hr) || textElements == nullptr)
		{
			OutputDebugStringW(L"[Discrypt] ScanMessages: Failed to find text elements.\n");
			CoUninitialize();
			return;
		}

		int length = 0;
		textElements->get_Length(&length);
		OutputDebugStringW((L"[Discrypt] ScanMessages: Found " + std::to_wstring(length) + L" text elements.\n").c_str());

		for (int i = 0; i < length; i++)
		{
			ComPtr<IUIAutomationElement> element;
			hr = textElements->GetElement(i, &element);
			if (FAILED(hr) || element == nullptr)
			{
				continue;
			}

			// Try to get the element's name
			BSTR name = nullptr;
			element->get_CurrentName(&name);
			std::wstring elementName = name ? name : L"";
			if (name) SysFreeString(name);

			// Try to get text using ValuePattern
			ComPtr<IUIAutomationValuePattern> valuePattern;
			hr = element->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&valuePattern));

			std::wstring textContent;
			if (SUCCEEDED(hr) && valuePattern != nullptr)
			{
				BSTR value = nullptr;
				valuePattern->get_CurrentValue(&value);
				if (value != nullptr)
				{
					textContent = value;
					SysFreeString(value);
				}
			}

			// If no ValuePattern, try to get the Name property as text
			if (textContent.empty())
			{
				textContent = elementName;
			}

			// Check if the text starts with "test" (case-insensitive)
			if (!textContent.empty())
			{
				std::wstring lowerText = textContent;
				std::transform(lowerText.begin(), lowerText.end(), lowerText.begin(), ::towlower);

				if (lowerText.find(L"test") == 0)
				{
					OutputDebugStringW((L"[Discrypt] *** FOUND MESSAGE STARTING WITH 'test': " + textContent + L"\n").c_str());

					// Try to modify using ValuePattern
					if (valuePattern != nullptr)
					{
						BSTR newValue = SysAllocString(L"updated");
						hr = valuePattern->SetValue(newValue);
						SysFreeString(newValue);

						if (SUCCEEDED(hr))
						{
							OutputDebugStringW(L"[Discrypt] *** SUCCESSFULLY UPDATED MESSAGE TO 'updated' using ValuePattern!\n");
						}
						else
						{
							OutputDebugStringW(L"[Discrypt] *** Failed to update using ValuePattern (read-only).\n");
						}
					}
					else
					{
						OutputDebugStringW(L"[Discrypt] *** No ValuePattern available for modification.\n");
					}
				}
			}
		}

		OutputDebugStringW(L"[Discrypt] ========== SCAN COMPLETE ==========\n");
		CoUninitialize();
	}

	std::vector<std::wstring> DiscordInterop::ScanForEncryptedMessages()
	{
		std::vector<std::wstring> encryptedMessages;

		CoInitializeEx(nullptr, COINIT_MULTITHREADED);

		ComPtr<IUIAutomation> automation;
		HRESULT hr = CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation));
		if (FAILED(hr))
		{
			CoUninitialize();
			return encryptedMessages;
		}

		HWND hwnd = GetDiscordWindow();
		if (hwnd == nullptr)
		{
			CoUninitialize();
			return encryptedMessages;
		}

		ComPtr<IUIAutomationElement> rootElement;
		hr = automation->ElementFromHandle(hwnd, &rootElement);
		if (FAILED(hr) || rootElement == nullptr)
		{
			CoUninitialize();
			return encryptedMessages;
		}

		// Create condition for Text controls
		ComPtr<IUIAutomationCondition> textCondition;
		VARIANT varProp;
		varProp.vt = VT_I4;
		varProp.lVal = UIA_TextControlTypeId;
		automation->CreatePropertyCondition(UIA_ControlTypePropertyId, varProp, &textCondition);

		// Find all text elements
		ComPtr<IUIAutomationElementArray> textElements;
		hr = rootElement->FindAll(TreeScope_Descendants, textCondition.Get(), &textElements);
		if (FAILED(hr) || textElements == nullptr)
		{
			CoUninitialize();
			return encryptedMessages;
		}

		int length = 0;
		textElements->get_Length(&length);

		for (int i = 0; i < length; i++)
		{
			ComPtr<IUIAutomationElement> element;
			hr = textElements->GetElement(i, &element);
			if (FAILED(hr) || element == nullptr)
			{
				continue;
			}

			// Try to get the element's name
			BSTR name = nullptr;
			element->get_CurrentName(&name);
			std::wstring textContent = name ? name : L"";
			if (name) SysFreeString(name);

			// Check if the text contains [ENC]:
			if (!textContent.empty() && textContent.find(L"[ENC]:") != std::wstring::npos)
			{
				encryptedMessages.push_back(textContent);
				OutputDebugStringW((L"[Discrypt] Found encrypted message: " + textContent.substr(0, 60) + L"...\n").c_str());
			}
		}

		CoUninitialize();
		return encryptedMessages;
	}

	std::vector<EncryptedMessageData> DiscordInterop::ScanForEncryptedMessagesWithPositions()
	{
		std::vector<EncryptedMessageData> encryptedMessages;

		CoInitializeEx(nullptr, COINIT_MULTITHREADED);

		ComPtr<IUIAutomation> automation;
		HRESULT hr = CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation));
		if (FAILED(hr))
		{
			CoUninitialize();
			return encryptedMessages;
		}

		HWND hwnd = GetDiscordWindow();
		if (hwnd == nullptr)
		{
			CoUninitialize();
			return encryptedMessages;
		}

		ComPtr<IUIAutomationElement> rootElement;
		hr = automation->ElementFromHandle(hwnd, &rootElement);
		if (FAILED(hr) || rootElement == nullptr)
		{
			CoUninitialize();
			return encryptedMessages;
		}

		// Create condition for Text controls
		ComPtr<IUIAutomationCondition> textCondition;
		VARIANT varProp;
		varProp.vt = VT_I4;
		varProp.lVal = UIA_TextControlTypeId;
		automation->CreatePropertyCondition(UIA_ControlTypePropertyId, varProp, &textCondition);

		// Find all text elements
		ComPtr<IUIAutomationElementArray> textElements;
		hr = rootElement->FindAll(TreeScope_Descendants, textCondition.Get(), &textElements);
		if (FAILED(hr) || textElements == nullptr)
		{
			CoUninitialize();
			return encryptedMessages;
		}

		int length = 0;
		textElements->get_Length(&length);

		// Get Discord window position for coordinate conversion
		RECT discordRect;
		GetWindowRect(hwnd, &discordRect);

		for (int i = 0; i < length; i++)
		{
			ComPtr<IUIAutomationElement> element;
			hr = textElements->GetElement(i, &element);
			if (FAILED(hr) || element == nullptr)
			{
				continue;
			}

			// Try to get the element's name
			BSTR name = nullptr;
			element->get_CurrentName(&name);
			std::wstring textContent = name ? name : L"";
			if (name) SysFreeString(name);

			// Check if the text contains [ENC]:
			if (!textContent.empty() && textContent.find(L"[ENC]:") != std::wstring::npos)
			{
				EncryptedMessageData msgData;
				msgData.text = textContent;

				// Try to get bounding rectangle
				RECT boundingRect;
				hr = element->get_CurrentBoundingRectangle(&boundingRect);

				if (SUCCEEDED(hr) && boundingRect.left != 0 && boundingRect.top != 0)
				{
					// We have valid coordinates
					msgData.position = boundingRect;
				}
				else
				{
					// Fallback: use default position (will be stacked vertically in overlay)
					msgData.position = { 0, 0, 0, 0 };
				}

				encryptedMessages.push_back(msgData);
				OutputDebugStringW((L"[Discrypt] Found encrypted message at (" + 
					std::to_wstring(msgData.position.left) + L"," + std::to_wstring(msgData.position.top) + L"): " + 
					textContent.substr(0, 40) + L"...\n").c_str());
			}
		}

		CoUninitialize();
		return encryptedMessages;
	}

	void DiscordInterop::TestMessageModification()
	{
		CoInitializeEx(nullptr, COINIT_MULTITHREADED);

		ComPtr<IUIAutomation> automation;
		HRESULT hr = CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation));
		if (FAILED(hr))
		{
			OutputDebugStringW(L"[Discrypt] TestModification: Failed to create IUIAutomation.\n");
			CoUninitialize();
			return;
		}

		HWND hwnd = GetDiscordWindow();
		if (!hwnd)
		{
			OutputDebugStringW(L"[Discrypt] TestModification: Discord window not found.\n");
			CoUninitialize();
			return;
		}

		ComPtr<IUIAutomationElement> rootElement;
		automation->ElementFromHandle(hwnd, &rootElement);

		// Find text elements
		ComPtr<IUIAutomationCondition> textCondition;
		VARIANT varProp;
		varProp.vt = VT_I4;
		varProp.lVal = UIA_TextControlTypeId;
		automation->CreatePropertyCondition(UIA_ControlTypePropertyId, varProp, &textCondition);

		ComPtr<IUIAutomationElementArray> textElements;
		rootElement->FindAll(TreeScope_Descendants, textCondition.Get(), &textElements);

		if (!textElements)
		{
			OutputDebugStringW(L"[Discrypt] TestModification: No text elements found.\n");
			CoUninitialize();
			return;
		}

		int length = 0;
		textElements->get_Length(&length);

		OutputDebugStringW(L"\n[Discrypt] ========================================\n");
		OutputDebugStringW(L"[Discrypt] TESTING MESSAGE MODIFICATION TECHNIQUES\n");
		OutputDebugStringW(L"[Discrypt] ========================================\n\n");

		// Find a message starting with "test"
		for (int i = 0; i < length; i++)
		{
			ComPtr<IUIAutomationElement> element;
			textElements->GetElement(i, &element);
			if (!element) continue;

			BSTR name = nullptr;
			element->get_CurrentName(&name);
			std::wstring textContent = name ? name : L"";
			if (name) SysFreeString(name);

			std::wstring lowerText = textContent;
			std::transform(lowerText.begin(), lowerText.end(), lowerText.begin(), ::towlower);

			if (lowerText.find(L"test") == 0)
			{
				OutputDebugStringW((L"[Discrypt] Found test message: " + textContent + L"\n").c_str());
				OutputDebugStringW(L"[Discrypt] Analyzing available patterns...\n\n");

				// TEST 1: Check ALL available patterns
				OutputDebugStringW(L"[Discrypt] === PATTERN AVAILABILITY ===\n");

				IUIAutomationValuePattern* pValuePattern = nullptr;
				hr = element->GetCurrentPatternAs(UIA_ValuePatternId, __uuidof(IUIAutomationValuePattern), (void**)&pValuePattern);
				OutputDebugStringW((L"[Discrypt] ValuePattern: " + std::wstring(SUCCEEDED(hr) && pValuePattern ? L"Available" : L"Not Available") + L"\n").c_str());
				if (pValuePattern) pValuePattern->Release();

				IUIAutomationTextPattern* pTextPattern = nullptr;
				hr = element->GetCurrentPatternAs(UIA_TextPatternId, __uuidof(IUIAutomationTextPattern), (void**)&pTextPattern);
				OutputDebugStringW((L"[Discrypt] TextPattern: " + std::wstring(SUCCEEDED(hr) && pTextPattern ? L"Available" : L"Not Available") + L"\n").c_str());
				if (pTextPattern) pTextPattern->Release();

				IUIAutomationInvokePattern* pInvokePattern = nullptr;
				hr = element->GetCurrentPatternAs(UIA_InvokePatternId, __uuidof(IUIAutomationInvokePattern), (void**)&pInvokePattern);
				OutputDebugStringW((L"[Discrypt] InvokePattern: " + std::wstring(SUCCEEDED(hr) && pInvokePattern ? L"Available" : L"Not Available") + L"\n").c_str());
				if (pInvokePattern) pInvokePattern->Release();

				IUIAutomationLegacyIAccessiblePattern* pLegacyPattern = nullptr;
				hr = element->GetCurrentPatternAs(UIA_LegacyIAccessiblePatternId, __uuidof(IUIAutomationLegacyIAccessiblePattern), (void**)&pLegacyPattern);
				OutputDebugStringW((L"[Discrypt] LegacyIAccessiblePattern: " + std::wstring(SUCCEEDED(hr) && pLegacyPattern ? L"Available" : L"Not Available") + L"\n").c_str());
				if (pLegacyPattern) pLegacyPattern->Release();

				IUIAutomationSelectionItemPattern* pSelectionPattern = nullptr;
				hr = element->GetCurrentPatternAs(UIA_SelectionItemPatternId, __uuidof(IUIAutomationSelectionItemPattern), (void**)&pSelectionPattern);
				OutputDebugStringW((L"[Discrypt] SelectionItemPattern: " + std::wstring(SUCCEEDED(hr) && pSelectionPattern ? L"Available" : L"Not Available") + L"\n").c_str());
				if (pSelectionPattern) pSelectionPattern->Release();

				// TEST 2: Try TextPattern manipulation
				OutputDebugStringW(L"\n[Discrypt] === TESTING TextPattern ===\n");
				IUIAutomationTextPattern* pText = nullptr;
				hr = element->GetCurrentPatternAs(UIA_TextPatternId, __uuidof(IUIAutomationTextPattern), (void**)&pText);
				if (SUCCEEDED(hr) && pText)
				{
					OutputDebugStringW(L"[Discrypt] TextPattern available! Attempting to get document range...\n");

					IUIAutomationTextRange* pRange = nullptr;
					hr = pText->get_DocumentRange(&pRange);
					if (SUCCEEDED(hr) && pRange)
					{
						OutputDebugStringW(L"[Discrypt] Got document range! Attempting to modify text...\n");

						// Try to select the text
						hr = pRange->Select();
						if (SUCCEEDED(hr))
						{
							OutputDebugStringW(L"[Discrypt] ? Successfully selected text range!\n");

							// Now try to type replacement text
							Sleep(100);

							std::wstring newText = L"MODIFIED BY DISCRYPT";
							for (wchar_t ch : newText)
							{
								INPUT charInputs[2] = {};
								charInputs[0].type = INPUT_KEYBOARD;
								charInputs[0].ki.wScan = ch;
								charInputs[0].ki.dwFlags = KEYEVENTF_UNICODE;
								charInputs[1].type = INPUT_KEYBOARD;
								charInputs[1].ki.wScan = ch;
								charInputs[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
								SendInput(2, charInputs, sizeof(INPUT));
							}
							OutputDebugStringW(L"[Discrypt] Typed replacement text via keyboard simulation.\n");
						}
						else
						{
							OutputDebugStringW(L"[Discrypt] ? Failed to select text range.\n");
						}
						pRange->Release();
					}
					pText->Release();
				}

				// TEST 3: Try InvokePattern (might trigger edit mode)
				OutputDebugStringW(L"\n[Discrypt] === TESTING InvokePattern ===\n");
				IUIAutomationInvokePattern* pInvoke = nullptr;
				hr = element->GetCurrentPatternAs(UIA_InvokePatternId, __uuidof(IUIAutomationInvokePattern), (void**)&pInvoke);
				if (SUCCEEDED(hr) && pInvoke)
				{
					OutputDebugStringW(L"[Discrypt] InvokePattern available! Attempting to invoke...\n");
					hr = pInvoke->Invoke();
					if (SUCCEEDED(hr))
					{
						OutputDebugStringW(L"[Discrypt] ? Successfully invoked element! (Check if edit mode activated)\n");
						Sleep(500); // Wait to see if edit mode appears
					}
					else
					{
						OutputDebugStringW(L"[Discrypt] ? Failed to invoke element.\n");
					}
					pInvoke->Release();
				}

				// TEST 4: Try LegacyIAccessiblePattern DoDefaultAction
				OutputDebugStringW(L"\n[Discrypt] === TESTING LegacyIAccessiblePattern ===\n");
				IUIAutomationLegacyIAccessiblePattern* pLegacy = nullptr;
				hr = element->GetCurrentPatternAs(UIA_LegacyIAccessiblePatternId, __uuidof(IUIAutomationLegacyIAccessiblePattern), (void**)&pLegacy);
				if (SUCCEEDED(hr) && pLegacy)
				{
					OutputDebugStringW(L"[Discrypt] LegacyIAccessiblePattern available!\n");

					// Try DoDefaultAction
					hr = pLegacy->DoDefaultAction();
					if (SUCCEEDED(hr))
					{
						OutputDebugStringW(L"[Discrypt] ? DoDefaultAction succeeded!\n");
						Sleep(500);
					}
					else
					{
						OutputDebugStringW(L"[Discrypt] ? DoDefaultAction failed or not available.\n");
					}

					// Try to get the role
					DWORD role = 0;
					hr = pLegacy->get_CurrentRole(&role);
					if (SUCCEEDED(hr))
					{
						OutputDebugStringW((L"[Discrypt] Accessible Role: " + std::to_wstring(role) + L"\n").c_str());
					}

					pLegacy->Release();
				}

				// TEST 5: Try mouse click simulation on the element
				OutputDebugStringW(L"\n[Discrypt] === TESTING Mouse Click Simulation ===\n");
				RECT rect;
				hr = element->get_CurrentBoundingRectangle(&rect);
				if (SUCCEEDED(hr))
				{
					OutputDebugStringW((L"[Discrypt] Element bounds: Left=" + std::to_wstring(rect.left) + 
						L", Top=" + std::to_wstring(rect.top) +
						L", Right=" + std::to_wstring(rect.right) +
						L", Bottom=" + std::to_wstring(rect.bottom) + L"\n").c_str());

					// Calculate center point
					int centerX = (rect.left + rect.right) / 2;
					int centerY = (rect.top + rect.bottom) / 2;

					OutputDebugStringW((L"[Discrypt] Clicking at center: X=" + std::to_wstring(centerX) + L", Y=" + std::to_wstring(centerY) + L"\n").c_str());

					// Save current cursor position
					POINT oldPos;
					GetCursorPos(&oldPos);

					// Move to element and click
					SetCursorPos(centerX, centerY);
					Sleep(50);

					// Single click
					mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
					Sleep(10);
					mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
					Sleep(200);

					OutputDebugStringW(L"[Discrypt] Single click executed.\n");

					// Restore cursor
					SetCursorPos(oldPos.x, oldPos.y);
				}

				// TEST 6: Try double-click (might enter edit mode)
				OutputDebugStringW(L"\n[Discrypt] === TESTING Double-Click ===\n");
				hr = element->get_CurrentBoundingRectangle(&rect);
				if (SUCCEEDED(hr))
				{
					int centerX = (rect.left + rect.right) / 2;
					int centerY = (rect.top + rect.bottom) / 2;

					POINT oldPos;
					GetCursorPos(&oldPos);

					SetCursorPos(centerX, centerY);
					Sleep(50);

					// Double click
					mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
					mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
					Sleep(50);
					mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
					mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);

					OutputDebugStringW(L"[Discrypt] Double-click executed. Check if edit mode activated!\n");
					Sleep(500);

					SetCursorPos(oldPos.x, oldPos.y);
				}

				// TEST 7: Try right-click (context menu)
				OutputDebugStringW(L"\n[Discrypt] === TESTING Right-Click (Context Menu) ===\n");
				hr = element->get_CurrentBoundingRectangle(&rect);
				if (SUCCEEDED(hr))
				{
					int centerX = (rect.left + rect.right) / 2;
					int centerY = (rect.top + rect.bottom) / 2;

					POINT oldPos;
					GetCursorPos(&oldPos);

					SetCursorPos(centerX, centerY);
					Sleep(50);

					// Right click
					mouse_event(MOUSEEVENTF_RIGHTDOWN, 0, 0, 0, 0);
					Sleep(10);
					mouse_event(MOUSEEVENTF_RIGHTUP, 0, 0, 0, 0);

					OutputDebugStringW(L"[Discrypt] Right-click executed. Check for context menu!\n");
					Sleep(1000); // Give time for context menu to appear

					SetCursorPos(oldPos.x, oldPos.y);
				}

				// TEST 8: Explore parent elements
				OutputDebugStringW(L"\n[Discrypt] === EXPLORING PARENT ELEMENTS ===\n");
				ComPtr<IUIAutomationTreeWalker> walker;
				hr = automation->get_ControlViewWalker(&walker);
				if (SUCCEEDED(hr) && walker)
				{
					ComPtr<IUIAutomationElement> parent;
					hr = walker->GetParentElement(element.Get(), &parent);
					if (SUCCEEDED(hr) && parent)
					{
						BSTR parentName = nullptr;
						parent->get_CurrentName(&parentName);
						CONTROLTYPEID parentControlType = 0;
						parent->get_CurrentControlType(&parentControlType);

						OutputDebugStringW((L"[Discrypt] Parent element name: " + std::wstring(parentName ? parentName : L"(none)") + L"\n").c_str());
						OutputDebugStringW((L"[Discrypt] Parent control type: " + std::to_wstring(parentControlType) + L"\n").c_str());

						if (parentName) SysFreeString(parentName);

						// Check if parent has different patterns
						IUIAutomationValuePattern* pParentValue = nullptr;
						hr = parent->GetCurrentPatternAs(UIA_ValuePatternId, __uuidof(IUIAutomationValuePattern), (void**)&pParentValue);
						OutputDebugStringW((L"[Discrypt] Parent ValuePattern: " + std::wstring(SUCCEEDED(hr) && pParentValue ? L"Available" : L"Not Available") + L"\n").c_str());
						if (pParentValue) pParentValue->Release();
					}
				}

				// TEST 4: Check if element is editable (has edit role)
				OutputDebugStringW(L"\n[Discrypt] === ELEMENT PROPERTIES ===\n");
				BOOL isEnabled = FALSE;
				element->get_CurrentIsEnabled(&isEnabled);
				OutputDebugStringW((L"[Discrypt] IsEnabled: " + std::wstring(isEnabled ? L"True" : L"False") + L"\n").c_str());

				BOOL isKeyboardFocusable = FALSE;
				element->get_CurrentIsKeyboardFocusable(&isKeyboardFocusable);
				OutputDebugStringW((L"[Discrypt] IsKeyboardFocusable: " + std::wstring(isKeyboardFocusable ? L"True" : L"False") + L"\n").c_str());

				BOOL isOffscreen = FALSE;
				element->get_CurrentIsOffscreen(&isOffscreen);
				OutputDebugStringW((L"[Discrypt] IsOffscreen: " + std::wstring(isOffscreen ? L"True" : L"False") + L"\n").c_str());

				OutputDebugStringW(L"\n[Discrypt] ========================================\n");
				OutputDebugStringW(L"[Discrypt] TEST COMPLETE - Check Discord to see results!\n");
				OutputDebugStringW(L"[Discrypt] ========================================\n\n");

				// Only test the first message
				break;
			}
		}

		CoUninitialize();
	}
}
