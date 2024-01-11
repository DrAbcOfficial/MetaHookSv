#include <metahook.h>
#include "exportfuncs.h"
#include "privatefuncs.h"
#include "DpiManager.h"
#include "Surface2.h"

extern int g_iProportionalBaseWidth;
extern int g_iProportionalBaseHeight;
extern int g_iProportionalBaseWidthHD;
extern int g_iProportionalBaseHeightHD;

class CDpiManager : public IDpiManager
{
private:
	float m_flDpiScaling;
	int m_iDpiScalingSource;
	bool m_bIsHighDpiSupported;
public:
	CDpiManager()
	{
		m_flDpiScaling = 0;
		m_iDpiScalingSource = 0;
		m_bIsHighDpiSupported = false;
	}

	void Init() override
	{
		if (g_iEngineType != ENGINE_GOLDSRC_HL25)
			m_bIsHighDpiSupported = true;

		if (gEngfuncs.CheckParm("-no_high_dpi", NULL))
			m_bIsHighDpiSupported = false;

		if (g_iEngineType == ENGINE_GOLDSRC_HL25)
			m_bIsHighDpiSupported = false;

		if (DpiScalingSource_SDL2 > m_iDpiScalingSource)
		{
			if (gPrivateFuncs.SDL_GetDisplayDPI)
			{
				float ddpi = 0;
				float hdpi = 0;
				float vdpi = 0;

				if (0 == gPrivateFuncs.SDL_GetDisplayDPI(0, &ddpi, &hdpi, &vdpi))
				{
					m_flDpiScaling = vdpi / 96.0f;
					m_iDpiScalingSource = DpiScalingSource_SDL2;
				}
			}
		}

		if (DpiScalingSource_System > m_iDpiScalingSource)
		{
			auto user32 = GetModuleHandleA("user32.dll");
			if (user32)
			{
				auto pfnGetDpiForSystem = (decltype(GetDpiForSystem)*)GetProcAddress(user32, "GetDpiForSystem");

				if (pfnGetDpiForSystem)
				{
					m_flDpiScaling = pfnGetDpiForSystem() / 96.0f;
					m_iDpiScalingSource = DpiScalingSource_System;
				}
			}
		}

		if (DpiScalingSource_System > m_iDpiScalingSource)
		{
			HDC hScreen = GetDC(g_MainWnd);
			int dpiX = GetDeviceCaps(hScreen, LOGPIXELSX);
			int dpiY = GetDeviceCaps(hScreen, LOGPIXELSY);
			ReleaseDC(g_MainWnd, hScreen);

			m_flDpiScaling = (float)dpiY / 96.0f;
			m_iDpiScalingSource = DpiScalingSource_System;
		}
	}

	void InitFromMainHwnd() override
	{
		if (DpiScalingSource_Window > m_iDpiScalingSource)
		{
			auto user32 = GetModuleHandleA("user32.dll");
			if (user32)
			{
				auto pfnGetDpiForWindow = (decltype(GetDpiForWindow)*)GetProcAddress(user32, "GetDpiForWindow");
				if (pfnGetDpiForWindow)
				{
					m_flDpiScaling = pfnGetDpiForWindow(g_MainWnd) / 96.0f;
					m_iDpiScalingSource = DpiScalingSource_Window;
				}
			}
		}

		if (DpiScalingSource_Window > m_iDpiScalingSource)
		{
			HDC hScreen = GetDC(g_MainWnd);
			int dpiX = GetDeviceCaps(hScreen, LOGPIXELSX);
			int dpiY = GetDeviceCaps(hScreen, LOGPIXELSY);
			ReleaseDC(g_MainWnd, hScreen);

			m_flDpiScaling = (float)dpiY / 96.0f;
			m_iDpiScalingSource = DpiScalingSource_Window;
		}
	}

	void Shutdown()  override
	{

	}

	float GetDpiScaling() const override
	{
		return m_flDpiScaling;
	}

	int GetDpiScalingSource() const override
	{
		return m_iDpiScalingSource;
	}

	bool IsHighDpiSupportEnabled() const
	{
		return m_bIsHighDpiSupported;
	}
};

static CDpiManager g_DpiManager;

IDpiManager* dpimanager()
{
	return &g_DpiManager;
}