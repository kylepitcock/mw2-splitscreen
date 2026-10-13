#include "util.hpp"
#include <MinHook.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>

namespace ss
{
	namespace
	{
		HANDLE g_log = INVALID_HANDLE_VALUE;
		CRITICAL_SECTION g_logLock;
	}

	void LogInit()
	{
		InitializeCriticalSection(&g_logLock);
		wchar_t path[MAX_PATH];
		GetModuleFileNameW(nullptr, path, MAX_PATH);
		wchar_t* slash = wcsrchr(path, L'\\');
		if (slash) wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"splitscreen.log");
		g_log = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	}

	void Log(const char* fmt, ...)
	{
		char buf[1024];
		SYSTEMTIME t;
		GetLocalTime(&t);
		int n = sprintf_s(buf, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
		va_list ap;
		va_start(ap, fmt);
		int m = vsnprintf(buf + n, sizeof(buf) - n - 3, fmt, ap);
		va_end(ap);
		n += m < 0 ? 0 : (m > static_cast<int>(sizeof(buf)) - n - 3 ? static_cast<int>(sizeof(buf)) - n - 3 : m);
		buf[n++] = '\r';
		buf[n++] = '\n';
		buf[n] = 0;

		EnterCriticalSection(&g_logLock);
		if (g_log != INVALID_HANDLE_VALUE)
		{
			DWORD w;
			WriteFile(g_log, buf, n, &w, nullptr);
		}
		LeaveCriticalSection(&g_logLock);
		OutputDebugStringA(buf);
	}

	bool Hook(uintptr_t target, void* detour, void** original)
	{
		const auto* b = reinterpret_cast<const unsigned char*>(target);
		Log("hooking %08X (bytes %02X %02X %02X %02X %02X%s)", static_cast<unsigned>(target), b[0], b[1], b[2], b[3], b[4],
		    b[0] == 0xE9 ? " - already hooked by IW4x, chaining" : "");
		MH_STATUS st = MH_CreateHook(reinterpret_cast<void*>(target), detour, original);
		if (st == MH_OK) st = MH_EnableHook(reinterpret_cast<void*>(target));
		if (st != MH_OK) Log("hook %08X failed: %s", static_cast<unsigned>(target), MH_StatusToString(st));
		return st == MH_OK;
	}

	bool HookApi(const wchar_t* module, const char* name, void* detour, void** original)
	{
		HMODULE mod = GetModuleHandleW(module);
		auto target = mod ? reinterpret_cast<uintptr_t>(GetProcAddress(mod, name)) : 0;
		if (!target)
		{
			Log("hook %ls!%s: not found", module, name);
			return false;
		}
		return Hook(target, detour, original);
	}

	void Patch(uintptr_t address, const void* bytes, size_t size)
	{
		DWORD old;
		VirtualProtect(reinterpret_cast<void*>(address), size, PAGE_EXECUTE_READWRITE, &old);
		std::memcpy(reinterpret_cast<void*>(address), bytes, size);
		VirtualProtect(reinterpret_cast<void*>(address), size, old, &old);
		FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(address), size);
	}

	bool IsSupportedGame()
	{
		// "CL_ConnectFromParty called for local client %i\n" lives here in 1.2.159 only.
		const char* probe = reinterpret_cast<const char*>(0x712E7C);
		MEMORY_BASIC_INFORMATION mbi{};
		if (!VirtualQuery(probe, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) return false;
		return std::strncmp(probe, "CL_ConnectFromParty called for local client", 43) == 0;
	}
}
