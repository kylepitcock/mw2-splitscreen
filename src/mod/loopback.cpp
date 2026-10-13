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
	}
}
