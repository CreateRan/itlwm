# Offline IWX AWDL resource ownership

Continue the authorized offline AWDL work in the existing feature checkout.
This increment supplies a pure C/C++ resource ledger, not firmware commands,
DMA ownership, station capability negotiation or a radio TX path. EFI and the
installed driver remain untouched. The implementation choices below are the
builder's decisions under the user's instruction to continue coding.

## Approach

Reusing AP station 0/normal queues conflicts with current shared state.
Immediately wiring experimental station commands into the live driver lacks
validated context binding and removal semantics. Use a bounded offline ledger
first, then integrate only once a central allocator and firmware adapter exist.

## Contract

- Caller serializes access, owns immutable configuration and grants exclusive
  station/queue IDs for the ledger's entire firmware epoch. No other allocator
  may use these grants, including IDs not yet assigned to a peer.
- Maximum 16 stations, 512 queues. Station IDs 0..2 and queue IDs 0..9 are always
  excluded by this conservative profile. Runtime station/queue limits and
  explicit grant sets are required; they are not inferred firmware capability.
- One station and at most one queue per unicast nonzero peer MAC. Reserve the
  lowest granted free station; duplicate MACs and exhaustion fail atomically.
- Queue allocation admission counts both owned queues and in-flight allocation
  requests against the exclusive grants. Exhaustion fails before recording intent;
  this reserves capacity, not a particular firmware-assigned ID.
- Stable lease handle includes epoch, monotonic lease serial and station ID.
  Each command has a distinct ticket serial. Counters never wrap. Stale replies
  cannot change reused resources, later operations, or a new firmware epoch.
- Caller records command intent before submission. Outcomes are SUCCESS,
  REJECTED (authoritative no-effect rejection) and UNKNOWN (possibly applied).
  Transport errors/timeouts must be UNKNOWN, not assumed rejection.
- Order: reserve -> add station -> allocate queue -> ready; teardown: close ->
  drain -> remove queue -> remove station. Reserved-only leases may be canceled.
  No station removal before confirmed queue removal. Failed teardown is retryable.
- Close is monotonic and blocks TX even if an in-flight creation later succeeds.
  Every TX lookup validates the handle and requires ready, not closing, no fault.
- Unknown outcomes quarantine resources until confirmed hardware reset. Unknown
  queue allocation or an invalid/colliding successful queue ID also faults the
  entire ledger, since queue ownership is ambiguous. Never release another peer.
- Reset requires a strictly larger nonzero epoch AFTER caller has stopped DMA,
  reset firmware and fenced old commands. The ledger cannot establish those facts.
  Reset clears leases/faults while retaining grants; handles are per-ledger and
  callers must not reinitialize live storage or mix handles across ledgers.

## Validation

Independent transition tests in C11/C++11 with ASan/UBSan cover positive setup,
normal teardown, rejection at each phase, unknown outcomes, queue collisions,
close during pending creation, stale/duplicate replies, epoch reset, malformed
configuration and counter exhaustion. Tests exercise actual ledger code; they
cannot prove firmware acceptance, DMA safety or AirDrop interoperability.
