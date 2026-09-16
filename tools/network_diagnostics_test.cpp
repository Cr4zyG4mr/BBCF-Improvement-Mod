#include "SteamNetworkDiagnostics.h"
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>

using namespace SteamNetDiag;

static const Peer& PeerWith(const Snapshot& snapshot, Count id)
{
	for (const auto& peer : snapshot.peers) if (peer.id == id) return peer;
	assert(false);
	return snapshot.peers[0];
}

int main()
{
	Counters counters;
	Snapshot snapshot;
	assert(!counters.Frame(1000, snapshot)); // Idle/offline logging stays quiet.
	counters.Send(11, 0, 0, 80, true, 1100, 2);
	counters.Send(11, 0, 0, 80, false, 1101, 3);
	counters.Send(11, 3, 2, 10, true, 1102, 1);
	counters.Read(11, 0, 40, true, 1103, 1);
	counters.Read(999, 0, 12345, false, 1104, 4); // Invalid failed-read outputs must not create a peer.
	counters.Poll(0, false, 1105, 2);
	counters.Poll(3, true, 1106, 1);
	counters.CallbacksBegin(1107);
	assert(counters.Frame(1110, snapshot));
	const auto first = PeerWith(snapshot, 11);
	assert(first.sends == 3 && first.accepted == 2 && first.sentBytes == 90);
	assert(first.received == 1 && first.receivedBytes == 40);
	assert(first.lastSend == 1102 && first.lastReceive == 1103);
	assert(first.maxSendMs == 3);
	assert(first.channels[0].id == 0 && first.channels[0].sends == 2 && first.channels[0].accepted == 1);
	assert(first.channels[1].id == 3 && first.channels[1].accepted == 1);
	assert(snapshot.activity.callbacksBegin == 1 && snapshot.activity.callbacksEnd == 0);
	assert(snapshot.activity.polls == 2 && snapshot.activity.available == 1);
	assert(snapshot.activity.reads == 2 && snapshot.activity.readOK == 1);
	for (const auto& peer : snapshot.peers) assert(peer.id != 999);
	for (Count now = 1111; now < 2110; ++now) assert(!counters.Frame(now, snapshot));
	counters.CallbacksEnd(2100, 993);
	assert(counters.Frame(2110, snapshot));
	assert(snapshot.activity.maxCallbacksMs == 993 && snapshot.activity.callbacksEnd == 1);
	assert(PeerWith(snapshot, 11).maxSendMs == 0); // Interval maxima reset, packet totals don't.
	assert(PeerWith(snapshot, 11).sends == 3);

	// Failure and cleanup for the old opponent must never be attributed to the new one.
	counters.Send(22, 0, 0, 10, true, 2200, 0);
	counters.Lifecycle(11, Counters::Failure, false, 4, 2300);
	counters.Lifecycle(11, Counters::CloseSession, true, -1, 2301);
	assert(counters.Frame(3110, snapshot));
	assert(PeerWith(snapshot, 11).closed && PeerWith(snapshot, 11).lastError == 4);
	assert(!PeerWith(snapshot, 22).closed && PeerWith(snapshot, 22).failures == 0);
	assert(PeerWith(snapshot, 11).serial == first.serial);
	counters.Send(11, 0, 0, 20, false, 3200, 0);
	counters.Lifecycle(22, Counters::CloseChannel, true, -1, 3201);
	assert(counters.Frame(4110, snapshot));
	assert(!PeerWith(snapshot, 11).closed); // Sending after close attempts to reopen.
	assert(!PeerWith(snapshot, 22).closed); // Closing one channel is not closing the whole peer.
	assert(PeerWith(snapshot, 22).channelCloses == 1);

	Counters flood;
	for (unsigned id = 1; id <= 100; ++id) flood.Send(id, 0, 0, 1, true, 10000 + id, 0);
	for (int channel = 1; channel <= 100; ++channel) flood.Send(100, channel, 0, 1, true, 10200, 0);
	assert(flood.Frame(11000, snapshot));
	unsigned tracked = 0;
	for (const auto& peer : snapshot.peers) if (peer.id) ++tracked;
	assert(tracked == MaxPeers && snapshot.evictions == 100 - MaxPeers);
	assert(PeerWith(snapshot, 100).channelOverflow == 97);
	assert(!flood.Frame(10200 + RetainMs + 1, snapshot));
	for (unsigned i = 0; i < 32; ++i) assert(flood.AllowEventLog(100000));
	for (unsigned i = 0; i < 100; ++i) assert(!flood.AllowEventLog(100000));
	assert(flood.AllowEventLog(101000));
	flood.Send(100, 0, 0, 1, true, 101000, 0);
	assert(flood.Frame(101000, snapshot) && snapshot.eventsSuppressed == 100);

	// Contending threads either record a call or count a skipped observation;
	// diagnostics do not wait/spin or lose observations without accounting for it.
	Counters concurrent;
	std::vector<std::thread> workers;
	for (unsigned thread = 0; thread < 4; ++thread) workers.emplace_back([&concurrent, thread]() {
		for (unsigned i = 0; i < 10000; ++i) concurrent.Send(thread + 1, 0, 0, 1, true, 2000, 0);
	});
	for (auto& worker : workers) worker.join();
	assert(concurrent.Frame(3000, snapshot));
	Count recorded = 0;
	for (const auto& peer : snapshot.peers) recorded += peer.sends;
	assert(recorded + snapshot.skipped == 40000);
	std::cout << "Network diagnostics: counters, failure recovery, bounds, rate limits, and concurrency passed\n";
}
