# Offline explicit station command adapter

`iwx_station_adapter.h` now connects command preparation and completion policy
to the actual offline resource ledger. It never sends a command, changes the
base station's shared flags, allocates DMA or installs/loads a kext. The only
kernel-source change is inclusion and compile-time ABI checks; dispatch remains
disconnected.

## Preparation

`itl_iwx_station_prepare` consumes a current lease handle, explicit command and
notification versions, and (for add) a caller-validated context/type. It prepares
the whole request before recording a fresh ledger operation ticket. Invalid
versions/context, stale handles and wrong lifecycle phases leave ownership and
serials unchanged; failed output is cleared. The request retains the original
epoch/lease/operation identity. It is caller-owned, immutable, per-ledger storage
and must not alias inputs or the ledger. Access must be serialized. A successful
prepare leaves a pending operation even though it does not transmit: the caller
must retain and complete that request. If it can prove the command was never
submitted, it may record a definitive no-effect DENIED outcome through the
ledger; ambiguous submission must remain UNCERTAIN.

The deliberately narrow wire profiles are:

| Action | Group/opcode | Version / reply | Payload |
| --- | --- | --- | --- |
| ADD_STATION | 1 / 0x18 | 12 / 2 only | 48 bytes, station from lease and MAC from peer entry |
| REMOVE_STATION | 1 / 0x19 | 2 / 0 only | 4 bytes, station from lease, reserved bytes zero |

Add uses explicit byte-sized context ID/color, station type 0..4 as encodable
identifiers, disabled aggregation, zero advanced settings and a 20 MHz/SISO
flags/mask profile. These are wire constraints, not supported AWDL role or radio
claims. A caller must validate the independent context's MAC/PHY ownership,
station type and version-specific semantics. In particular v12 may interpret
the context field as an LMAC ID for some roles; the adapter does not perform
that negotiation. It must not be used to infer an auxiliary or multicast role
from a valid byte. There is no default selection of station 0 or AP context.

The layout reference is Intel's
[Linux v6.12 station API](https://github.com/torvalds/linux/blob/v6.12/drivers/net/wireless/intel/iwlwifi/fw/api/sta.h),
including the 48-byte station layout reused by these versions and 4-byte remove
payload. Kernel assertions check the fields written by the adapter against the
existing packed structures, plus status/mask and context packing constants.
No compatibility with other command versions is inferred from numeric ordering.

## Completion policy

`finish_add` requires the original request, successful transport evidence, a
clear command-failed flag and exactly four status bytes. It interprets the
published low-byte status mask; a success value of 1 completes the original
ADD_STATION ticket. Upper status bits are not used as a low-byte error code.
A non-success status, failed transport, missing/truncated/oversized reply or
failure flag is treated as UNCERTAIN and quarantines the station reservation.
A status error is not assumed to guarantee atomic no-effect rejection.
Malformed request envelopes are rejected without completing the pending ticket.
The transport must associate the reply with the exact request and firmware
incarnation; the adapter cannot establish that hardware correlation itself.

`finish_remove` accepts explicit station-absence confirmation from a future
validated backend. It does not invent a status payload for reply version 0.
An empty response or successful send alone is insufficient. Unconfirmed absence
quarantines the reservation. For a station-bound queue, confirmed removal enters
STATION_GONE with the queue and lease still held; only the independent host
DMA/lifetime fence may complete RELEASE_HOST_QUEUE. Tests supply attestations
explicitly; those fixtures do not prove hardware completion or DMA safety.

DRAIN_QUEUE is deliberately not mapped to a single ADD_STA modify request.
Acknowledging DRAIN_FLOW does not establish a completed flush or a queue-lifetime
fence. Other unsupported actions are rejected without consuming a ticket.
Late/duplicate replies are rejected by the ledger. Closing during creation never
reopens TX, and reset invalidates old requests.

## Verification and next integration

The new C11/C++11 ASan/UBSan suite checks independent golden bytes, explicit
station identity, invalid versions and contexts, atomic preparation failure,
unaligned status data, transport/status/length failures, duplicate/stale replies,
close/reset handling and retained queues through legacy removal. The entire
existing runner passed all 17 C/C++ sanitizer executions and five Python test
cases. Debug x86_64 Tahoe and Sequoia builds both exited 0, including the new
compile-time ABI assertions, with pinned MacKernelSDK and Xcode macOS 26.5 SDK.
Both builds retain compiler/linker warnings; this is not warning-free validation.
No command was submitted to hardware during these checks. Independent read-only
review found no critical/important issues and reran the full runner successfully.
Optional untested exact values noted by review are nonboolean confirmation 2 and
upper encodable station/context/type boundaries; these branches were inspected.

Still required: a validated independent MAC/PHY context and role, version inputs
from the active firmware epoch, command-gate transport with transaction/epoch
correlation, proven flush/absence evidence, and DMA teardown fencing. The
adapter is usable for offline protocol/lifecycle verification only. AirDrop and
live station creation remain unavailable.
