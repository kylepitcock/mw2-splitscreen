#pragma once
// Engine addresses for iw4mp 1.2.159 (the executable IW4x runs, iw4x.exe).
// Every address here was verified by disassembly (tools/re); see docs/engine-notes.md.
#include <cstdint>

namespace game
{
	constexpr int MAX_SPLITSCREEN = 4;

	struct netadr_t // 20 bytes, passed by value
	{
		int type;           // 2 = NA_LOOPBACK
		unsigned char ip[4];
		unsigned short port; // loopback: the sending local client
		unsigned short pad;
		int addrHandleIndex;
		int unused;
	};
	static_assert(sizeof(netadr_t) == 20, "netadr_t");

	struct msg_t
	{
		int overflowed;
		int readOnly;
		unsigned char* data;
		unsigned char* splitData;
		int maxsize;
		int cursize;
		int splitSize;
		int readcount;
		int bit;
		int lastEntityRef;
	};
	static_assert(sizeof(msg_t) == 0x28, "msg_t");

	struct cmd_function_s
	{
		cmd_function_s* next;
		const char* name;
		const char* autoCompleteDir;
		const char* autoCompleteExt;
		void (*function)();
		int flags;
	};

	enum netsrc_t
	{
		NS_CLIENT1 = 0,
		NS_SERVER = 1,
	};

	// ---------------------------------------------------------------- functions
	using Com_Printf_t = void(__cdecl*)(int channel, const char* fmt, ...);
	inline auto Com_Printf = reinterpret_cast<Com_Printf_t>(0x402500);

	using Cmd_AddCommand_t = void(__cdecl*)(const char* name, void (*fn)(), cmd_function_s* alloc, int isKey);
	inline auto Cmd_AddCommand = reinterpret_cast<Cmd_AddCommand_t>(0x470090);

	using Cbuf_AddText_t = void(__cdecl*)(int localClientNum, const char* text);
	inline auto Cbuf_AddText = reinterpret_cast<Cbuf_AddText_t>(0x404B20);

	using Sys_Milliseconds_t = int(__cdecl*)();
	inline auto Sys_Milliseconds = reinterpret_cast<Sys_Milliseconds_t>(0x42A660);

	using MSG_Init_t = void(__cdecl*)(msg_t* msg, unsigned char* data, int length);
	inline auto MSG_Init = reinterpret_cast<MSG_Init_t>(0x45FCA0);

	// CL_PacketEvent(localClientNum, from, msg, time)
	using CL_PacketEvent_t = void(__cdecl*)(int localClientNum, netadr_t from, msg_t* msg, int time);
	inline auto CL_PacketEvent = reinterpret_cast<CL_PacketEvent_t>(0x45CEF0);

	// CL_Frame(localClientNum): per-client work inside Com_Frame (only runs while clientUIActive.isRunning)
	using CL_Frame_t = void(__cdecl*)(int localClientNum);
	constexpr uintptr_t CL_Frame = 0x4B0ED0;

	// Connects the running local client to "localhost" (what the engine does after a local server starts).
	using CL_ConnectToLocalServer_t = void(__cdecl*)();
	inline auto CL_ConnectToLocalServer = reinterpret_cast<CL_ConnectToLocalServer_t>(0x44A2A0);

	using CL_Disconnect_t = void(__cdecl*)(int localClientNum);
	inline auto CL_Disconnect = reinterpret_cast<CL_Disconnect_t>(0x403540);

	// Returns the constant 1 in the PC build; 24 callers use it as "number of local players".
	constexpr uintptr_t CL_GetLocalClientActiveCount = 0x5BAD90;

	// ---------------------------------------------------------------- loopback
	// NET_SendLoopPacket(sock, length, data, to): client->server queue, or server->client via QueueClientLoop.
	constexpr uintptr_t NET_SendLoopPacket = 0x41DF30;
	// NET_QueueClientLoopPacket(localClientNum, data) with length in EBX: server->client queue.
	constexpr uintptr_t NET_QueueClientLoopPacket = 0x60FD60;
	// NET_GetLoopPacket(sock, from*, msg*) -> int
	using NET_GetLoopPacket_t = int(__cdecl*)(int sock, netadr_t* from, msg_t* msg);
	constexpr uintptr_t NET_GetLoopPacket = 0x4BFEE0;

	constexpr uintptr_t svLoopQueue = 0x1CC85F0;     // 64 x 0x580: data[0x578], length, source
	constexpr uintptr_t svLoopSend = 0x1CDE5F4;      // write counter (interlocked)
	constexpr int LOOP_PACKET_SIZE = 0x578;
	constexpr int LOOP_ENTRY_SIZE = 0x580;
	constexpr int SV_LOOP_PACKETS = 64;

	// ---------------------------------------------------------------- per-local-client state
	// Every block below is referenced at a fixed address by the PC build (single local client).
	struct StateBlock
	{
		const char* name;
		uintptr_t address;
		size_t size;
	};

	inline constexpr StateBlock clientStateBlocks[] = {
		{ "clientUIActive", 0xB2BB88, 0xAF4 },
		{ "cl", 0xB2C698, 0x52C960 },
		{ "cl_parseEntities", 0xAE7978, 0x40004 },
		{ "clc", 0xA1E878, 0x615E8 },
		{ "clc_flags", 0xA1E874, 4 },
		{ "cgs", 0x7ED3B8, 0x3BA4 },
		{ "cg", 0x7F0F78, 0xFD540 },
		{ "cg_entities", 0x8F3CA8, 0x102000 },
		{ "cg_weapons", 0x7E3CC8, 0x9600 },
		{ "cg_lc_1680", 0x7DEDB8, 0x1680 },
		{ "cg_lc_28", 0x9F5D28, 0x28 },
		{ "uiContext", 0x62E2858, 0x22FC },
	};

	constexpr uintptr_t clientUIActive_isRunning = 0xB2BB89; // bool
	constexpr uintptr_t clientUIActive_connectionState = 0xB2C540;
	constexpr uintptr_t cls_servername = 0xA7FE98;
}
