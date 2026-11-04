# Offline IWX AWDL station/queue ownership

`itlwm/hal_iwx/iwx_awdl_resources.h` is an allocation-free policy ledger. It
records software intent and authoritative completion evidence supplied by a
future adapter. It is not integrated into the driver and sends no commands.
It cannot reserve a firmware-chosen queue ID, stop DMA, establish station type
support, or preserve Wi-Fi by itself. The installed kext and EFI are untouched.

## Configuration and boundaries

The caller initializes fresh storage with a nonzero firmware epoch, explicit
station/queue limits, and exclusive grant sets. Storage supports 16 stations
and 512 queues. This conservative profile excludes station IDs 0..2 and queue
IDs 0..9, protecting the base station, auxiliary/monitor and DQA reservations
identified in `if_iwxvar.h`/`if_iwxreg.h`. These are upper bounds and policy
exclusions, not a claim that every remaining ID is supported or available.
Limits must come from separately validated hardware/firmware capabilities.
Empty grant sets are valid and allow no corresponding assignments. Starting a
queue allocation consumes a capacity slot; assigned queues and pending requests
both count, so exhausted grants reject new allocation before any command intent
is recorded. This bounds demand without reserving a particular firmware ID.

Grants must remain exclusive against every other allocator for the complete
epoch, including queue IDs not yet assigned to a peer. A one-time snapshot of
currently free IDs is insufficient. The existing `sc_tid_data`, `sc->txq` and
station allocator do not participate in this ledger. The future integration
must first centralize ownership and determine how firmware's dynamic queue
assignment can honor those grants. An unexpected queue ID causes a fault; it
is not forcibly freed or relabeled as belonging to AWDL.

One peer gets one station and at most one queue. A nonzero unicast MAC is only
an identity key, not authentication, discovery admission or a validated AWDL
peer. The caller serializes every access and must not mutate ledger internals.
Output/handle/ticket storage is separate from inputs and the ledger. Handles
belong to one ledger instance. Do not mix handles from independent ledgers,
reinitialize a live ledger, or reconstruct it with a previously used epoch.

## Lifecycle and tickets

`reserve` returns the lowest granted unused station. Duplicate MACs, exhausted
slots and invalid input do not alter ownership. The handle contains epoch,
lease serial and station ID; each `begin` adds a distinct operation serial.
Neither serial nor epoch may wrap. A stale or duplicate completion changes
nothing, including when a slot has been reused or a later command is pending.

| Current state | `begin` action | Confirmed success |
| --- | --- | --- |
| Reserved | ADD_STATION | Station exists, no queue |
| Station exists | ALLOC_QUEUE | Ready, with a granted unique queue |
| Ready | DRAIN_QUEUE | Queue idle, no TX allowed |
| Queue idle | REMOVE_QUEUE | Station exists, queue no longer owned |
| Station exists | REMOVE_STATION | Lease released |

Reserved-only leases can be canceled. All other releases require the ordered
completion path or a confirmed reset. `close` is monotonic and can be recorded
during an in-flight command; a late successful creation still cannot reopen
TX. Drain and removal intents close the lease automatically. New creation is
blocked once closing starts. A TX lookup requires a current handle, ready
state, an owned queue, no close request and no ledger-wide fault. The returned
ID is not a resource pin: revalidate under the same serialization that commits
actual TX, with separately validated binding, DMA lifetime and radio scheduling.

Completion outcomes have deliberately different meanings:

- SUCCESS: adapter has verified the operation actually completed. For drain
  and queue removal, submission alone or resetting a host ring is insufficient.
- DENIED: authoritative no-effect rejection. Add rejection releases its station
  reservation; queue allocation rejection retains the station; teardown
  rejection retains ownership for retry and never reopens TX.
- UNCERTAIN: possibly executed, including timeout or an ambiguous transport
  error. The lease is quarantined and its known queue remains owned. Unknown
  allocation also faults the whole ledger because the returned ID is unknown.

`complete` returns OK, REJECTED or UNKNOWN for these recorded outcomes. A
successful allocation returning a reserved, ungranted, out-of-range or already
owned queue returns CONFLICT, quarantines the requesting lease and faults all
TX lookup/new-creation paths. Other owners are retained. Known teardown is
still possible; the fault persists until reset. This fault is a software
admission barrier, not evidence that a conflicting firmware command was harmless
to the existing Wi-Fi connection. A real adapter needs device recovery here.

## Reset and future integration

`reset(new_epoch)` requires a strictly larger epoch, retains configuration,
and invalidates all leases and tickets. It is legal only AFTER the adapter has
fenced old commands, stopped DMA and reset firmware. A generation increment,
sleep/wake notification or request to stop does not prove this happened.
Serial exhaustion fails closed; recovery also requires this confirmed boundary.

Still required before using this in the driver:

1. Establish a supported independent MAC/PHY context and central allocator.
2. Validate station-command versions/types and queue allocation/removal semantics
   for the actual firmware. Do not map every transport error to DENIED.
3. Implement operation dispatch, quiescence evidence, reset fencing, cancellation
   and suspend/resume handling. Route completions using the original ticket.
4. Bind TX/RX and radio scheduling, then verify coexistence and frames on air.

## Offline evidence

`tests/iwx_awdl_resources_test.c` exercises the actual ledger in C11 and C++11
under ASan/UBSan. Fixtures cover setup/ordered teardown, duplicate/capacity/grants,
close during creation, operation/lease/epoch staleness, phase-specific rejection,
uncertain outcomes, queue conflicts and serial limits. One fixture feeds a
literal legacy queue-v2 reply through the existing decoder into the ledger;
this is codec-to-policy validation, not a simulated firmware success claim.
The complete runner passed all 13 executions across 9 distinct suites, including
the ledger as C11 and C++11 with ASan/UBSan. An independent read-only review also
ran the full runner successfully and found no critical or important issues.
No kernel build, driver loading, firmware acceptance or live lifecycle validation
is claimed for this independent-header increment. AirDrop is still unavailable.
