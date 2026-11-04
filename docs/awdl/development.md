# AWDL development status

Branch: `feature/airdrop-prototype`, based on laobamac/itlwm `7204c69`.

The intended end-to-end test is a small text file sent from macOS Tahoe to
an iPhone using Finder AirDrop. **This branch does not yet implement AirDrop.**

## Implemented: passive discovery-frame receive diagnostics

`itlwm_awdl_diag=1` enables an observer before the net80211 infrastructure
BSSID/state filters. It logs at most 32 structurally valid AWDL PSF/MIF frames
per driver start. With no boot argument the observer is disabled. This path
neither changes radio filters/channels nor consumes, modifies, or retains the
received mbuf. It does not create `awdl0` or advertise AWDL capabilities.

The allocation-free parser reads across mbuf boundaries, checks the Apple OUI,
AWDL type/version/subtype and TLV lengths, and reports timestamps, addresses and
TLV presence. Unknown TLVs are skipped. Encrypted and fragmented frames are not
parsed. The reader expects FCS-free 802.11 frames, without a radiotap prefix.
The 4096-byte maximum bounds work on untrusted frames. Only structural TLV
validation is implemented; synchronization/election TLV contents are not used.

A log such as `itlwm: AWDL RX subtype=3 ...` establishes that an AWDL action
frame reached the software receive path. It does not establish peer discovery
in Finder, protocol synchronization, or successful delivery to an iPhone.
No logs also cannot establish that the iPhone sent nothing: firmware filters
and current channel selection can prevent these frames from reaching software.
The diagnostic does not enable monitor mode or change the infrastructure link.

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
class used by the driver, not an emulation of IOKit callbacks.

Build with the pinned MacKernelSDK and the commands in the repository README.
Artifacts stay in `build/`; installing a kext and changing EFI are separate
hardware validation steps. No hardware validation has been performed for this
branch. Do not interpret a successful build as verified AWDL interoperability.

## Next dependencies, in order

1. Validate the native creation experiment on hardware: confirm private-symbol
   binding, request arrival/results, actual BSD registration, child detach and
   controller teardown. Check repeated requests and Wi-Fi/sleep transitions.
   The design relies on IOService service-plane ordering; ABI symbols/slots
   were inspected on 25G332 and other builds need their own check.
2. Establish Intel firmware MAC/PHY context, station/queue binding and action
   frame transmission on a single channel. Confirm transmission with a second
   receiver; a software TX counter alone is insufficient.
3. Parse synchronization/election/channel-sequence contents and implement
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
