#include "common.h"
#include <MinHook.h>
#include <cstdio>
#include <cstdarg>
#include <cwchar>

namespace ss
{
	Config g_cfg;

	namespace
	{
		HANDLE g_logFile = INVALID_HANDLE_VALUE;
		CRITICAL_SECTION g_logLock;

		std::wstring GetEnv(const wchar_t* name)
		{
			wchar_t buf[1024];
			const DWORD n = GetEnvironmentVariableW(name, buf, ARRAYSIZE(buf));
			if (n == 0 || n >= ARRAYSIZE(buf)) return {};
			return buf;
		}
	}

	void LoadConfig()
	{
		wchar_t exe[MAX_PATH];
		GetModuleFileNameW(nullptr, exe, MAX_PATH);
		g_cfg.gameDir = exe;
		g_cfg.gameDir.resize(g_cfg.gameDir.find_last_of(L"\\/"));

		const std::wstring slot = GetEnv(L"MW2SS_SLOT");
		if (slot.empty()) return;

		g_cfg.active = true;
		g_cfg.slot = _wtoi(slot.c_str());

		const std::wstring pad = GetEnv(L"MW2SS_PAD");
		g_cfg.pad = pad.empty() ? -1 : _wtoi(pad.c_str());
		if (g_cfg.pad > 3) g_cfg.pad = -1;

		g_cfg.kbm = GetEnv(L"MW2SS_KBM") == L"1";
		g_cfg.topmost = GetEnv(L"MW2SS_TOPMOST") != L"0";

		int x, y, w, h;
		if (swscanf_s(GetEnv(L"MW2SS_RECT").c_str(), L"%d,%d,%d,%d", &x, &y, &w, &h) == 4 && w > 0 && h > 0)
		{
			g_cfg.hasRect = true;
			g_cfg.rect = { x, y, x + w, y + h };
		}

		g_cfg.profileDir = GetEnv(L"MW2SS_PROFILE");
		while (!g_cfg.profileDir.empty() && (g_cfg.profileDir.back() == L'\\' || g_cfg.profileDir.back() == L'/'))
			g_cfg.profileDir.pop_back();

		// Opened before any file hooks exist, so logging never re-enters them.
		InitializeCriticalSection(&g_logLock);
		const std::wstring log = GetEnv(L"MW2SS_LOG");
		if (!log.empty())
		{
			g_logFile = CreateFileW(log.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
			                        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		}

		Log("mw2ss proxy %s: slot=%d pad=%d kbm=%d rect=%s(%ld,%ld %ldx%ld) profile=%ls", MW2SS_VERSION, g_cfg.slot,
		    g_cfg.pad, g_cfg.kbm, g_cfg.hasRect ? "" : "none", g_cfg.rect.left, g_cfg.rect.top,
		    g_cfg.rect.right - g_cfg.rect.left, g_cfg.rect.bottom - g_cfg.rect.top, g_cfg.profileDir.c_str());
	}

	void Log(const char* fmt, ...)
	{
		if (g_logFile == INVALID_HANDLE_VALUE) return;

		char buf[1024];
		SYSTEMTIME t;
		GetLocalTime(&t);
		int n = sprintf_s(buf, "[%02d:%02d:%02d.%03d][P%d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, g_cfg.slot);

		va_list ap;
		va_start(ap, fmt);
		const int m = vsnprintf(buf + n, sizeof(buf) - n - 2, fmt, ap);
		va_end(ap);
		n += m < 0 ? 0 : (m > static_cast<int>(sizeof(buf)) - n - 3 ? static_cast<int>(sizeof(buf)) - n - 3 : m);
		buf[n++] = '\r';
		buf[n++] = '\n';

		EnterCriticalSection(&g_logLock);
		DWORD written;
		WriteFile(g_logFile, buf, n, &written, nullptr);
		LeaveCriticalSection(&g_logLock);
	}

	bool HookAddress(void* target, void* detour, void** original)
	{
		MH_STATUS st = MH_CreateHook(target, detour, original);
		if (st == MH_OK) st = MH_EnableHook(target);
		if (st != MH_OK)
		{
			Log("hook at %p failed: %s", target, MH_StatusToString(st));
			return false;
		}
		return true;
	}

	bool HookApi(const wchar_t* module, const char* name, void* detour, void** original)
	{
		const HMODULE mod = GetModuleHandleW(module);
		void* target = mod ? reinterpret_cast<void*>(GetProcAddress(mod, name)) : nullptr;
		if (!target)
		{
			Log("hook %ls!%s: not found", module, name);
			return false;
		}
		const bool ok = HookAddress(target, detour, original);
		if (!ok) Log("  (was %ls!%s)", module, name);
		return ok;
	}
}
