// Redirects <game>\players\... to a per-player profile directory.
//
// Each IW4x instance needs its own guid.dat (the server rejects duplicate
// GUIDs), its own config and its own stats, and must never overwrite the
// user's real profile with splitscreen-sized video settings.
#include "common.h"

namespace
{
	using namespace ss;

	std::wstring g_prefix; // "<game>\players"

	bool Translate(const wchar_t* in, std::wstring& out)
	{
		if (!in || !*in) return false;

		wchar_t full[1024];
		const DWORD n = GetFullPathNameW(in, ARRAYSIZE(full), full, nullptr);
		const DWORD pl = static_cast<DWORD>(g_prefix.size());
		if (n == 0 || n >= ARRAYSIZE(full) || n < pl) return false;
		if (CompareStringOrdinal(full, pl, g_prefix.c_str(), pl, TRUE) != CSTR_EQUAL) return false;
		if (full[pl] != L'\0' && full[pl] != L'\\') return false;

		out = g_cfg.profileDir + (full + pl);
		return true;
	}

	bool TranslateA(const char* in, std::string& out)
	{
		if (!in || !*in) return false;
		wchar_t wide[1024];
		if (!MultiByteToWideChar(CP_ACP, 0, in, -1, wide, ARRAYSIZE(wide))) return false;
		std::wstring w;
		if (!Translate(wide, w)) return false;
		const int len = WideCharToMultiByte(CP_ACP, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
		if (len <= 0) return false;
		out.resize(len - 1);
		WideCharToMultiByte(CP_ACP, 0, w.c_str(), -1, out.data(), len, nullptr, nullptr);
		return true;
	}

	// Path argument -> translated path, falling back to the original pointer.
	struct PW
	{
		std::wstring buf;
		const wchar_t* p;
		explicit PW(const wchar_t* in) : p(in) { if (Translate(in, buf)) p = buf.c_str(); }
		operator const wchar_t*() const { return p; }
	};
	struct PA
	{
		std::string buf;
		const char* p;
		explicit PA(const char* in) : p(in) { if (TranslateA(in, buf)) p = buf.c_str(); }
		operator const char*() const { return p; }
	};

	decltype(&CreateFileW) oCreateFileW;
	decltype(&CreateFileA) oCreateFileA;
	decltype(&GetFileAttributesW) oGetFileAttributesW;
	decltype(&GetFileAttributesA) oGetFileAttributesA;
	decltype(&GetFileAttributesExW) oGetFileAttributesExW;
	decltype(&GetFileAttributesExA) oGetFileAttributesExA;
	decltype(&SetFileAttributesW) oSetFileAttributesW;
	decltype(&SetFileAttributesA) oSetFileAttributesA;
	decltype(&FindFirstFileExW) oFindFirstFileExW;
	decltype(&FindFirstFileExA) oFindFirstFileExA;
	decltype(&FindFirstFileW) oFindFirstFileW;
	decltype(&FindFirstFileA) oFindFirstFileA;
	decltype(&CreateDirectoryW) oCreateDirectoryW;
	decltype(&CreateDirectoryA) oCreateDirectoryA;
	decltype(&RemoveDirectoryW) oRemoveDirectoryW;
	decltype(&RemoveDirectoryA) oRemoveDirectoryA;
	decltype(&DeleteFileW) oDeleteFileW;
	decltype(&DeleteFileA) oDeleteFileA;
	decltype(&MoveFileExW) oMoveFileExW;
	decltype(&MoveFileExA) oMoveFileExA;
	decltype(&MoveFileWithProgressW) oMoveFileWithProgressW;
	decltype(&CopyFileW) oCopyFileW;
	decltype(&CopyFileA) oCopyFileA;
	decltype(&CopyFileExW) oCopyFileExW;
	decltype(&ReplaceFileW) oReplaceFileW;

	HANDLE WINAPI hkCreateFileW(LPCWSTR f, DWORD a, DWORD s, LPSECURITY_ATTRIBUTES sa, DWORD c, DWORD fl, HANDLE t)
	{
		return oCreateFileW(PW(f), a, s, sa, c, fl, t);
	}
	HANDLE WINAPI hkCreateFileA(LPCSTR f, DWORD a, DWORD s, LPSECURITY_ATTRIBUTES sa, DWORD c, DWORD fl, HANDLE t)
	{
		return oCreateFileA(PA(f), a, s, sa, c, fl, t);
	}
	DWORD WINAPI hkGetFileAttributesW(LPCWSTR f) { return oGetFileAttributesW(PW(f)); }
	DWORD WINAPI hkGetFileAttributesA(LPCSTR f) { return oGetFileAttributesA(PA(f)); }
	BOOL WINAPI hkGetFileAttributesExW(LPCWSTR f, GET_FILEEX_INFO_LEVELS l, LPVOID i)
	{
		return oGetFileAttributesExW(PW(f), l, i);
	}
	BOOL WINAPI hkGetFileAttributesExA(LPCSTR f, GET_FILEEX_INFO_LEVELS l, LPVOID i)
	{
		return oGetFileAttributesExA(PA(f), l, i);
	}
	BOOL WINAPI hkSetFileAttributesW(LPCWSTR f, DWORD a) { return oSetFileAttributesW(PW(f), a); }
	BOOL WINAPI hkSetFileAttributesA(LPCSTR f, DWORD a) { return oSetFileAttributesA(PA(f), a); }
	HANDLE WINAPI hkFindFirstFileExW(LPCWSTR f, FINDEX_INFO_LEVELS l, LPVOID d, FINDEX_SEARCH_OPS o, LPVOID p, DWORD fl)
	{
		return oFindFirstFileExW(PW(f), l, d, o, p, fl);
	}
	HANDLE WINAPI hkFindFirstFileExA(LPCSTR f, FINDEX_INFO_LEVELS l, LPVOID d, FINDEX_SEARCH_OPS o, LPVOID p, DWORD fl)
	{
		return oFindFirstFileExA(PA(f), l, d, o, p, fl);
	}
	HANDLE WINAPI hkFindFirstFileW(LPCWSTR f, LPWIN32_FIND_DATAW d) { return oFindFirstFileW(PW(f), d); }
	HANDLE WINAPI hkFindFirstFileA(LPCSTR f, LPWIN32_FIND_DATAA d) { return oFindFirstFileA(PA(f), d); }
	BOOL WINAPI hkCreateDirectoryW(LPCWSTR f, LPSECURITY_ATTRIBUTES sa) { return oCreateDirectoryW(PW(f), sa); }
	BOOL WINAPI hkCreateDirectoryA(LPCSTR f, LPSECURITY_ATTRIBUTES sa) { return oCreateDirectoryA(PA(f), sa); }
	BOOL WINAPI hkRemoveDirectoryW(LPCWSTR f) { return oRemoveDirectoryW(PW(f)); }
	BOOL WINAPI hkRemoveDirectoryA(LPCSTR f) { return oRemoveDirectoryA(PA(f)); }
	BOOL WINAPI hkDeleteFileW(LPCWSTR f) { return oDeleteFileW(PW(f)); }
	BOOL WINAPI hkDeleteFileA(LPCSTR f) { return oDeleteFileA(PA(f)); }
	BOOL WINAPI hkMoveFileExW(LPCWSTR f, LPCWSTR t, DWORD fl) { return oMoveFileExW(PW(f), PW(t), fl); }
	BOOL WINAPI hkMoveFileExA(LPCSTR f, LPCSTR t, DWORD fl) { return oMoveFileExA(PA(f), PA(t), fl); }
	BOOL WINAPI hkMoveFileWithProgressW(LPCWSTR f, LPCWSTR t, LPPROGRESS_ROUTINE r, LPVOID d, DWORD fl)
	{
		return oMoveFileWithProgressW(PW(f), PW(t), r, d, fl);
	}
	BOOL WINAPI hkCopyFileW(LPCWSTR f, LPCWSTR t, BOOL x) { return oCopyFileW(PW(f), PW(t), x); }
	BOOL WINAPI hkCopyFileA(LPCSTR f, LPCSTR t, BOOL x) { return oCopyFileA(PA(f), PA(t), x); }
	BOOL WINAPI hkCopyFileExW(LPCWSTR f, LPCWSTR t, LPPROGRESS_ROUTINE r, LPVOID d, LPBOOL c, DWORD fl)
	{
		return oCopyFileExW(PW(f), PW(t), r, d, c, fl);
	}
	BOOL WINAPI hkReplaceFileW(LPCWSTR r, LPCWSTR w, LPCWSTR b, DWORD fl, LPVOID e, LPVOID x)
	{
		return oReplaceFileW(PW(r), PW(w), b ? PW(b).p : nullptr, fl, e, x);
	}

	// Prefer the kernelbase implementation (catches calls routed through kernel32
	// stubs too); fall back to kernel32 for functions that only live there.
	template <typename T>
	void Hook(const char* name, T detour, T& original)
	{
		const HMODULE kb = GetModuleHandleW(L"kernelbase.dll");
		const wchar_t* mod = kb && GetProcAddress(kb, name) ? L"kernelbase.dll" : L"kernel32.dll";
		HookApi(mod, name, reinterpret_cast<void*>(detour), reinterpret_cast<void**>(&original));
	}
}

namespace ss
{
	void InstallFileRedirect()
	{
		if (g_cfg.profileDir.empty()) return;

		wchar_t full[1024];
		if (!GetFullPathNameW((g_cfg.gameDir + L"\\players").c_str(), ARRAYSIZE(full), full, nullptr)) return;
		g_prefix = full;
		CreateDirectoryW(g_cfg.profileDir.c_str(), nullptr);
		Log("redirecting %ls -> %ls", g_prefix.c_str(), g_cfg.profileDir.c_str());

		Hook("CreateFileW", hkCreateFileW, oCreateFileW);
		Hook("CreateFileA", hkCreateFileA, oCreateFileA);
		Hook("GetFileAttributesW", hkGetFileAttributesW, oGetFileAttributesW);
		Hook("GetFileAttributesA", hkGetFileAttributesA, oGetFileAttributesA);
		Hook("GetFileAttributesExW", hkGetFileAttributesExW, oGetFileAttributesExW);
		Hook("GetFileAttributesExA", hkGetFileAttributesExA, oGetFileAttributesExA);
		Hook("SetFileAttributesW", hkSetFileAttributesW, oSetFileAttributesW);
		Hook("SetFileAttributesA", hkSetFileAttributesA, oSetFileAttributesA);
		Hook("FindFirstFileExW", hkFindFirstFileExW, oFindFirstFileExW);
		Hook("FindFirstFileExA", hkFindFirstFileExA, oFindFirstFileExA);
		Hook("FindFirstFileW", hkFindFirstFileW, oFindFirstFileW);
		Hook("FindFirstFileA", hkFindFirstFileA, oFindFirstFileA);
		Hook("CreateDirectoryW", hkCreateDirectoryW, oCreateDirectoryW);
		Hook("CreateDirectoryA", hkCreateDirectoryA, oCreateDirectoryA);
		Hook("RemoveDirectoryW", hkRemoveDirectoryW, oRemoveDirectoryW);
		Hook("RemoveDirectoryA", hkRemoveDirectoryA, oRemoveDirectoryA);
		Hook("DeleteFileW", hkDeleteFileW, oDeleteFileW);
		Hook("DeleteFileA", hkDeleteFileA, oDeleteFileA);
		Hook("MoveFileExW", hkMoveFileExW, oMoveFileExW);
		Hook("MoveFileExA", hkMoveFileExA, oMoveFileExA);
		Hook("MoveFileWithProgressW", hkMoveFileWithProgressW, oMoveFileWithProgressW);
		Hook("CopyFileW", hkCopyFileW, oCopyFileW);
		Hook("CopyFileA", hkCopyFileA, oCopyFileA);
		Hook("CopyFileExW", hkCopyFileExW, oCopyFileExW);
		Hook("ReplaceFileW", hkReplaceFileW, oReplaceFileW);
	}
}
