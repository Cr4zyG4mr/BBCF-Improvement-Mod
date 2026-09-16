# Ranked disconnect diagnostics

Experimental branch only. Applied after the CBR conversion diagnostic patch.
This adds observation to existing Steam wrappers, the existing callback detour,
and EndScene. It does not change packet contents, API arguments/return values,
timeouts, connection acceptance/closure policy, CBR timing, or any assembly hook.
No diagnostic action sends probes or attempts to reconnect a peer.

Enable the existing `GenerateDebugLogs` setting. At wrapper initialization,
`BBCF_IM/DEBUG.txt` contains `[NETDIAG] enabled version=1`. No new setting is needed.
The existing DEBUG history rotation still applies; CbrErrorReport is unrelated.

## Reading the log

Every record includes a monotonic `tick` in milliseconds, alongside the normal
wall-clock log prefix. Lifecycle begin/end records also include the remote
SteamID, native result, elapsed time, and caller address on the begin record.
Use the build's matching PDB to resolve caller addresses if needed.

| Record/field | Meaning |
| --- | --- |
| `sample` | Once-per-second snapshot driven by rendered frames, including numeric game/match state. Match state 4 is FinishSign. |
| `frames`, `frameGapMaxMs` | Observed EndScene count and largest interval between observations since the previous sample. |
| `cbBegin`, `cbEnd`, ages, maxima | Native Steam callback invocation/completion counts, time since each, longest call and invocation gap. A callback count mismatch may also involve skipped observations. |
| `polls`, `available`, `reads`, `readOK` | Cumulative game/API polling and packet-read counts, with last channel, ages and call-duration maxima. A false availability/read result is not labeled an error. |
| `peer`, `serial` | Remote SteamID and observer slot lifetime; compare cumulative counters only within the same serial. Slot eviction changes serial. |
| `txCalls`, `txAccepted`, `txBytes` | Send attempts, sends accepted by Steam, and accepted bytes. Accepted is **not confirmation of delivery**. |
| `rxPackets`, `rxBytes`, ages | Successfully read incoming packets and bytes. `-1` age means not observed. |
| `channels(send/accepted/recv)` | Per-channel cumulative counts for up to four channels per peer. `channelOverflow` accounts for additional channels; peer totals still include them. |
| `queried`, `stateOK` | Whether this sample queried native Steam state and whether that query succeeded. Unavailable state fields are `-1`, not zero/healthy. |
| `active`, `connecting`, `error`, `relay`, queue fields | Native Steam session state at query time. The historical `lastFailure` is separate from the current native `error`. |
| `closed` | Last observed successful whole-session close, cleared by a later send/read/successful accept. This is observer history, not Steam's current connection verdict. Closing one channel does not mark the whole peer closed. |
| `event=connect-fail` | Existing Steam failure callback, attributed to its own remote ID. The existing handling of that callback is unchanged. |
| `event=accept-*`, `close-session-*`, `close-channel-*`, `allow-relay` | Actual game/mod API calls; no extra lifecycle calls are introduced. |
| `skipped`, `evictions`, `eventsSuppressed` | Accounting for counter-lock contention, bounded peer replacement and lifecycle-log rate limiting. |
| `previousSampleCostMs`, `queryMs` | Observer overhead and native state-query duration; a slow query can itself exceed the additional-query budget. |

Packet paths only update fixed counters. They never query extra Steam state or
write the new packet summaries to disk. Lifecycle records are limited to 32
events per second (up to two lines per event). Frame sampling retains at most 16
peers for 60 seconds since last activity/lifecycle event and emits at most one
summary plus 16 peer lines per second while any peer remains recent.

State reads use the original Steam interface directly, at most four per second,
rotating among tracked peers. No further query is issued after state reads in a
sample have already taken 5 ms. This is not a timeout on an individual Steam call. Recently
closed peers are still logged but not queried until another connection attempt
or received packet is observed. Counter locks are never held across Steam calls
or logging, and a contending observation is skipped rather than waiting/spinning.

## One useful test session

Keep the same Proton version and fsync/esync settings. Start BBCF with this DLL,
play Ranked until the disconnect, then attempt another Ranked match in the same
game process. Exit normally and preserve that session's DEBUG.txt and Proton
log. Report the score and visible screen at the first failure. Avoid restarting
before collecting the failed-followup attempt; restart recovery is already known.

The key comparisons are the active opponent's receive age/queue state, continued
polling/callbacks/rendering, and precisely which peer/channel is closed before the
next synchronization attempt. Lobby packets received successfully do not prove
that gameplay synchronization is healthy. A later timeout callback for the old
opponent must not be attributed to the next opponent.

This observes the P2P-packet API used in the current logs. It is not a packet
capture, does not establish what the opponent received, and cannot prove an
external path failure versus Steam-internal loss without additional evidence.

## Verification

After applying the workflow patches, run `python tools/test_network_diagnostics.py`.
It tests bounded counters, per-peer/channel attribution, old-peer failures while
a new opponent connects, close/reopen histories, sampling limits, and concurrent
observations. A mock Steam implementation exercises the **actual production
wrapper**, checking exact forwarding, failed-read output safety, last-error
preservation, no diagnostic recovery calls, and bounded state queries. Linux
uses ASan/UBSan; GitHub Actions runs the same cases with Win32 MSVC and builds the
full DLL against the real Steam SDK. In-game validation remains necessary.
