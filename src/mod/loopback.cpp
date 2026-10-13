// In-process networking for extra local players.
//
// Client -> server: the engine's server loop queue records a "source" per
// packet, which becomes the loopback address port the server sees. We stamp
// the live player's index there, so the server sees players as distinct.
//
// Server -> client: the engine queues by that port, but only has storage for
// local client 0. Players 1..3 get their own queues here; when they are the
// live player, the client's loop reads are served from their queue.
#include "splitscreen.hpp"
#include "game.hpp"
#include "util.hpp"
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace
{
	using namespace game;

	struct Packet
	{
		unsigned char data[LOOP_PACKET_SIZE];
		int length;
	};

	constexpr int QUEUE_SIZE = 32;

	struct Queue
	{
		Packet packets[QUEUE_SIZE];
		volatile LONG send;
		volatile LONG get;
	};

	Queue g_queues[MAX_SPLITSCREEN];

	// Diagnostics: first few out-of-band packets of each kind, plus a running count.
	void LogPacket(const char* dir, int player, const void* data, int length)
	{
		struct Seen
		{
			char name[24];
			int count;
		};
		static Seen seen[2][MAX_SPLITSCREEN][16];
		static int total[2][MAX_SPLITSCREEN];
		const int d = dir[0] == 'P' ? 0 : 1;
		++total[d][player];

		const unsigned char* bytes = static_cast<const unsigned char*>(data);
		if (length < 5 || *reinterpret_cast<const int*>(bytes) != -1)
		{
			if (total[d][player] % 500 == 0)
				ss::Log("%s: %d packets so far", dir, total[d][player]);
			return;
		}
		char name[24] = {};
		for (int i = 0; i < 23 && 4 + i < length && bytes[4 + i] > ' '; ++i) name[i] = static_cast<char>(bytes[4 + i]);
		for (Seen& s : seen[d][player])
		{
			if (!s.name[0]) strcpy_s(s.name, name);
			if (std::strcmp(s.name, name) != 0) continue;
			if (++s.count <= 3)
			{
				char line[64];
				sprintf_s(line, dir, player + 1);
				ss::Log("%s: oob '%.60s' (%d bytes)", line, reinterpret_cast<const char*>(bytes + 4), length);
			}
			return;
		}
	}

	using SendLoop_t = void(__cdecl*)(int sock, int length, const void* data, netadr_t to);
	SendLoop_t oSendLoop;
	void* oQueueClient;
	NET_GetLoopPacket_t oGetLoop;

	void __cdecl hkSendLoop(int sock, int length, const void* data, netadr_t to)
	{
		if (sock >= NS_SERVER || ss::g_live == 0)
		{
			oSendLoop(sock, length, data, to);
			return;
		}

		// Same as the engine's client branch, but the source is the live player.
		if (length > LOOP_PACKET_SIZE) length = LOOP_PACKET_SIZE;
		LONG index = *reinterpret_cast<volatile LONG*>(svLoopSend) & (SV_LOOP_PACKETS - 1);
		auto* entry = reinterpret_cast<unsigned char*>(svLoopQueue + index * LOOP_ENTRY_SIZE);
		std::memcpy(entry, data, length);
		*reinterpret_cast<int*>(entry + 0x578) = length;
		*reinterpret_cast<int*>(entry + 0x57C) = ss::g_live;
		InterlockedIncrement(reinterpret_cast<volatile LONG*>(svLoopSend));

		LogPacket("P%d -> server", ss::g_live, data, length);
	}

	using SV_PacketEvent_t = void(__cdecl*)(netadr_t from, msg_t* msg);
	SV_PacketEvent_t oSvPacketEvent;
	void __cdecl hkSvPacketEvent(netadr_t from, msg_t* msg)
	{
		static int statsLogged = 0;
		if (from.type == 2 && from.port > 0 && from.port < MAX_SPLITSCREEN && msg->cursize > 12 &&
		    std::memcmp(msg->data + 4, "stats", 5) == 0 && statsLogged < 2)
		{
			++statsLogged;
			const unsigned char* b = msg->data + 10;
			ss::Log("stats from local player %d: next bytes %02X %02X %02X %02X", from.port + 1, b[0], b[1], b[2], b[3]);
			for (int i = 0; i < *reinterpret_cast<int*>(0x31D938C); ++i)
			{
				auto* cl = reinterpret_cast<unsigned char*>(0x31D9390 + i * 0xA6790);
				if (*reinterpret_cast<int*>(cl) == 0) continue;
				ss::Log("  sv client %d: state %d adr type %d port %d qport %d", i, *reinterpret_cast<int*>(cl),
				        *reinterpret_cast<int*>(cl + 0x28), *reinterpret_cast<unsigned short*>(cl + 0x30),
				        *reinterpret_cast<int*>(cl + 0x3C));
			}
		}
		oSvPacketEvent(from, msg);
	}

	void __cdecl QueueExtra(int player, const void* data, int length)
	{
		ss::LoopbackQueue(player, data, length);
	}

	// NET_QueueClientLoopPacket(localClientNum, data) with the length in EBX.
	__declspec(naked) void hkQueueClient()
	{
		__asm
		{
			mov eax, [esp + 4]
			test eax, eax
			jz original
			cmp eax, 4 // MAX_SPLITSCREEN
			jae original
			push ebx
			push dword ptr [esp + 0xC]
			push eax
			call QueueExtra
			add esp, 12
			ret
		original:
			jmp oQueueClient
		}
	}

	// NET_CompareAdr treats every loopback address as equal, so the server's
	// party lookup maps player 2 onto player 1's slot. Loopback addresses that
	// carry different local player indices (ports 0..3) are different clients.
	using CompareAdr_t = int(__cdecl*)(netadr_t a, netadr_t b);
	CompareAdr_t oCompareAdr;

	int __cdecl hkCompareAdr(netadr_t a, netadr_t b)
	{
		if (a.type == 2 && b.type == 2 && a.port < MAX_SPLITSCREEN && b.port < MAX_SPLITSCREEN && a.port != b.port)
			return 0;
		return oCompareAdr(a, b);
	}

	// Extra local players authenticate with player 1's IW4x key, so their
	// connect carries player 1's XUID (IW4x rejects anything else). The engine
	// then finds player 1's session slot by that XUID and replaces player 1.
	// After IW4x has verified the packet, give the server a distinct XUID.
	struct CmdArgs
	{
		int nesting;
		int localClientNum[8];
		int controllerIndex[8];
		int argc[8];
		char** argv[8];
	};
	static_assert(offsetof(CmdArgs, argc) == 0x44 && offsetof(CmdArgs, argv) == 0x64, "CmdArgs");
	auto& sv_cmd_args = *reinterpret_cast<CmdArgs*>(0x1ACF8A0);

	void MakeXuidUnique(char* userinfo, int player)
	{
		char* key = std::strstr(userinfo, "\\xuid\\");
		if (!key) return;
		char* value = key + 6;
		char* end = value;
		while (*end && *end != '\\') ++end;
		if (end == value) return;
		const char* hex = "0123456789abcdef";
		const char* at = std::strchr(hex, end[-1] | 0x20);
		int digit = at ? static_cast<int>(at - hex) : 0;
		end[-1] = hex[(digit + player) & 15];
	}

	using DirectConnect_t = void(__cdecl*)(netadr_t from);
	DirectConnect_t oDirectConnect;
	void __cdecl hkDirectConnect(netadr_t from)
	{
		if (from.type == 2 && from.port > 0 && from.port < MAX_SPLITSCREEN)
		{
			int n = sv_cmd_args.nesting;
			if (n >= 0 && n < 8 && sv_cmd_args.argc[n] > 2)
				MakeXuidUnique(sv_cmd_args.argv[n][2], from.port);
			ss::Log("SV_DirectConnect for local player %d", from.port + 1);
		}
		oDirectConnect(from);
	}

	using PartyFind_t = int(__cdecl*)(void* party, netadr_t from, int unk);
	PartyFind_t oPartyFind;
	int __cdecl hkPartyFind(void* party, netadr_t from, int unk)
	{
		int r = oPartyFind(party, from, unk);
		if (from.type == 2) ss::Log("party member lookup for loopback port %d -> %d", from.port, r);
		return r;
	}

	int __cdecl hkGetLoop(int sock, netadr_t* from, msg_t* msg)
	{
		if (sock == NS_SERVER || ss::g_live == 0) return oGetLoop(sock, from, msg);
		return ss::LoopbackDequeue(ss::g_live, from, msg) ? 1 : 0;
	}
}

namespace ss
{
	void LoopbackQueue(int player, const void* data, int length)
	{
		Queue& q = g_queues[player];
		if (q.send - q.get >= QUEUE_SIZE)
		{
			Log("loopback queue for player %d overflowed; dropping oldest", player + 1);
			InterlockedIncrement(&q.get);
		}
		if (length > LOOP_PACKET_SIZE) length = LOOP_PACKET_SIZE;
		LogPacket("server -> P%d", player, data, length);
		Packet& p = q.packets[q.send % QUEUE_SIZE];
		std::memcpy(p.data, data, length);
		p.length = length;
		InterlockedIncrement(&q.send);
	}

	bool LoopbackDequeue(int player, void* fromAdr, void* msgPtr)
	{
		Queue& q = g_queues[player];
		if (q.get >= q.send) return false;

		Packet& p = q.packets[q.get % QUEUE_SIZE];
		auto* msg = static_cast<msg_t*>(msgPtr);
		int length = p.length < msg->maxsize ? p.length : msg->maxsize;
		std::memcpy(msg->data, p.data, length);
		msg->cursize = length;
		InterlockedIncrement(&q.get);

		// The engine thinks it is local client 0, so the packet must look like client 0's.
		auto* from = static_cast<netadr_t*>(fromAdr);
		std::memset(from, 0, sizeof(*from));
		from->type = 2; // NA_LOOPBACK
		return true;
	}

	void LoopbackReset(int player)
	{
		g_queues[player].get = g_queues[player].send;
	}

	void InstallLoopback()
	{
		Hook(NET_SendLoopPacket, hkSendLoop, &oSendLoop);
		Hook(NET_QueueClientLoopPacket, reinterpret_cast<void*>(hkQueueClient), &oQueueClient);
		Hook(NET_GetLoopPacket, hkGetLoop, &oGetLoop);
		Hook(0x4D0AA0, hkCompareAdr, &oCompareAdr); // NET_CompareAdr
		Hook(0x460480, hkDirectConnect, &oDirectConnect);
		Hook(0x48CB10, hkPartyFind, &oPartyFind);
		Hook(0x626800, hkSvPacketEvent, &oSvPacketEvent);
	}
}
