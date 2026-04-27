#pragma once
#include <windows.h>
#include <UIAutomation.h>
#include <functional>
#include <string>

namespace Discrypt
{
	/// <summary>
	/// Monitors Discord messages using UI Automation events
	/// </summary>
	class MessageMonitor : public IUIAutomationStructureChangedEventHandler
	{
	public:
		using MessageCallback = std::function<void(const std::wstring&)>;

		MessageMonitor();
		~MessageMonitor();

		// Start/stop monitoring
		bool StartMonitoring(HWND discordWindow, MessageCallback callback);
		void StopMonitoring();

		// IUnknown methods
		ULONG STDMETHODCALLTYPE AddRef() override;
		ULONG STDMETHODCALLTYPE Release() override;
		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override;

		// IUIAutomationStructureChangedEventHandler method
		HRESULT STDMETHODCALLTYPE HandleStructureChangedEvent(
			IUIAutomationElement* pSender,
			StructureChangeType changeType,
			SAFEARRAY* pRuntimeId) override;

	private:
		ULONG m_refCount;
		IUIAutomation* m_pAutomation;
		IUIAutomationElement* m_pRootElement;
		MessageCallback m_callback;
		bool m_isMonitoring;
	};
}
