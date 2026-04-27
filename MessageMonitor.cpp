#include "pch.h"
#include "MessageMonitor.h"

#pragma comment(lib, "ole32.lib")

namespace Discrypt
{
	MessageMonitor::MessageMonitor()
		: m_refCount(1)
		, m_pAutomation(nullptr)
		, m_pRootElement(nullptr)
		, m_callback(nullptr)
		, m_isMonitoring(false)
	{
		OutputDebugStringW(L"[Discrypt] MessageMonitor created\n");
	}

	MessageMonitor::~MessageMonitor()
	{
		StopMonitoring();
		OutputDebugStringW(L"[Discrypt] MessageMonitor destroyed\n");
	}

	bool MessageMonitor::StartMonitoring(HWND discordWindow, MessageCallback callback)
	{
		if (m_isMonitoring)
		{
			OutputDebugStringW(L"[Discrypt] Already monitoring\n");
			return false;
		}

		if (!discordWindow || !callback)
		{
			OutputDebugStringW(L"[Discrypt] Invalid parameters for monitoring\n");
			return false;
		}

		m_callback = callback;

		// Initialize COM
		CoInitialize(nullptr);

		// Create UI Automation instance
		HRESULT hr = CoCreateInstance(__uuidof(CUIAutomation), nullptr,
			CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation), (void**)&m_pAutomation);

		if (FAILED(hr) || !m_pAutomation)
		{
			OutputDebugStringW(L"[Discrypt] Failed to create UI Automation instance\n");
			CoUninitialize();
			return false;
		}

		// Get root element from Discord window
		hr = m_pAutomation->ElementFromHandle(discordWindow, &m_pRootElement);
		if (FAILED(hr) || !m_pRootElement)
		{
			OutputDebugStringW(L"[Discrypt] Failed to get root element from Discord window\n");
			m_pAutomation->Release();
			m_pAutomation = nullptr;
			CoUninitialize();
			return false;
		}

		// Add structure changed event handler
		hr = m_pAutomation->AddStructureChangedEventHandler(
			m_pRootElement,
			TreeScope_Descendants,
			nullptr,
			this);

		if (FAILED(hr))
		{
			OutputDebugStringW(L"[Discrypt] Failed to add structure changed event handler\n");
			OutputDebugStringW((L"[Discrypt] HRESULT: 0x" + std::to_wstring(hr) + L"\n").c_str());
			m_pRootElement->Release();
			m_pRootElement = nullptr;
			m_pAutomation->Release();
			m_pAutomation = nullptr;
			CoUninitialize();
			return false;
		}

		m_isMonitoring = true;
		OutputDebugStringW(L"[Discrypt] *** MESSAGE MONITORING STARTED ***\n");
		OutputDebugStringW(L"[Discrypt] Listening for structure changes in Discord...\n");

		return true;
	}

	void MessageMonitor::StopMonitoring()
	{
		if (!m_isMonitoring)
			return;

		if (m_pAutomation && m_pRootElement)
		{
			m_pAutomation->RemoveStructureChangedEventHandler(m_pRootElement, this);
			OutputDebugStringW(L"[Discrypt] Structure changed event handler removed\n");
		}

		if (m_pRootElement)
		{
			m_pRootElement->Release();
			m_pRootElement = nullptr;
		}

		if (m_pAutomation)
		{
			m_pAutomation->Release();
			m_pAutomation = nullptr;
		}

		m_callback = nullptr;
		m_isMonitoring = false;

		CoUninitialize();
		OutputDebugStringW(L"[Discrypt] *** MESSAGE MONITORING STOPPED ***\n");
	}

	ULONG STDMETHODCALLTYPE MessageMonitor::AddRef()
	{
		return InterlockedIncrement(&m_refCount);
	}

	ULONG STDMETHODCALLTYPE MessageMonitor::Release()
	{
		ULONG count = InterlockedDecrement(&m_refCount);
		if (count == 0)
		{
			delete this;
		}
		return count;
	}

	HRESULT STDMETHODCALLTYPE MessageMonitor::QueryInterface(REFIID riid, void** ppvObject)
	{
		if (!ppvObject)
			return E_POINTER;

		*ppvObject = nullptr;

		if (riid == __uuidof(IUnknown))
		{
			*ppvObject = static_cast<IUnknown*>(this);
		}
		else if (riid == __uuidof(IUIAutomationStructureChangedEventHandler))
		{
			*ppvObject = static_cast<IUIAutomationStructureChangedEventHandler*>(this);
		}
		else
		{
			return E_NOINTERFACE;
		}

		AddRef();
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE MessageMonitor::HandleStructureChangedEvent(
		IUIAutomationElement* pSender,
		StructureChangeType changeType,
		SAFEARRAY* pRuntimeId)
	{
		// Log the event
		std::wstring changeTypeStr;
		switch (changeType)
		{
		case StructureChangeType_ChildAdded:
			changeTypeStr = L"ChildAdded";
			break;
		case StructureChangeType_ChildRemoved:
			changeTypeStr = L"ChildRemoved";
			break;
		case StructureChangeType_ChildrenInvalidated:
			changeTypeStr = L"ChildrenInvalidated";
			break;
		case StructureChangeType_ChildrenBulkAdded:
			changeTypeStr = L"ChildrenBulkAdded";
			break;
		case StructureChangeType_ChildrenBulkRemoved:
			changeTypeStr = L"ChildrenBulkRemoved";
			break;
		case StructureChangeType_ChildrenReordered:
			changeTypeStr = L"ChildrenReordered";
			break;
		default:
			changeTypeStr = L"Unknown";
			break;
		}

		OutputDebugStringW((L"[Discrypt] *** DOM CHANGE DETECTED *** Type: " + changeTypeStr + L"\n").c_str());

		// Try to get element name for debugging
		if (pSender)
		{
			BSTR name = nullptr;
			HRESULT hr = pSender->get_CurrentName(&name);
			if (SUCCEEDED(hr) && name)
			{
				OutputDebugStringW((L"[Discrypt] Element name: " + std::wstring(name) + L"\n").c_str());
				SysFreeString(name);
			}

			// Try to get control type
			CONTROLTYPEID controlType;
			hr = pSender->get_CurrentControlType(&controlType);
			if (SUCCEEDED(hr))
			{
				OutputDebugStringW((L"[Discrypt] Control type ID: " + std::to_wstring(controlType) + L"\n").c_str());
			}

			// Check if it's a text element
			if (controlType == UIA_TextControlTypeId || controlType == UIA_EditControlTypeId)
			{
				IUIAutomationValuePattern* pValuePattern = nullptr;
				hr = pSender->GetCurrentPatternAs(UIA_ValuePatternId,
					__uuidof(IUIAutomationValuePattern), (void**)&pValuePattern);

				if (SUCCEEDED(hr) && pValuePattern)
				{
					BSTR value = nullptr;
					hr = pValuePattern->get_CurrentValue(&value);
					if (SUCCEEDED(hr) && value)
					{
						std::wstring text(value);
						OutputDebugStringW((L"[Discrypt] Text content: " + text + L"\n").c_str());

						// Call the callback if message starts with [ENC]:
						if (text.find(L"[ENC]:") == 0 && m_callback)
						{
							OutputDebugStringW(L"[Discrypt] *** ENCRYPTED MESSAGE DETECTED ***\n");
							m_callback(text);
						}

						SysFreeString(value);
					}
					pValuePattern->Release();
				}
			}
		}

		return S_OK;
	}
}
