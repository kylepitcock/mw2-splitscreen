#pragma once
#include <windows.h>
#include <cstdint>

namespace ss
{
	void LogInit();
	void Log(const char* fmt, ...);

	// MinHook helpers. Both log and return false on failure.
	bool Hook(uintptr_t target, void* detour, void** original);
	bool HookApi(const wchar_t* module, const char* name, void* detour, void** original);

	template <typename T>
	bool Hook(uintptr_t target, T detour, T* original)
	{
		return Hook(target, reinterpret_cast<void*>(detour), reinterpret_cast<void**>(original));
	}

	// Overwrite code bytes (handles page protection + icache).
	void Patch(uintptr_t address, const void* bytes, size_t size);

	// Is this the iw4mp 1.2.159 executable our addresses belong to?
	bool IsSupportedGame();

	// Loads the system dsound.dll that our exports forward to.
	bool LoadRealDsound();
}
