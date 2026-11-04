# AWDL development status

Branch: `feature/airdrop-prototype`, based on laobamac/itlwm `7204c69`.

The intended end-to-end test is a small text file sent from macOS Tahoe to
an iPhone using Finder AirDrop. **This branch does not yet implement AirDrop.**

## Implemented: passive discovery-frame receive diagnostics

`itlwm_awdl_diag=1` enables an observer before the net80211 infrastructure
BSSID/state filters. It logs at most 32 structurally valid AWDL discovery/data frames
per driver start, sharing one budget across both kinds. With no boot argument the observer is disabled. This path
neither changes radio filters/channels nor consumes, modifies, or retains the
received mbuf. It does not create `awdl0` or advertise AWDL capabilities.

The allocation-free parser reads across mbuf boundaries, checks the Apple OUI,
AWDL type/version/subtype and TLV lengths, and reports timestamps, addresses and
TLV presence. Unknown TLVs are skipped. Encrypted and fragmented frames are not
parsed. The reader expects FCS-free 802.11 frames, without a radiotap prefix.
The 4096-byte maximum bounds work on untrusted frames. The generic parser validates TLV extents; the separate synchronization parser
extracts the limited semantics described below. No election state is adopted.

A log such as `itlwm: AWDL RX subtype=3 ...` establishes that an AWDL action
frame reached the software receive path. It does not establish peer discovery
in Finder, protocol synchronization, or successful delivery to an iPhone.
No logs also cannot establish that the iPhone sent nothing: firmware filters
and current channel selection can prevent these frames from reaching software.
The diagnostic does not enable monitor mode or change the infrastructure link.

## Implemented offline: data wire codec

`awdl_data.h` adds a no-allocation codec for a deliberately narrow plaintext
format profile. The encoder writes **only a 16-byte LLC/SNAP + AWDL data header**:
Apple OUI `00:17:f2`, protocol ID `0x0800`, data header marker `03 04`, caller-owned
16-bit little-endian sequence, zero padding and big-endian EtherType. It neither
allocates a sequence nor writes an 802.11 header/payload, dequeues a packet, or
transmits anything. Output remains untouched on invalid arguments/short capacity.

The decoder accepts a borrowed FCS-free 802.11 ordinary Data or QoS Data frame,
handles optional QoS HT Control, and checks the AWDL BSSID, LLC/SNAP prefix,
marker/padding and EtherType. It extracts addresses, sequence and a bounded
payload range without copying/delivering that payload. Encrypted/fragmented,
ToDS/FromDS/four-address and A-MSDU frames are unsupported. Other AWDL variants
need separate evidence and implementation; this is not a universal decoder.

EtherType and payload are opaque: returning a valid wire header does not prove
IPv6 validity, checksum integrity, peer identity, replay freshness or successful
AirDrop. The 4096-byte cap is a parser work limit, not an advertised radio MTU.
The same decoder runs in the existing passive diagnostic observer and tests.
`AWDL DATA RX` logs consume the existing shared 32-frame budget and do not
consume, alter, redirect or deliver the original mbuf.

The user requested offline development; no installed kext, EFI file, or boot
argument was changed. Unit fixtures use invented addresses and byte literals,
not captures containing real device data.

## Implemented offline: synchronization metadata and nominal windows

`awdl_sync.h` parses synchronization TLV 4 and embedded/standalone channel
sequences (TLV 18) from a structurally valid discovery frame. The supported
profile uses 16 entries, presence mode 4, step count 3, zero duplication and
`FFFF` fill. Simple, legacy and operating-class encodings preserve channel
and auxiliary bytes without interpreting radio eligibility. Duplicate relevant
TLVs, contradictory embedded/standalone sequences, bad lengths and padding
are rejected without exposing a partial result.

The pure projection helper consumes a caller-provided observation time and
freshness limit. It returns a nominal AW counter, sequence index/channel,
and relative times to the next AW/EAW boundary, with 16-bit wrap and integer
TU conversion. It never installs a timer, updates a clock, elects a master,
changes channel or submits a frame. A zero channel means an idle slot.
The helper follows the documented four-AW profile; peer trust, RX timestamp
bias, clock drift, guard times and regulatory/firmware constraints remain work
for a real scheduler. See [synchronization contract](synchronization.md).

The existing passive observer optionally prints `AWDL SYNC RX` metadata for
supported observations, using the same 32-frame budget. Unsupported semantic
profiles keep the generic discovery diagnostic. No observation automatically
becomes synchronization state or a radio command.

## Implemented offline: bounded peer state and explicit reference

`awdl_peers.h` maintains at most 16 observed MAC records without allocation or
live RX integration. It refuses fresh additions when full, filters duplicate
and conservatively old/ambiguous 12-bit management sequences, and expires
records using caller-provided monotonic time and TTL. Invalid observations do
not mutate state. Duplicate/old observations cannot refresh liveness or sync
age; unrelated discovery cannot extend a synchronization snapshot's age.
Partial timing/sequence updates invalidate an old anchor instead of merging
observations from different frames.

The caller must explicitly select an existing, live peer with a fresh complete
snapshot. Removal/expiry clears the choice before slot reuse. Projection copies
that peer's identity/time and delegates to nominal window arithmetic; it never
chooses a replacement, trusts a sender or adopts a clock. Clear preserves the
local identity/time watermark. See [peer-state contract](peer-state.md) for the
sequence-reset, clock, expiry and security limits.

## Implemented offline: guarded nominal overlap candidates

`awdl_overlap.h` searches two caller-provided snapshots for matching nonzero
channel/operating-class identifiers. Independent caller-provided guards trim
both sides of each nominal AW. The whole requested transaction must fit, and
the search is clipped to both freshness deadlines, the caller horizon and
clock headroom. A fixed 128-segment budget bounds work and returns LIMIT rather
than claiming no later window exists.

This is a pure encoding-3 candidate calculator: no master choice, clock
adoption, reservation, timer, firmware operation or live integration. Identifier
equality is not regulatory/firmware eligibility. It does not merge adjacent
AWs or estimate airtime/guard uncertainty. See [overlap contract](overlap-planner.md).

## Implemented: opt-in native Tahoe creation and framework-owned teardown

`itlwm_awdl_vif=1` opts into the native creation experiment and enables its
factory. With no argument the native setter remains unsupported. The earlier
`itlwm_awdl_factory=1` still enables only the factory; it does not enable native
creation. Sequoia retains its previous unsupported setter.

The 32-byte version-1 AWDL request is gated, validates role 6 and a nonzero
unicast address, and creates at most one direct service-plane child. Ownership
is recorded before asynchronous registration. Same-address requests reuse the
child; conflicting addresses return busy. A terminating child is retained but
returns not-ready until actual detach, after which a request may create another.
No BSD name yet also returns not-ready; the kernel does not claim success just
because it allocated an object. At most 16 request-result logs are emitted.

Creation opens only at the final successful startup point and rejects inactive
controllers. `willTerminate` closes the creation gate and preserves the system
superclass callback. **The driver never force-terminates the child from stop.**
IOKit owns child stop/detach ordering; controller stop releases the original
factory reference after framework child detach, before HAL teardown. This
avoids a synchronous termination wait inside a workloop gate and avoids a
failed termination followed by unconditional controller resource destruction.
The normal IOService termination contract is required; manually calling
controller stop outside the framework is not a supported entry point.

These source-level ownership rules and lifecycle tests do not verify loaded
kernel callbacks. Real creation of `awdl0`, BSD registration, stopping,
suspend/resume and private-symbol binding remain unverified on hardware.
The experiment does not set link-up, change firmware channels, advertise AWDL
capabilities, bind firmware stations/queues, or implement peer packet transport.
AirDrop remains unavailable. See [ABI and lifecycle evidence](tahoe-abi.md).

## Validation

Run `sh tests/run-awdl-tests.sh`. Tests use hand-encoded protocol fixtures,
including truncated TLVs, unknown types, fragmented/protected frames, HT Control,
every possible two-segment split, unaligned buffers, reader failure, oversized
lengths, and 20,000 deterministic mutated inputs under ASan/UBSan.
The parser is shared by these tests and the actual receive path. Factory
policy tests cover opt-in, roles, and invalid MACs. Lifecycle tests exercise
creation failure/retry, duplicate attempts, pending/refused termination, closing
during creation, and detach/recreation. They test the actual lifecycle state
class used by the driver, not an emulation of IOKit callbacks. Data-codec tests
use independent golden bytes for encode and decode (not just a round-trip),
including short output buffers, 802.11/QoS/HT truncation, unaligned chained
buffers, exact 4096-byte bounds, reader failures, unsupported profiles and
20,000 additional deterministic mutated inputs under ASan/UBSan.
Synchronization tests independently encode fixed/embedded/standalone TLVs,
exercise all three encodings, reader failures, conflicting sequences, split
buffers and nominal timing boundaries (including 16-bit wrap and UINT64_MAX),
plus another 20,000 deterministic mutations. Peer-state tests additionally
cover bounded admission, duplicate/wrap ordering, independent ages, explicit
selection, removal/expiry, clock limits, partial updates and 10,000 state
transitions. The peer suite runs as both C11 and C++11 under ASan/UBSan.
The new table is not integrated into the current driver; no kernel build or
live deployment is claimed for this table increment. The overlap suite adds
literal boundary cases and an independent per-microsecond oracle over 1,000
parameter combinations; it runs as C11 and C++11 under ASan/UBSan. No driver
build or deployment is claimed for the independent overlap module.

Build with the pinned MacKernelSDK and the commands in the repository README.
Artifacts stay in `build/`; installing a kext and changing EFI are separate
hardware validation steps. No hardware validation has been performed for this
branch. Do not interpret a successful build as verified AWDL interoperability.

## Next dependencies

While installation is deferred, continue offline work on synchronization,
channel scheduling and firmware queue/station requirements. Hardware creation
and over-the-air validation remain prerequisites for interoperability claims.

Hardware-dependent path:

1. Validate the native creation experiment on hardware: confirm private-symbol
   binding, request arrival/results, actual BSD registration, child detach and
   controller teardown. Check repeated requests and Wi-Fi/sleep transitions.
   The design relies on IOService service-plane ordering; ABI symbols/slots
   were inspected on 25G332 and other builds need their own check.
2. Establish Intel firmware MAC/PHY context, station/queue binding and action
   frame transmission on a single channel. Confirm transmission with a second
   receiver; a software TX counter alone is insufficient.
3. Complete election and extended synchronization semantics, implement
   channel/time-event scheduling, then route AWDL data TX/RX to the macOS peer
   interface with bounded peer state, cancellation and suspend/resume cleanup.
4. Validate IPv6 neighbor discovery and service discovery, then the Finder to
   iPhone text-file transfer. ACK/discovery traffic is needed in both directions.

## Protocol references

Wire-format reference (facts only; implementation is written independently):
[OWL frame definitions](https://github.com/seemoo-lab/owl/blob/master/src/frame.h)
and [OWL receive path](https://github.com/seemoo-lab/owl/blob/master/src/rx.c).
See also [upstream AWDL investigation](https://github.com/OpenIntelWireless/itlwm/issues/1062)
for reports of virtual interfaces without a working Intel hardware data path.


## IWX backend groundwork

The [firmware backend investigation](iwx-backend.md) maps the shared MAC/PHY,
station, queue and time-event state that prevents directly reusing the current
Wi-Fi path for concurrent AWDL. The legacy queue-v2 codec is integrated into
both existing IWX queue configuration handlers. It adds strict failure-flag
rejection and bounds/argument validation, with independent wire fixtures in
C11/C++11 under ASan/UBSan. It does not activate an AWDL hardware path.


## Offline resource ownership

The [IWX station/queue ledger](resource-ownership.md) adds exclusive ID grants,
epoch/lease/operation tickets, ordered teardown and fail-closed quarantine for
uncertain outcomes. It is independent of the driver. Integration still requires
a central allocator, command/version negotiation, real firmware completion and
DMA/reset fencing. C11/C++11 sanitizer tests exercise its actual transitions.


## Flush reply and teardown evidence

The existing IWX flush handler now uses the tested legacy reply decoder and
bounds-checked management/data queue binding helper. It validates all entries
before updating rings and distinguishes no-payload compatibility from positive
queue evidence. See [flush validation and removal investigation](flush-evidence.md)
for the TID bounds finding, integration details and unresolved firmware removal
profile. This does not activate AWDL commands or the resource ledger.


## Legacy queue lifetime

The [firmware metadata audit and queue lifetime extension](queue-lifetime.md)
identify the bundled firmware's older command profile and add a required explicit
policy to the offline ledger. Station removal and host queue release are separate
stages, so a station acknowledgement cannot release a queue still owned by DMA.
The Python metadata audit runs with the test runner; it does not select kernel
commands or activate the independent ledger.
