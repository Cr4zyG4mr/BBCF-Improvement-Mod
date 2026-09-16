// Compiles the actual production wrapper with disposable platform/API stubs.
#include "SteamNetworkingWrapper.h"
#include "Core/interfaces.h"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

unsigned long long mockTick = 1000;
DWORD mockError = 0;
bool logging = true;
TestGameValues g_gameVals{};
std::vector<std::string> lines;
bool IsLoggingEnabled() { return logging; }
void ForceLog(const char* format, ...)
{
	char buffer[4096];
	va_list args; va_start(args, format);
	std::vsnprintf(buffer, sizeof(buffer), format, args); va_end(args);
	lines.emplace_back(buffer);
	SetLastError(999); // Logging must not leak this into the game.
}

struct Native : ISteamNetworking
{
	int sends = 0, reads = 0, polls = 0, states = 0, closes = 0, accepts = 0;
	unsigned stateDelay = 0;
	bool result = true;
	CSteamID remote{111};
	const void* lastData = nullptr;
	uint32 lastBytes = 0;
	int lastChannel = -1;
	EP2PSend lastType{};
	bool SendP2PPacket(CSteamID id, const void* data, uint32 bytes, EP2PSend type, int channel) override
	{
		++sends; remote = id; lastData = data; lastBytes = bytes; lastChannel = channel; lastType = type;
		SetLastError(123); return result;
	}
	bool IsP2PPacketAvailable(uint32* bytes, int channel) override
	{
		++polls; lastChannel = channel; if (result && bytes) *bytes = 4;
		SetLastError(123); return result;
	}
	bool ReadP2PPacket(void* data, uint32 capacity, uint32* bytes, CSteamID* id, int channel) override
	{
		++reads; lastChannel = channel;
		if (result && capacity >= 4)
		{
			if (data) static_cast<char*>(data)[0] = 'x';
			if (bytes) *bytes = 4;
			if (id) *id = remote;
		}
		SetLastError(123); return result;
	}
	bool GetP2PSessionState(CSteamID, P2PSessionState_t* state) override
	{
		++states; mockTick += stateDelay; state->m_bConnectionActive = 1; state->m_nPacketsQueuedForSend = 7;
		SetLastError(777); return true;
	}
	bool AcceptP2PSessionWithUser(CSteamID) override { ++accepts; SetLastError(123); return result; }
	bool CloseP2PSessionWithUser(CSteamID) override { ++closes; SetLastError(123); return result; }
	bool CloseP2PChannelWithUser(CSteamID, int channel) override { ++closes; lastChannel = channel; SetLastError(123); return result; }
	bool AllowP2PPacketRelay(bool) override { SetLastError(123); return result; }
};

static bool Logged(const char* text)
{
	for (const auto& line : lines) if (line.find(text) != std::string::npos) return true;
	return false;
}

int main()
{
	Native native;
	ISteamNetworking* ptr = &native;
	SteamNetworkingWrapper wrapper(&ptr);
	char data[8]{};
	uint32 bytes = 0;
	CSteamID remote;
	assert(wrapper.SendP2PPacket(CSteamID(111), data, 8, static_cast<EP2PSend>(2), 3));
	assert(native.sends == 1 && native.remote.ConvertToUint64() == 111);
	assert(native.lastData == data && native.lastBytes == 8 && native.lastChannel == 3 && native.lastType == 2);
	assert(GetLastError() == 123 && native.states == 0); // No extra Steam queries on packet paths.
	assert(wrapper.IsP2PPacketAvailable(&bytes, 3) && bytes == 4 && native.polls == 1);
	assert(wrapper.ReadP2PPacket(data, 8, &bytes, &remote, 3));
	assert(data[0] == 'x' && bytes == 4 && remote.ConvertToUint64() == 111 && GetLastError() == 123);
	native.result = false;
	assert(!wrapper.SendP2PPacket(CSteamID(111), data, 8, static_cast<EP2PSend>(0), 0));
	// Deliberately invalid pointers are safe here because the native failed read
	// never touches them, and neither may the diagnostic observer.
	assert(!wrapper.ReadP2PPacket(nullptr, 0, reinterpret_cast<uint32*>(1), reinterpret_cast<CSteamID*>(1), 0));
	assert(!wrapper.IsP2PPacketAvailable(reinterpret_cast<uint32*>(1), 0));
	assert(GetLastError() == 123 && native.reads == 2 && native.sends == 2);
	native.result = true;
	assert(wrapper.AcceptP2PSessionWithUser(CSteamID(111)) && native.accepts == 1 && GetLastError() == 123);
	auto start = wrapper.DiagnosticCallbacksBegin();
	mockTick += 20;
	wrapper.DiagnosticCallbacksEnd(start);
	wrapper.DiagnosticFrame();
	assert(native.states == 1 && GetLastError() == 123);
	assert(Logged("txCalls=2 txAccepted=1 txBytes=8 rxPackets=1 rxBytes=4"));
	assert(Logged("channels(send/accepted/recv)=[3:1/1/1,0:1/0/0]"));
	assert(Logged("cbMaxMs=20"));
	assert(Logged("queuedPackets=7"));
	for (int i = 0; i < 1000; ++i) wrapper.DiagnosticFrame();
	assert(native.states == 1); // Rendering more often does not issue more IPC.
	SteamNetworkingWrapper::DiagnosticFailure(CSteamID(111), 4);
	assert(native.closes == 0); // Observation must never attempt recovery itself.
	assert(wrapper.CloseP2PChannelWithUser(CSteamID(111), 3) && native.closes == 1 && native.lastChannel == 3);
	assert(wrapper.CloseP2PSessionWithUser(CSteamID(111)) && native.closes == 2 && GetLastError() == 123);
	mockTick += 1000; wrapper.DiagnosticFrame();
	assert(native.states == 1); // Do not probe a session the game just closed.
	assert(Logged("event=close-session-begin") && Logged("event=close-session-end"));
	assert(Logged("failures=1 lastFailure=4 closed=1"));
	for (unsigned id = 111; id < 121; ++id) wrapper.SendP2PPacket(CSteamID(id), data, 8, static_cast<EP2PSend>(0), 0);
	mockTick += 1000; wrapper.DiagnosticFrame();
	assert(native.states == 5); // At most four state queries in a sample.
	native.stateDelay = 10;
	mockTick += 1000; wrapper.DiagnosticFrame();
	assert(native.states == 6); // One slow query stops additional queries in this sample.
	assert(wrapper.AllowP2PPacketRelay(false) && GetLastError() == 123);
	logging = false;
	const auto lineCount = lines.size();
	mockTick += 1000; wrapper.DiagnosticFrame();
	assert(wrapper.SendP2PPacket(CSteamID(111), data, 8, static_cast<EP2PSend>(0), 0));
	assert(native.states == 6 && lines.size() == lineCount);
	std::cout << "Network wrapper: native call forwarding, failed-read safety, error preservation, passive observation, and query limits passed\n";
}
