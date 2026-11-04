# IWX flush reply validation and teardown evidence

This increment integrates a host-tested decoder into the existing
`iwx_flush_sta_tids` path. It does not install a kext, submit any new AWDL
command, create an AWDL station, or activate the offline resource ledger.

## Finding and change

The original handler indexed `sc_tid_data[tid]` using an unchecked firmware
uint16 TID. The table has `IWX_MAX_TID_COUNT + 1` (9) entries while management
TID is 15. Thus a management or malformed TID could read beyond this array.
This is a source-level bounds defect; it has not been tied to a hardware crash.

`iwx_flush_v1.h` now decodes the exact 132-byte legacy reply: station/count
followed by sixteen fixed 8-byte entries. It verifies station identity, count,
TID range/request mask, the driver's 512-queue array bounds and unique queue
IDs. The whole reply is checked before any ring update; malformed final entries
cannot leave earlier entries already applied. Unused fixed-array tail bytes
are ignored. Byte loads work for unaligned input. Failure clears decoded output.
The structure's sizes and field offsets are asserted in the kernel caller.

For normal data TIDs, a separate bounds-checked binding helper compares the
returned queue against the eight active data-TID bindings. Management TID 15
uses `first_data_qid` without indexing the data array. Other valid wire TIDs
have no software binding and are skipped. Queue 0 is never eligible for a
station binding: the normal TX path treats data binding zero as unassigned,
and matching it must not reclaim the command queue. Only base station 0 may use these
AP bindings; another station needs its own owner lookup. Empty rings are skipped
before sequence-to-ring index conversion, and existing `iwx_ampdu_txq_advance`
completion-window validation remains in place. Returned read pointers remain
raw 16-bit values; the codec does not confuse sequence and ring-index domains.

The handler also rejects a changed `sc_generation` (and AIRPORT_VTD DMA epoch)
after the command returns, before touching queues from a newer incarnation.
It retains the existing caller serialization assumptions; these checks are
not a substitute for a future AWDL command-gate/ownership adapter.

## No reply is not drain evidence

Some supported firmware returns an empty payload. The decoder exposes a
separate NO_REPLY result, and the base handler preserves its existing successful
return in that compatibility case. That return must not be used as evidence
for the AWDL ledger's DRAIN_QUEUE or REMOVE_QUEUE completion. A full zero-count
reply likewise names no queue and proves no particular queue was drained.
A flush result describes queue pointers; it does not prove queue deletion or
complete DMA quiescence. Such evidence requires the supported command path,
completion processing and correct host ownership/lifetime fencing.

## Queue and station removal investigation

The current `iwx_rm_sta_cmd` selects base/monitor stations from shared state
and cannot directly remove an independent AWDL peer. `iwx_flush_sta` also
uses base station 0, shared TXFLUSH state, and a drain -> flush -> undrain
sequence, so it is not an AWDL close primitive.

Linux v6.12's
[`iwl_mvm_disable_txq`](https://github.com/torvalds/linux/blob/v6.12/drivers/net/wireless/intel/iwlwifi/mvm/sta.c)
branches on the TX API and whether station removal requires a separate queue
removal. One branch sends `SCD_QUEUE_CONFIG_CMD` with a REMOVE operation and
station-mask/TID fields. Other branches use different lifetime/command rules.
This establishes that removal cannot be inferred by clearing the enable bit
in our legacy 24-byte allocation command. It does not establish which removal
profile our exact hardware and firmware support. Command-version/capability
negotiation, explicit station identity and DMA fence semantics remain necessary
before integrating the resource ledger with firmware.

The legacy response layout is documented in Intel's
[Linux v6.12 TX API header](https://github.com/torvalds/linux/blob/v6.12/drivers/net/wireless/intel/iwlwifi/fw/api/tx.h).
These sources are protocol/reference evidence; no Linux runtime behavior is
claimed for this macOS implementation.

## Validation

`tests/iwx_flush_v1_test.c` runs as C11/C++11 with ASan/UBSan. It covers management
TID with an eight-entry data table, invalid/unmapped bindings, independent
literal bytes, unaligned input, every truncated length, oversized/null replies,
station/count limits, all sixteen entries, a malformed last entry with cleared
output, unexpected TIDs, duplicate queues, ignored unused tail bytes, raw
pointer boundaries, explicit empty/zero-count evidence and 20,000 deterministic
mutations. The full runner passed 15 executions across 10 distinct suites.
A temporary mutation restoring an unchecked data-TID lookup was caught as an
ASan stack-buffer-overflow for TID 15; production source stayed unchanged.
Debug x86_64 Tahoe and Sequoia builds both exited 0 with the pinned MacKernelSDK
and Xcode macOS 26.5 SDK. Each build still emits warnings; these are not
warning-free builds. Both final builds and the full runner were repeated after
the Q0 guard and exited 0. Independent read-only review found no critical or
important issues. Kernel caller branches for station/generation/epoch changes
and zero ring count were source-reviewed; host fixtures do not directly execute
those kernel branches, so real lifecycle behavior remains unverified.
No live drain, queue deletion, coexistence or AirDrop interoperability is claimed.
