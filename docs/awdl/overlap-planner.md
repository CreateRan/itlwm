# Offline nominal overlap planner

`awdl_overlap.h` is a pure, allocation-free calculation over two immutable sync
snapshots. It is not connected to the driver, peer-table admission, clock
adoption, a radio timer, an Intel queue or a transmission operation. There is
no reservation or permission to transmit implied by an OK result.

## Inputs and ownership

The caller supplies local and peer observations, each containing metadata,
a receive/reference time and a maximum allowed age. Both timestamps and `now`
must use the same local monotonic microsecond domain. A caller using peer state
must explicitly choose a reference/target and copy or guard that snapshot;
this module never selects, authenticates or refreshes a peer.

An explicit policy supplies search horizon, independent local/peer guard
microseconds and a positive whole-transaction duration. Guards are **not**
interpreted from the raw discovery guard byte. Transaction duration is not
estimated here: a future sender must include appropriate PHY, ACK, retry and
switching budgets. Output storage must be separate from input storage; caller
inputs must remain immutable throughout the call.

## Candidate geometry

The planner uses the existing four-AW nominal projection profile. It currently
requires encoding 3 on both sides, and matches identical nonzero channel and
operating-class bytes. It never converts simple/legacy encodings, treats idle
channel zero as available, or infers equality from a channel number alone.

Operating-class/channel equality is only equality of advertised identifiers.
These bytes are not checked against country rules, hardware capabilities,
bandwidth/PHY context or allowed firmware channels. Even unknown nonzero
identifiers can match nominally: a later eligibility layer must reject any
unsupported combination before hardware use.

For each pair of current AWs, it intersects their intervals after removing the
respective guard at each AW's start/end. It clips the result to `now`, the
search horizon, both snapshot age deadlines and the representable clock range.
A transaction must finish no later than the clipped end; exact fitting is
allowed. Adjacent AWs are not merged even when the channel stays unchanged.
This conservative model may decline transactions that a richer scheduler could
place across nominal boundaries.

An OK result reports the earliest found relative wait, nominal available length,
local/peer AW counters, and raw channel/operating class. Times are relative,
so no future timestamp addition overflows inside the planner. A future user
must revalidate freshness, ownership and eligibility before acting on a plan.

## Limits and failure

- Missing/invalid arguments, nonpositive transaction duration, backward clocks
  and malformed projection inputs return INVALID.
- An observation already older than its caller limit returns STALE.
- Missing/unsupported projection profiles or non-operating-class encodings
  return UNSUPPORTED.
- A fresh snapshot can still have no positive remaining lifetime; it cannot
  authorize a transaction crossing its age limit.
- NONE means no fit was found through the entire effective horizon, or the
  policy makes a single guarded AW intrinsically too short.
- LIMIT is different: the bounded 128 boundary segments were exhausted before
  the horizon was fully examined. It is not proof that no later overlap exists.
- All failures leave the output zeroed. Input observations are never modified.

Search advances only to the next local/peer AW boundary or horizon endpoint.
It does not poll microseconds or allocate memory. The step bound is independent
of an arbitrarily large horizon or a tiny nominal AW period.

## Tests and evidence limits

Run `sh tests/run-awdl-tests.sh`: seven distinct sanitizer suites; peer-state
and overlap suites also run as C++11. The overlap suite checks asymmetric phase,
different AW periods, separate guards, exact-fit endpoints, idle slots,
operating-class mismatch, 16-bit counter wrap, future expiry, clock overflow
headroom and the distinct search-limit result.

A separate per-microsecond oracle for small 1-TU models checks 1,000 deterministic
parameter cases. It does not call production projection or overlap helpers;
expected earliest matches and exact nominal available lengths come from a
direct small-time scan. This checks the
nominal algorithm, not real RF timing, trustworthy peers, driver behavior,
regulatory eligibility or working AirDrop.
