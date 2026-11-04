# Synchronization metadata and nominal window contract

This is offline parser/arithmetic infrastructure, not an active AWDL scheduler.
Nothing here adopts a remote master, sets a clock, changes an Intel PHY context,
installs a timer or transmits a packet. No EFI or installed driver was changed.

## Discovery parser

`itl_awdl_parse_sync(reader, out)` uses the same borrowed, FCS-free 802.11 reader
as discovery/data parsing. It first validates the whole discovery frame, then
extracts synchronization/channel-sequence values. HT Control is accounted for.
Output must be separate writable storage from the frame/reader inputs.
Failure zeroes the entire output; unknown unrelated TLVs are skipped.

Sync TLV 4 has a 33-byte fixed value. Fields extracted include AW/AF periods,
transmit down-counter, advertised AW counter, next/master channels, raw guard
value, master address and presence mode. Fixed-only TLV 4 is supported; a
sequence may instead appear in TLV 18. A valid result can explicitly lack one
or both components; projection requires both.

The bounded profile uses 16 entries, zero duplicates, step count 3 (four AWs),
fill `FFFF` and presence mode 4. The sequence decoder accepts:

| Encoding | Entry bytes | Preserved interpretation |
| --- | --- | --- |
| 0 | channel | Auxiliary byte zero |
| 1 | flags, channel | Flags preserved, not interpreted |
| 3 | channel, operating class | Operating class preserved, not validated |

Sequences may have no trailing padding or the checked zero padding used by the
reference format (two bytes embedded, three standalone). Duplicate TLV 4/18
instances are rejected. One embedded and one standalone sequence are accepted
only when their encoding and all preserved bytes agree. Unknown presence,
encoding, count/duplication/step/fill configurations remain unsupported.

This extracts only selected semantics. AP alignment, extension-length fields,
other flags and election parameters do not drive scheduling here. Guard value
is metadata only. The master address is not evidence that a peer is trusted.

## Pure nominal projection

`itl_awdl_project_window(sync, received_us, now_us, max_age_us, out)` uses
monotonic microseconds provided by its caller, with output storage separate
from the input metadata. Backwards time is malformed;
age strictly greater than the caller's limit is unsupported. The limit has no
implicit default, and zero permits projection only at the observation instant.

For this profile:

- One TU is 1024 microseconds; an EAW contains four AW periods.
- The advertised down-counter is interpreted as time until the next EAW.
- The current EAW base is the advertised 16-bit counter rounded down to a
  multiple of four; nominal phase comes from the down-counter.
- Counter wraps at 16 bits. Sequence index is `(counter / 4) % 16`.
- Boundary instants enter the next window. Next-boundary delays are positive;
  down-counter zero denotes an EAW boundary at the observation instant.
- Channel zero is idle, not an instruction to reuse the infrastructure channel.

The helper only returns relative delays, avoiding subtraction before clock
startup or addition beyond UINT64_MAX. No wall-clock time or floating point
is used. A stale caller-supplied observation is never silently refreshed.

This model follows the narrow four-AW interpretation, not every possible AWDL
configuration. It does not compensate for sender queueing, transmit/receive
latency, integer-TU quantization or local oscillator drift. A receiver must
choose/authenticate its synchronization peer, reject replayed observations,
and validate channel/operating-class/regulatory/firmware compatibility before
any future radio scheduling. Successful arithmetic is not synchronized RF.

## Evidence and tests

Protocol facts were checked against [OWL frame layout](https://github.com/seemoo-lab/owl/blob/master/src/frame.h),
[channel layout](https://github.com/seemoo-lab/owl/blob/master/src/channel.h),
[receive offsets](https://github.com/seemoo-lab/owl/blob/master/src/rx.c),
and [synchronization semantics](https://github.com/seemoo-lab/owl/blob/master/src/sync.c).
The implementation is independent; no reference implementation body is copied.

Run `sh tests/run-awdl-tests.sh` for all five ASan/UBSan suites. Hand-encoded
fixtures provide independent expected fields/timing values. Tests cover split
and unaligned readers, truncated/conflicting TLVs, HT Control, clock reversal,
staleness, exact boundaries, sequence/counter wrap, zero time, UINT64_MAX and
20,000 mutated inputs. These are local byte/model checks, not interoperability
or hardware scheduling results.
