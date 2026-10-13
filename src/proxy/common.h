#pragma once
#include <windows.h>
#include <string>

#define MW2SS_VERSION "1.0.0"

namespace ss
{
	// Per-instance settings, passed by the launcher through environment variables:
	//   MW2SS_SLOT     1..4   (absent = proxy is a plain xinput pass-through)
	//   MW2SS_PAD      XInput user index driving this instance, -1 for none
	//   MW2SS_KBM      1 if this instance owns the keyboard and mouse
	//   MW2SS_RECT     x,y,w,h of the viewport in physical screen pixels
	//   MW2SS_PROFILE  directory that replaces <game>\players for this instance
	//   MW2SS_TOPMOST  1 to keep the viewport above the taskbar
	//   MW2SS_LOG      optional log file
	struct Config
	{
		bool active = false;
		int slot = 0;
		int pad = -1;
		bool kbm = false;
		bool hasRect = false;
		RECT rect{};
		bool topmost = true;
		std::wstring profileDir;
		std::wstring gameDir;
	};

	extern Config g_cfg;

	void LoadConfig();
	void Log(const char* fmt, ...);

	// MinHook helper: hooks `module!name`, returns false (and logs) on failure.
	bool HookApi(const wchar_t* module, const char* name, void* detour, void** original);
	bool HookAddress(void* target, void* detour, void** original);

	void InstallWindowHooks();
	void InstallFileRedirect();
	void InstallD3DHooks();
}
