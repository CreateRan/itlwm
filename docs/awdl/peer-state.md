# Offline peer observations and reference selection

`awdl_peers.h` is a caller-owned, allocation-free table of at most 16 observed
MAC addresses. It is not wired into the kernel receive path, changes no live
Wi-Fi behavior, and issues no radio/clock command. Addresses identify observed
senders, not authenticated devices or established connections.

## Ownership and time

Initialize with a nonzero unicast local address. Invalid initialization leaves
an existing table unchanged. The caller owns storage, serializes all access,
and provides a consistent monotonic microsecond clock and age policy. Frames
must remain immutable during observation; input/output buffers must be separate
from mutable table storage. No frame pointer or callback context is retained.

Observation, expiry, selection and projection advance one local time watermark.
Backward time is rejected before any table changes. `forget` and `clear` remove
records without resetting the clock; `clear` preserves local identity and the
watermark. A successful explicit initialization resets the entire context.
Slot indexes from `find` are only valid until the next mutation; window output
copies the reference address and observation timestamp instead of a pointer.

## Admission and duplicate ordering

`observe` validates the whole supported discovery/synchronization frame and
source address before changing the table. Local, zero and multicast sources
are rejected. Unsupported semantic profiles and reader failures do not advance
time or refresh records. A fresh address is admitted only into an unused or
expired slot. A full table refuses another address; no fresh peer is evicted.

The local filter uses the 12-bit 802.11 management sequence field, **not** the
16-bit advertised AW counter. Within a live record, modular deltas 1..2047
advance, zero is duplicate, and deltas 2048..4095 are rejected as old/ambiguous.
Wrap 4095 to 0 advances. This is a conservative single-management-stream policy
using the [serial arithmetic principle](https://www.rfc-editor.org/rfc/rfc1982.html),
not a claim that AWDL mandates this admission policy.

Duplicates/old records do not refresh peer or sync timestamps. Valid operations
still advance the local watermark and expire other records before reporting
full/duplicate/old. Long sequence jumps or a peer restart may require expiry or
explicit forgetting before readmission. Filtering resets when a record is
removed: it is not cryptographic replay protection, and spoofed MACs or forged
new sequence values are not authenticated by this component.

## Liveness and synchronization age are independent

Peer expiry uses `now - last_seen > peer_ttl`: exact age equality is retained.
A zero TTL retains a record only at its observation instant. Expiry/removal of
the selected record clears selection before that slot can be reused.

A complete supported sync+sequence snapshot records its own receive time.
Unrelated discovery can refresh peer liveness but never the snapshot timestamp.
A partial sync or sequence update invalidates the old snapshot; fields from
different frames are not silently merged into a fresh anchor. A later complete
snapshot can restore readiness for the same still-selected MAC.

## Explicit reference and window projection

`select` requires an existing peer with a complete snapshot no older than the
caller's sync-age limit. Other live peers are never selected automatically.
A failed selection preserves the prior choice unless normal expiry removed it.
This choice is local policy only: it performs no master election or peer trust
verification, and does not adopt the remote clock.

`project` requires that explicit reference, live peer and fresh complete
snapshot. It delegates to the existing nominal window helper and copies the
address, snapshot time, age and calculated window. Failure zeroes the output.
There is no fallback reference/channel. A stale chosen snapshot remains chosen
but unusable until a fresh complete observation arrives or it is removed.

## APIs and validation

- `init`, `observe`, `expire`, `select`, `project`, `forget`, `clear` operate on
  caller-owned `itl_awdl_peers`; time/TTL/age values have no implicit defaults.
- Selection never supplies authentication, replay security or RF synchronization.
- Future kernel integration needs explicit storage/gate/lifecycle ownership and
  admission/trust policy. No peer table is allocated on a kernel stack here.

Run `sh tests/run-awdl-tests.sh`: six distinct suites, with the peer suite also
compiled as C++11. The C++ check matches the kernel target's diagnostics for C
aggregate zero initialization. Both language runs use ASan/UBSan. Tests cover
capacity, wrap/half-range ordering, independent snapshot age, partial updates,
expiry equality, reference clearing/slot reuse, failed selection, invalid
frames, late reader failure, backwards clocks, zero TTL, UINT64_MAX, clearing,
and 10,000 deterministic state transitions with uniqueness/age/reference
invariants. These verify offline state behavior, not a working AirDrop peer.
