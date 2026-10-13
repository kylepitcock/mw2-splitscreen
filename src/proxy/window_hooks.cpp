// Window, focus and input isolation.
//
// Every instance is made to believe it is the active, foreground application
// (so it keeps rendering, polling its pad and playing audio), while real
// keyboard/mouse input only reaches the one instance that owns it - and only
// while its window genuinely has focus. Controller-only viewports can never be
// activated, so clicking them cannot steal the keyboard player's focus.
#include "common.h"
#include <unordered_map>
#include <cstring>
#include <cwctype>

namespace
{
	using namespace ss;

	decltype(&RegisterClassA) oRegisterClassA;
	decltype(&RegisterClassExA) oRegisterClassExA;
	decltype(&CreateWindowExA) oCreateWindowExA;
	decltype(&SetWindowLongA) oSetWindowLongA;
	decltype(&GetWindowLongA) oGetWindowLongA;
	decltype(&ShowWindow) oShowWindow;
	decltype(&SetForegroundWindow) oSetForegroundWindow;
	decltype(&SetActiveWindow) oSetActiveWindow;
	decltype(&SetFocus) oSetFocus;
	decltype(&BringWindowToTop) oBringWindowToTop;
	decltype(&GetForegroundWindow) oGetForegroundWindow;
	decltype(&GetActiveWindow) oGetActiveWindow;
	decltype(&GetFocus) oGetFocus;
	decltype(&GetCursorPos) oGetCursorPos;
	decltype(&SetCursorPos) oSetCursorPos;
	decltype(&ClipCursor) oClipCursor;
	decltype(&GetRawInputData) oGetRawInputData;
	decltype(&GetAsyncKeyState) oGetAsyncKeyState;
	decltype(&MessageBoxA) oMessageBoxA;
	decltype(&CreateMutexA) oCreateMutexA;
	decltype(&CreateMutexW) oCreateMutexW;
	decltype(&CreateMutexExA) oCreateMutexExA;
	decltype(&CreateMutexExW) oCreateMutexExW;
	decltype(&OpenMutexW) oOpenMutexW;

	constexpr DWORD kFrameStyles = WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX |
	                               WS_BORDER | WS_DLGFRAME | WS_MAXIMIZE | WS_MINIMIZE;
	constexpr DWORD kFrameExStyles = WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_DLGMODALFRAME | WS_EX_STATICEDGE;

	SRWLOCK g_lock = SRWLOCK_INIT;
	std::unordered_map<ATOM, WNDPROC> g_classProcs; // classes registered through us
	std::unordered_map<HWND, WNDPROC> g_innerProcs; // procs installed via SetWindowLong behind our wrapper

	HWND g_mainHwnd = nullptr;
	thread_local bool t_creatingMain = false;

	POINT g_lastCursor{};
	bool g_haveLastCursor = false;

	int Width() { return g_cfg.rect.right - g_cfg.rect.left; }
	int Height() { return g_cfg.rect.bottom - g_cfg.rect.top; }

	bool IsMain(HWND hwnd) { return hwnd && hwnd == g_mainHwnd; }

	// Real keyboard/mouse input is only allowed for the KBM owner while it truly has focus.
	bool InputAllowed()
	{
		return g_cfg.kbm && g_mainHwnd && oGetForegroundWindow() == g_mainHwnd;
	}

	bool IsInputMessage(UINT msg)
	{
		return (msg >= WM_KEYFIRST && msg <= WM_KEYLAST) || (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) ||
		       msg == WM_INPUT || (msg >= WM_NCMOUSEMOVE && msg <= WM_NCXBUTTONDBLCLK);
	}

	void OnRealDeactivate()
	{
		if (g_cfg.kbm) oClipCursor(nullptr);
	}

	void OnRealActivate()
	{
		if (g_cfg.kbm && g_haveLastCursor) oSetCursorPos(g_lastCursor.x, g_lastCursor.y);
	}

	WNDPROC InnerProc(HWND hwnd)
	{
		AcquireSRWLockShared(&g_lock);
		WNDPROC proc = nullptr;
		if (auto it = g_innerProcs.find(hwnd); it != g_innerProcs.end())
			proc = it->second;
		else if (auto ct = g_classProcs.find(static_cast<ATOM>(GetClassWord(hwnd, GCW_ATOM))); ct != g_classProcs.end())
			proc = ct->second;
		ReleaseSRWLockShared(&g_lock);
		return proc ? proc : DefWindowProcA;
	}

	// Returns true if the message was fully handled (result in *out); otherwise
	// wp/lp may have been rewritten and the message continues to the game.
	bool FilterMessage(HWND hwnd, UINT msg, WPARAM& wp, LPARAM& lp, LRESULT* out)
	{
		switch (msg)
		{
		case WM_ACTIVATEAPP:
			if (!wp)
			{
				OnRealDeactivate();
				wp = TRUE;
			}
			return false;

		case WM_ACTIVATE:
			if (LOWORD(wp) == WA_INACTIVE)
				OnRealDeactivate();
			else if (lp != reinterpret_cast<LPARAM>(hwnd))
				OnRealActivate();
			wp = WA_ACTIVE; // never inactive, never minimized
			return false;

		case WM_NCACTIVATE:
			wp = TRUE;
			return false;

		case WM_KILLFOCUS:
			OnRealDeactivate();
			*out = 0;
			return true;

		case WM_MOUSEACTIVATE:
			if (g_cfg.kbm) return false;
			*out = MA_NOACTIVATEANDEAT;
			return true;

		case WM_WINDOWPOSCHANGING:
			if (auto* wpos = reinterpret_cast<WINDOWPOS*>(lp); wpos && g_cfg.hasRect)
			{
				if (!(wpos->flags & SWP_NOMOVE))
				{
					wpos->x = g_cfg.rect.left;
					wpos->y = g_cfg.rect.top;
				}
				if (!(wpos->flags & SWP_NOSIZE))
				{
					wpos->cx = Width();
					wpos->cy = Height();
				}
				if (g_cfg.topmost && !(wpos->flags & SWP_NOZORDER)) wpos->hwndInsertAfter = HWND_TOPMOST;
				if (!g_cfg.kbm) wpos->flags |= SWP_NOACTIVATE;
			}
			return false;

		case WM_STYLECHANGING:
			if (auto* st = reinterpret_cast<STYLESTRUCT*>(lp); st && g_cfg.hasRect)
			{
				if (wp == static_cast<WPARAM>(GWL_STYLE))
					st->styleNew = (st->styleNew & ~kFrameStyles) | WS_POPUP;
				else if (wp == static_cast<WPARAM>(GWL_EXSTYLE))
					st->styleNew = (st->styleNew & ~kFrameExStyles) | (g_cfg.kbm ? 0 : WS_EX_NOACTIVATE);
			}
			return false;

		case WM_SYSCOMMAND:
			switch (wp & 0xFFF0)
			{
			case SC_MINIMIZE:
			case SC_MAXIMIZE:
			case SC_RESTORE:
			case SC_MOVE:
			case SC_SIZE:
				*out = 0;
				return true;
			}
			return false;

		case WM_SYSKEYDOWN:
			// Alt+Enter would switch the viewport to exclusive fullscreen.
			if (wp == VK_RETURN)
			{
				*out = 0;
				return true;
			}
			break;
		}

		if (IsInputMessage(msg) && !InputAllowed())
		{
			*out = msg == WM_INPUT ? DefWindowProcA(hwnd, msg, wp, lp) : 0;
			return true;
		}
		return false;
	}

	LRESULT CALLBACK WrapperProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
	{
		// The game's window gets its first messages from inside CreateWindowExA.
		if (t_creatingMain && !g_mainHwnd && !GetParent(hwnd)) g_mainHwnd = hwnd;

		const WNDPROC inner = InnerProc(hwnd);
		if (IsMain(hwnd))
		{
			LRESULT result = 0;
			if (FilterMessage(hwnd, msg, wp, lp, &result)) return result;
		}
		return CallWindowProcA(inner, hwnd, msg, wp, lp);
	}

	bool IsWrapped(HWND hwnd)
	{
		return reinterpret_cast<WNDPROC>(oGetWindowLongA(hwnd, GWL_WNDPROC)) == WrapperProc;
	}

	ATOM WINAPI hkRegisterClassA(const WNDCLASSA* wc)
	{
		if (!wc || !wc->lpfnWndProc) return oRegisterClassA(wc);
		WNDCLASSA copy = *wc;
		copy.lpfnWndProc = WrapperProc;
		const ATOM atom = oRegisterClassA(&copy);
		if (atom)
		{
			AcquireSRWLockExclusive(&g_lock);
			g_classProcs[atom] = wc->lpfnWndProc;
			ReleaseSRWLockExclusive(&g_lock);
		}
		return atom;
	}

	ATOM WINAPI hkRegisterClassExA(const WNDCLASSEXA* wc)
	{
		if (!wc || !wc->lpfnWndProc) return oRegisterClassExA(wc);
		WNDCLASSEXA copy = *wc;
		copy.lpfnWndProc = WrapperProc;
		const ATOM atom = oRegisterClassExA(&copy);
		if (atom)
		{
			AcquireSRWLockExclusive(&g_lock);
			g_classProcs[atom] = wc->lpfnWndProc;
			ReleaseSRWLockExclusive(&g_lock);
		}
		return atom;
	}

	void FakeActivate(HWND hwnd)
	{
		SendMessageA(hwnd, WM_ACTIVATEAPP, TRUE, 0);
		SendMessageA(hwnd, WM_ACTIVATE, WA_ACTIVE, reinterpret_cast<LPARAM>(hwnd));
		SendMessageA(hwnd, WM_SETFOCUS, 0, 0);
	}

	bool IsGameWindowTitle(const char* name)
	{
		if (!name || IS_INTRESOURCE(name)) return false;
		if (std::strstr(name, "onsole")) return false;
		return std::strstr(name, "Modern Warfare 2") != nullptr;
	}

	HWND WINAPI hkCreateWindowExA(DWORD exStyle, LPCSTR cls, LPCSTR name, DWORD style, int x, int y, int w, int h,
	                             HWND parent, HMENU menu, HINSTANCE inst, LPVOID param)
	{
		const bool isMain = !g_mainHwnd && !parent && !(style & WS_CHILD) && IsGameWindowTitle(name);
		if (!isMain) return oCreateWindowExA(exStyle, cls, name, style, x, y, w, h, parent, menu, inst, param);

		if (g_cfg.hasRect)
		{
			x = g_cfg.rect.left;
			y = g_cfg.rect.top;
			w = Width();
			h = Height();
			style = (style & ~kFrameStyles) | WS_POPUP;
			exStyle &= ~kFrameExStyles;
			if (g_cfg.topmost) exStyle |= WS_EX_TOPMOST;
		}
		if (!g_cfg.kbm) exStyle |= WS_EX_NOACTIVATE;

		t_creatingMain = true;
		const HWND hwnd = oCreateWindowExA(exStyle, cls, name, style, x, y, w, h, parent, menu, inst, param);
		t_creatingMain = false;
		if (!hwnd) return hwnd;

		g_mainHwnd = hwnd;
		if (!IsWrapped(hwnd))
		{
			// Class wasn't registered through us; subclass it instead.
			const auto prev = reinterpret_cast<WNDPROC>(
				oSetWindowLongA(hwnd, GWL_WNDPROC, reinterpret_cast<LONG>(WrapperProc)));
			AcquireSRWLockExclusive(&g_lock);
			g_innerProcs[hwnd] = prev;
			ReleaseSRWLockExclusive(&g_lock);
		}

		Log("game window %p created (%dx%d at %d,%d, requested by game: %dx%d)", hwnd, Width(), Height(),
		    g_cfg.rect.left, g_cfg.rect.top, w, h);

		if (g_cfg.hasRect)
		{
			SetWindowPos(hwnd, g_cfg.topmost ? HWND_TOPMOST : HWND_TOP, g_cfg.rect.left, g_cfg.rect.top, Width(),
			             Height(), SWP_FRAMECHANGED | (g_cfg.kbm ? 0 : SWP_NOACTIVATE));
		}
		if (g_cfg.kbm) oSetForegroundWindow(hwnd);
		FakeActivate(hwnd);
		return hwnd;
	}

	LONG WINAPI hkSetWindowLongA(HWND hwnd, int index, LONG value)
	{
		// Keep our wrapper outermost: a later subclass goes *behind* it.
		if (index == GWL_WNDPROC && IsWrapped(hwnd))
		{
			AcquireSRWLockExclusive(&g_lock);
			WNDPROC prev = nullptr;
			if (auto it = g_innerProcs.find(hwnd); it != g_innerProcs.end())
				prev = it->second;
			else if (auto ct = g_classProcs.find(static_cast<ATOM>(GetClassWord(hwnd, GCW_ATOM))); ct != g_classProcs.end())
				prev = ct->second;
			g_innerProcs[hwnd] = reinterpret_cast<WNDPROC>(value);
			ReleaseSRWLockExclusive(&g_lock);
			return reinterpret_cast<LONG>(prev);
		}
		return oSetWindowLongA(hwnd, index, value);
	}

	LONG WINAPI hkGetWindowLongA(HWND hwnd, int index)
	{
		if (index == GWL_WNDPROC && IsWrapped(hwnd)) return reinterpret_cast<LONG>(InnerProc(hwnd));
		return oGetWindowLongA(hwnd, index);
	}

	BOOL WINAPI hkShowWindow(HWND hwnd, int cmd)
	{
		if (IsMain(hwnd))
		{
			if (cmd == SW_MINIMIZE || cmd == SW_SHOWMINIMIZED || cmd == SW_SHOWMINNOACTIVE || cmd == SW_FORCEMINIMIZE ||
			    cmd == SW_MAXIMIZE)
				cmd = g_cfg.kbm ? SW_SHOWNORMAL : SW_SHOWNOACTIVATE;
			else if (!g_cfg.kbm && cmd != SW_HIDE)
				cmd = SW_SHOWNOACTIVATE;
		}
		return oShowWindow(hwnd, cmd);
	}

	BOOL WINAPI hkSetForegroundWindow(HWND hwnd)
	{
		if (IsMain(hwnd) && !g_cfg.kbm) return TRUE;
		return oSetForegroundWindow(hwnd);
	}

	HWND WINAPI hkSetActiveWindow(HWND hwnd)
	{
		if (IsMain(hwnd) && !g_cfg.kbm) return hwnd;
		return oSetActiveWindow(hwnd);
	}

	HWND WINAPI hkSetFocus(HWND hwnd)
	{
		if (IsMain(hwnd) && !g_cfg.kbm) return hwnd;
		return oSetFocus(hwnd);
	}

	BOOL WINAPI hkBringWindowToTop(HWND hwnd)
	{
		if (IsMain(hwnd) && !g_cfg.kbm) return TRUE;
		return oBringWindowToTop(hwnd);
	}

	HWND WINAPI hkGetForegroundWindow() { return g_mainHwnd ? g_mainHwnd : oGetForegroundWindow(); }
	HWND WINAPI hkGetActiveWindow() { return g_mainHwnd ? g_mainHwnd : oGetActiveWindow(); }
	HWND WINAPI hkGetFocus() { return g_mainHwnd ? g_mainHwnd : oGetFocus(); }

	BOOL WINAPI hkGetCursorPos(LPPOINT pt)
	{
		if (!g_mainHwnd || !pt || InputAllowed()) return oGetCursorPos(pt);

		// Report wherever the game last parked the cursor: mouse delta stays zero.
		if (g_haveLastCursor)
		{
			*pt = g_lastCursor;
		}
		else
		{
			RECT r;
			GetWindowRect(g_mainHwnd, &r);
			pt->x = (r.left + r.right) / 2;
			pt->y = (r.top + r.bottom) / 2;
		}
		return TRUE;
	}

	BOOL WINAPI hkSetCursorPos(int x, int y)
	{
		if (!g_mainHwnd) return oSetCursorPos(x, y);
		g_lastCursor = { x, y };
		g_haveLastCursor = true;
		return InputAllowed() ? oSetCursorPos(x, y) : TRUE;
	}

	BOOL WINAPI hkClipCursor(const RECT* rect)
	{
		if (!g_mainHwnd || !rect || InputAllowed()) return oClipCursor(rect);
		return TRUE;
	}

	UINT WINAPI hkGetRawInputData(HRAWINPUT raw, UINT cmd, LPVOID data, PUINT size, UINT header)
	{
		const UINT r = oGetRawInputData(raw, cmd, data, size, header);
		if (data && cmd == RID_INPUT && r != static_cast<UINT>(-1) && r >= sizeof(RAWINPUTHEADER) && !InputAllowed())
		{
			auto* ri = static_cast<RAWINPUT*>(data);
			ri->header.dwType = RIM_TYPEHID; // the game ignores HID reports
			std::memset(&ri->data, 0, r - sizeof(RAWINPUTHEADER));
		}
		return r;
	}

	SHORT WINAPI hkGetAsyncKeyState(int key)
	{
		if (g_mainHwnd && !InputAllowed()) return 0;
		return oGetAsyncKeyState(key);
	}

	int WINAPI hkMessageBoxA(HWND hwnd, LPCSTR text, LPCSTR caption, UINT type)
	{
		if (text && std::strstr(text, "another instance"))
		{
			Log("auto-accepted '%s'", text);
			return IDYES;
		}
		return oMessageBoxA(hwnd, text, caption, type);
	}

	template <typename Char>
	bool ContainsNoCase(const Char* s, const char* needle)
	{
		for (; *s; ++s)
		{
			size_t i = 0;
			while (needle[i] && s[i] && std::towlower(static_cast<wint_t>(s[i])) == static_cast<wint_t>(needle[i])) ++i;
			if (!needle[i]) return true;
		}
		return false;
	}

	// IW4x refuses to run twice ("iw4x_mutex"); give each viewport its own mutex namespace.
	// The A/W/Ex variants call each other inside kernelbase, so renaming must be idempotent.
	template <typename Char>
	bool ShouldRename(const Char* name)
	{
		return name && ContainsNoCase(name, "iw4") && !ContainsNoCase(name, "_mw2ss");
	}

	std::string RenamedA(const char* name) { return std::string(name) + "_mw2ss" + std::to_string(g_cfg.slot); }
	std::wstring RenamedW(const wchar_t* name) { return std::wstring(name) + L"_mw2ss" + std::to_wstring(g_cfg.slot); }

	HANDLE WINAPI hkCreateMutexA(LPSECURITY_ATTRIBUTES sa, BOOL owner, LPCSTR name)
	{
		if (!ShouldRename(name)) return oCreateMutexA(sa, owner, name);
		return oCreateMutexA(sa, owner, RenamedA(name).c_str());
	}

	HANDLE WINAPI hkCreateMutexW(LPSECURITY_ATTRIBUTES sa, BOOL owner, LPCWSTR name)
	{
		if (!ShouldRename(name)) return oCreateMutexW(sa, owner, name);
		return oCreateMutexW(sa, owner, RenamedW(name).c_str());
	}

	HANDLE WINAPI hkCreateMutexExA(LPSECURITY_ATTRIBUTES sa, LPCSTR name, DWORD flags, DWORD access)
	{
		if (!ShouldRename(name)) return oCreateMutexExA(sa, name, flags, access);
		return oCreateMutexExA(sa, RenamedA(name).c_str(), flags, access);
	}

	HANDLE WINAPI hkCreateMutexExW(LPSECURITY_ATTRIBUTES sa, LPCWSTR name, DWORD flags, DWORD access)
	{
		if (!ShouldRename(name)) return oCreateMutexExW(sa, name, flags, access);
		return oCreateMutexExW(sa, RenamedW(name).c_str(), flags, access);
	}

	HANDLE WINAPI hkOpenMutexW(DWORD access, BOOL inherit, LPCWSTR name)
	{
		if (!ShouldRename(name)) return oOpenMutexW(access, inherit, name);
		return oOpenMutexW(access, inherit, RenamedW(name).c_str());
	}

	template <typename T>
	void Hook(const wchar_t* module, const char* name, T detour, T& original)
	{
		HookApi(module, name, reinterpret_cast<void*>(detour), reinterpret_cast<void**>(&original));
	}
}

namespace ss
{
	void InstallWindowHooks()
	{
		// Match IW4x: physical pixels everywhere, so the launcher's rectangles line up.
		if (const HMODULE user32 = GetModuleHandleW(L"user32.dll"))
		{
			using SetCtx_t = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
			if (auto set = reinterpret_cast<SetCtx_t>(GetProcAddress(user32, "SetProcessDpiAwarenessContext")))
				set(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
		}

		const wchar_t* u = L"user32.dll";
		Hook(u, "RegisterClassA", hkRegisterClassA, oRegisterClassA);
		Hook(u, "RegisterClassExA", hkRegisterClassExA, oRegisterClassExA);
		Hook(u, "CreateWindowExA", hkCreateWindowExA, oCreateWindowExA);
		Hook(u, "SetWindowLongA", hkSetWindowLongA, oSetWindowLongA);
		Hook(u, "GetWindowLongA", hkGetWindowLongA, oGetWindowLongA);
		Hook(u, "ShowWindow", hkShowWindow, oShowWindow);
		Hook(u, "SetForegroundWindow", hkSetForegroundWindow, oSetForegroundWindow);
		Hook(u, "SetActiveWindow", hkSetActiveWindow, oSetActiveWindow);
		Hook(u, "SetFocus", hkSetFocus, oSetFocus);
		Hook(u, "BringWindowToTop", hkBringWindowToTop, oBringWindowToTop);
		Hook(u, "GetForegroundWindow", hkGetForegroundWindow, oGetForegroundWindow);
		Hook(u, "GetActiveWindow", hkGetActiveWindow, oGetActiveWindow);
		Hook(u, "GetFocus", hkGetFocus, oGetFocus);
		Hook(u, "GetCursorPos", hkGetCursorPos, oGetCursorPos);
		Hook(u, "SetCursorPos", hkSetCursorPos, oSetCursorPos);
		Hook(u, "ClipCursor", hkClipCursor, oClipCursor);
		Hook(u, "GetRawInputData", hkGetRawInputData, oGetRawInputData);
		Hook(u, "GetAsyncKeyState", hkGetAsyncKeyState, oGetAsyncKeyState);
		Hook(u, "MessageBoxA", hkMessageBoxA, oMessageBoxA);

		const wchar_t* kb = GetModuleHandleW(L"kernelbase.dll") ? L"kernelbase.dll" : L"kernel32.dll";
		Hook(kb, "CreateMutexA", hkCreateMutexA, oCreateMutexA);
		Hook(kb, "CreateMutexW", hkCreateMutexW, oCreateMutexW);
		Hook(kb, "CreateMutexExA", hkCreateMutexExA, oCreateMutexExA);
		Hook(kb, "CreateMutexExW", hkCreateMutexExW, oCreateMutexExW);
		Hook(kb, "OpenMutexW", hkOpenMutexW, oOpenMutexW);
	}
}
