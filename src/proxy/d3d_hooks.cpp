// Direct3D 9 hooks.
//
// The engine only accepts an r_mode that the adapter reports, so a split
// viewport such as 2560x720 is normally rejected. We add the viewport size to
// the adapter's mode list, and force the device to stay windowed.
#include "common.h"
#include <d3d9.h>
#include <map>
#include <mutex>

namespace
{
	using namespace ss;

	using Create9_t = IDirect3D9*(WINAPI*)(UINT);
	using Create9Ex_t = HRESULT(WINAPI*)(UINT, IDirect3D9Ex**);
	using GetModeCount_t = UINT(STDMETHODCALLTYPE*)(IDirect3D9*, UINT, D3DFORMAT);
	using EnumModes_t = HRESULT(STDMETHODCALLTYPE*)(IDirect3D9*, UINT, D3DFORMAT, UINT, D3DDISPLAYMODE*);
	using CreateDevice_t = HRESULT(STDMETHODCALLTYPE*)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD,
	                                                   D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
	using CreateDeviceEx_t = HRESULT(STDMETHODCALLTYPE*)(IDirect3D9Ex*, UINT, D3DDEVTYPE, HWND, DWORD,
	                                                     D3DPRESENT_PARAMETERS*, D3DDISPLAYMODEEX*, IDirect3DDevice9Ex**);
	using Reset_t = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
	using ResetEx_t = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9Ex*, D3DPRESENT_PARAMETERS*, D3DDISPLAYMODEEX*);

	// vtable slots
	constexpr int kGetAdapterModeCount = 6;
	constexpr int kEnumAdapterModes = 7;
	constexpr int kCreateDevice = 16;
	constexpr int kCreateDeviceEx = 20;
	constexpr int kReset = 16;
	constexpr int kResetEx = 132;

	Create9_t oDirect3DCreate9;
	Create9Ex_t oDirect3DCreate9Ex;

	std::mutex g_lock;
	std::map<std::pair<void**, int>, void*> g_originals; // (vtable, slot) -> original

	// Vtable patching rather than inline hooks: d3d9 and d3d9ex objects may or
	// may not share implementations, and this keeps one original per vtable.
	void PatchSlot(void* object, int slot, void* detour)
	{
		void** vtbl = *static_cast<void***>(object);
		std::lock_guard<std::mutex> lock(g_lock);
		if (vtbl[slot] == detour || g_originals.count({ vtbl, slot })) return;

		DWORD old;
		if (!VirtualProtect(&vtbl[slot], sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) return;
		g_originals[{ vtbl, slot }] = vtbl[slot];
		vtbl[slot] = detour;
		VirtualProtect(&vtbl[slot], sizeof(void*), old, &old);
	}

	template <typename T>
	T Original(void* object, int slot)
	{
		void** vtbl = *static_cast<void***>(object);
		std::lock_guard<std::mutex> lock(g_lock);
		auto it = g_originals.find({ vtbl, slot });
		return it == g_originals.end() ? nullptr : reinterpret_cast<T>(it->second);
	}

	UINT ViewW() { return static_cast<UINT>(g_cfg.rect.right - g_cfg.rect.left); }
	UINT ViewH() { return static_cast<UINT>(g_cfg.rect.bottom - g_cfg.rect.top); }

	bool AdapterHasViewportMode(IDirect3D9* self, UINT adapter, D3DFORMAT fmt, UINT count)
	{
		const auto enumModes = Original<EnumModes_t>(self, kEnumAdapterModes);
		for (UINT i = 0; i < count; ++i)
		{
			D3DDISPLAYMODE m{};
			if (enumModes(self, adapter, fmt, i, &m) == D3D_OK && m.Width == ViewW() && m.Height == ViewH()) return true;
		}
		return false;
	}

	UINT STDMETHODCALLTYPE hkGetAdapterModeCount(IDirect3D9* self, UINT adapter, D3DFORMAT fmt)
	{
		const UINT n = Original<GetModeCount_t>(self, kGetAdapterModeCount)(self, adapter, fmt);
		if (!g_cfg.hasRect || n == 0 || AdapterHasViewportMode(self, adapter, fmt, n)) return n;
		return n + 1;
	}

	HRESULT STDMETHODCALLTYPE hkEnumAdapterModes(IDirect3D9* self, UINT adapter, D3DFORMAT fmt, UINT mode,
	                                             D3DDISPLAYMODE* out)
	{
		const auto enumModes = Original<EnumModes_t>(self, kEnumAdapterModes);
		const UINT n = Original<GetModeCount_t>(self, kGetAdapterModeCount)(self, adapter, fmt);
		if (mode < n || !g_cfg.hasRect || n == 0) return enumModes(self, adapter, fmt, mode, out);
		if (mode > n || !out || AdapterHasViewportMode(self, adapter, fmt, n)) return D3DERR_INVALIDCALL;

		// Synthetic mode, using the refresh rate of the last real one.
		D3DDISPLAYMODE last{};
		enumModes(self, adapter, fmt, n - 1, &last);
		out->Width = ViewW();
		out->Height = ViewH();
		out->RefreshRate = last.RefreshRate ? last.RefreshRate : 60;
		out->Format = fmt;
		return D3D_OK;
	}

	void FixPresentParams(D3DPRESENT_PARAMETERS* pp)
	{
		if (!pp || !g_cfg.hasRect) return;
		if (!pp->Windowed) Log("forcing windowed device (game requested fullscreen)");
		pp->Windowed = TRUE;
		pp->FullScreen_RefreshRateInHz = 0;
		Log("device back buffer %ux%u (viewport %ux%u)", pp->BackBufferWidth, pp->BackBufferHeight, ViewW(), ViewH());
	}

	HRESULT STDMETHODCALLTYPE hkReset(IDirect3DDevice9* self, D3DPRESENT_PARAMETERS* pp)
	{
		FixPresentParams(pp);
		return Original<Reset_t>(self, kReset)(self, pp);
	}

	HRESULT STDMETHODCALLTYPE hkResetEx(IDirect3DDevice9Ex* self, D3DPRESENT_PARAMETERS* pp, D3DDISPLAYMODEEX* fs)
	{
		FixPresentParams(pp);
		return Original<ResetEx_t>(self, kResetEx)(self, pp, g_cfg.hasRect ? nullptr : fs);
	}

	HRESULT STDMETHODCALLTYPE hkCreateDevice(IDirect3D9* self, UINT adapter, D3DDEVTYPE type, HWND focus, DWORD flags,
	                                         D3DPRESENT_PARAMETERS* pp, IDirect3DDevice9** dev)
	{
		FixPresentParams(pp);
		const HRESULT hr = Original<CreateDevice_t>(self, kCreateDevice)(self, adapter, type, focus, flags, pp, dev);
		if (SUCCEEDED(hr) && dev && *dev) PatchSlot(*dev, kReset, reinterpret_cast<void*>(hkReset));
		else Log("CreateDevice failed: 0x%08lx", hr);
		return hr;
	}

	HRESULT STDMETHODCALLTYPE hkCreateDeviceEx(IDirect3D9Ex* self, UINT adapter, D3DDEVTYPE type, HWND focus,
	                                           DWORD flags, D3DPRESENT_PARAMETERS* pp, D3DDISPLAYMODEEX* fs,
	                                           IDirect3DDevice9Ex** dev)
	{
		FixPresentParams(pp);
		const HRESULT hr = Original<CreateDeviceEx_t>(self, kCreateDeviceEx)(self, adapter, type, focus, flags, pp,
		                                                                     g_cfg.hasRect ? nullptr : fs, dev);
		if (SUCCEEDED(hr) && dev && *dev)
		{
			PatchSlot(*dev, kReset, reinterpret_cast<void*>(hkReset));
			PatchSlot(*dev, kResetEx, reinterpret_cast<void*>(hkResetEx));
		}
		else Log("CreateDeviceEx failed: 0x%08lx", hr);
		return hr;
	}

	void PatchD3D(IDirect3D9* d3d)
	{
		PatchSlot(d3d, kGetAdapterModeCount, reinterpret_cast<void*>(hkGetAdapterModeCount));
		PatchSlot(d3d, kEnumAdapterModes, reinterpret_cast<void*>(hkEnumAdapterModes));
		PatchSlot(d3d, kCreateDevice, reinterpret_cast<void*>(hkCreateDevice));
	}

	IDirect3D9* WINAPI hkDirect3DCreate9(UINT sdk)
	{
		IDirect3D9* d3d = oDirect3DCreate9(sdk);
		if (d3d) PatchD3D(d3d);
		return d3d;
	}

	HRESULT WINAPI hkDirect3DCreate9Ex(UINT sdk, IDirect3D9Ex** out)
	{
		const HRESULT hr = oDirect3DCreate9Ex(sdk, out);
		if (SUCCEEDED(hr) && out && *out)
		{
			PatchD3D(*out);
			PatchSlot(*out, kCreateDeviceEx, reinterpret_cast<void*>(hkCreateDeviceEx));
		}
		return hr;
	}
}

namespace ss
{
	void InstallD3DHooks()
	{
		if (!GetModuleHandleW(L"d3d9.dll") && !LoadLibraryW(L"d3d9.dll")) return;
		HookApi(L"d3d9.dll", "Direct3DCreate9", reinterpret_cast<void*>(hkDirect3DCreate9),
		        reinterpret_cast<void**>(&oDirect3DCreate9));
		HookApi(L"d3d9.dll", "Direct3DCreate9Ex", reinterpret_cast<void*>(hkDirect3DCreate9Ex),
		        reinterpret_cast<void**>(&oDirect3DCreate9Ex));
	}
}
