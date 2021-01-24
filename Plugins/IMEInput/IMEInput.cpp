#include <metahook.h>
#include <keydefs.h>
#include <net_api.h>
#include <cvardef.h>
#include <imm.h>
#include "IMEInput.h"
#include "Encode.h"
#include "cmd.h"
#include "plugins.h"
#include "Input.h"

#pragma comment(lib, "imm32.lib")

HWND g_MainHwnd;
WNDPROC g_MainWndProc;
char g_szInputBuffer[4096];
char *g_pszInputCommand;
int *g_piEngineKeyDest;
int (*g_pfnVGuiWrap2_IsConsoleVisible)(void);
void (*g_pfnKey_Message)(int key);
int (*g_pfnDrawConsoleString)(int x, int y, char *string);

void IMEIN_KeyEvent(int key)
{
	if (key == K_ENTER || key == K_KP_ENTER)
	{
		static char command[276];
		sprintf(command, "%s \"%s\"", g_pszInputCommand, g_szInputBuffer);
		gEngfuncs.pfnClientCmd(command);
		g_szInputBuffer[0] = '\0';
		*g_piEngineKeyDest = 0;
	}
	else if (key == K_ESCAPE)
	{
		g_szInputBuffer[0] = '\0';
		*g_piEngineKeyDest = 0;
	}
}

int IMEIN_DrawConsoleString(int x, int y, char *string)
{
	int result = 0;

	if (*g_piEngineKeyDest == 1)
	{
		static int need = 0;

		if (string == g_pszInputCommand)
		{
			need = 3;
			result = g_pfnDrawConsoleString(x, y, string);
		}
		else if (need == 1)
			result = g_pfnDrawConsoleString(x, y, g_szInputBuffer);
		else
			result = g_pfnDrawConsoleString(x, y, string);

		need--;
	}
	else
		result = g_pfnDrawConsoleString(x, y, string);

	return result;
}

static bool s_bIMEComposing = false;

LRESULT CALLBACK MainWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
		case WM_COPYDATA:
		{
			gEngfuncs.pfnClientCmd((char *)(((COPYDATASTRUCT *)lParam)->lpData));
			gEngfuncs.pfnClientCmd("\n");
			return 1;
		}
	}

	return CallWindowProc(g_MainWndProc, hWnd, message, wParam, lParam);
}

void IMEIN_PatchNonASCIICheck(void)
{
	DWORD addr = (DWORD)g_pMetaHookAPI->SearchPattern((void *)((DWORD)gEngfuncs.pNetAPI->SetValueForKey + 0x1B0), 0xFF, "\x83\xFB\x20\x7C\x2A\x83\xFB\x7E", 8);

	if (addr)
	{
		g_pMetaHookAPI->WriteNOP((void *)addr, 10);
	}

	cvar_t *cl_name = gEngfuncs.pfnGetCvarPointer("name");

	if (cl_name)
	{
		cl_name->flags &= ~FCVAR_PRINTABLEONLY;
	}
}

void INEIN_InstallHook(void)
{
	EnumWindows([](HWND hwnd,LPARAM lParam
	)
	{
		DWORD pid = 0;
		if (GetWindowThreadProcessId(hwnd, &pid) && pid == GetCurrentProcessId())
		{
			char windowClass[256] = { 0 };
			RealGetWindowClassA(hwnd, windowClass, sizeof(windowClass));
			if (!strcmp(windowClass, "Valve001") || !strcmp(windowClass, "SDL_app"))
			{
				g_MainHwnd = hwnd;
				return FALSE;
			}
		}
		return TRUE;
	}, NULL);

	if (!g_MainHwnd)
		return;

	g_MainWndProc = (WNDPROC)GetWindowLong(g_MainHwnd, GWL_WNDPROC);
	SetWindowLong(g_MainHwnd, GWL_WNDPROC, (LONG)MainWndProc);

	return;

	for (cmd_function_t *cmd = (cmd_function_t *)gEngfuncs.GetFirstCmdFunctionHandle(); cmd; cmd = cmd->next)
	{
		if (!strcmp(cmd->name, "toggleconsole"))
		{
			DWORD addr = (DWORD)cmd->function + 0x1;
			g_pfnVGuiWrap2_IsConsoleVisible = (int (*)(void))(addr + *(DWORD *)addr + 0x4);
			continue;
		}

		/*if (!strcmp(cmd->name, "escape"))
		{
			DWORD addr = (DWORD)g_pMetaHookAPI->SearchPattern((void *)((DWORD)cmd->function + 0x40), 0x20, "\x6A\x1B\xE8", 3) + 0x3;
			g_pfnKey_Message = (void (*)(int))(addr + *(DWORD *)addr + 0x4);
			continue;
		}

		if (!strcmp(cmd->name, "messagemode"))
		{
			DWORD addr = (DWORD)g_pMetaHookAPI->SearchPattern((void *)((DWORD)cmd->function + 0x10), 0x50, "\x83\xC4\x04\x50\x68", 5) + 0x5;
			g_pszInputCommand = *(char **)addr;
			continue;
		}*/
	}

	IMEIN_PatchNonASCIICheck();

	g_szInputBuffer[0] = '\0';
	g_piEngineKeyDest = *(int **)((DWORD)g_pMetaHookAPI->SearchPattern((void *)((DWORD)gEngfuncs.VGui_ViewportPaintBackground + 0x10), 0x200, "\x85\xC0\x75\x2A\x83\x3D\x2A\x2A\x2A\x2A\x01", 10) + 0xE);
	g_pMetaHookAPI->InlineHook(g_pfnKey_Message, IMEIN_KeyEvent, (void *&)g_pfnKey_Message);
	g_pMetaHookAPI->InlineHook(gEngfuncs.pfnDrawConsoleString, IMEIN_DrawConsoleString, (void *&)g_pfnDrawConsoleString);
}