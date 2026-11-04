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

## Implemented: Tahoe factory-only prototype

`itlwm_awdl_factory=1` opts into a **Tahoe-only factory prototype**. It only
accepts AWDL role 6 and nonzero unicast addresses, and only runs within the
controller workloop gate. Other roles delegate to the system factory. The
factory uses the system metaclass allocator and exported initializer without
including the legacy class declaration. It returns an initialized, unattached
system object with one reference, matching the system factory contract.
On failure it releases the allocation. Controller shutdown disables new
factory calls under the gate before tearing down dependencies.

**The native create setter remains unsupported.** This code does not call
attach/register, keep an interface pointer, or introduce driver-owned child
cleanup. The boot argument alone therefore cannot create `awdl0`. This is a
foundation for a future attachment owner, not a usable interface feature.
Sequoia retains its previous factory behavior. Neither a live factory call nor
private-symbol resolution at kext load time has been verified on hardware.

Review rejected the initial driver-owned attachment approach: a child refusing
termination could outlive HAL/gate/workloop teardown. An ownership design that
keeps all callback dependencies alive through child termination must precede
native creation. Simply retaining a child reference or retrying termination is
insufficient. See [local ABI evidence](tahoe-abi.md).

## Validation

Run `sh tests/run-awdl-tests.sh`. Tests use hand-encoded protocol fixtures,
including truncated TLVs, unknown types, fragmented/protected frames, HT Control,
every possible two-segment split, unaligned buffers, reader failure, oversized
lengths, and 20,000 deterministic mutated inputs under ASan/UBSan.
The parser is shared by these tests and the actual receive path. Factory
policy tests cover opt-in, roles, and invalid MACs; they do not emulate or
validate IOKit object allocation or lifecycle calls.

Build with the pinned MacKernelSDK and the commands in the repository README.
Artifacts stay in `build/`; installing a kext and changing EFI are separate
hardware validation steps. No hardware validation has been performed for this
branch. Do not interpret a successful build as verified AWDL interoperability.

## Next dependencies, in order

1. Design ownership for native interface attachment, including termination
   refusal, registration timing, repeated requests, and controller teardown.
   Then validate lifecycle on hardware. ABI symbols/slots were inspected on
   25G332; other builds need their own check.
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
