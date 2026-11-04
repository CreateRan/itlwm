# Tahoe native AWDL ABI investigation

Observed locally on macOS 26.7.2, build 25G332 (x86_64).
Collection: `/System/Library/KernelCollections/BootKernelExtensions.kc`.
UUID: `4bf8cd70-1d60-637f-dd1a-ca7be2d3d846`.
SHA-256: `96c12f421a8e99580b5015dfd39877df38ecde9b9e056b8ccd28f15847bca35c`.
No system binary is redistributed by this branch.

## Reproducible symbol/slot checks

Run `python3 scripts/inspect-awdl-abi.py` on the target machine. The script
finds IO80211Family inside the collection and checks five vtable/symbol mappings
using the collection's chained-pointer targets. It only reads the image.
It does not certify function semantics, compatibility of another OS build,
private-symbol availability at kext load time, or successful interface creation.

## Semantic observations from local disassembly

| Method | Observed behavior in this collection |
| --- | --- |
| Controller `createVirtualInterface` | Only role 7 reaches allocation; other roles return null. |
| Controller `attachVirtualInterface` | Calls virtual factory at `0xdd8`, attaches object to provider, optionally registers service. Failed attach releases the factory reference; success returns it to caller. |
| Controller `detachVirtualInterface` | Calls object's terminate with flags `1 + 2 * synchronous`; does not release caller ownership. |
| Controller `bpfOutputPacket` | Frees the supplied mbuf and returns failure. |
| VirtualInterface constructor/metaclass | Inherits Skywalk; system metaclass uses instance size `0x130`, not the legacy header's layout. |
| VirtualInterface `init` | Exported initializer takes `(controller, ether_addr *, unsigned role, const char *)`, at virtual slot `0xe68`. |
| VirtualInterface `createPeerManager` | Role 6 selects `IO80211AWDLPeerManager`. |
| Native create IOCTL | Requires a 32-byte request and dispatches to the infrastructure protocol setter after the WCL path. |
| Native delete IOCTL | Returns unsupported in this build. |

The factory prototype overrides the controller factory for role 6. It allocates
through `OSMetaClass::allocClassWithName("IO80211VirtualInterface")`, checks its
Skywalk base type, and calls the exact exported initializer. This avoids a
guessed object size, a legacy class declaration, and hard-coded virtual calls.
It returns the same initialized/unattached +1 ownership as the system factory.

The native create setter remains unsupported: no attachment caller has been
introduced. Code review rejected driver-owned attachment because failed child
termination was followed by controller/HAL teardown. Keeping the factory
separate allows the ownership contract to be designed before this is wired to
native creation. The opt-in `itlwm_awdl_factory=1` alone creates no interface.

None of these observations supplies Intel firmware queue/station/channel
binding or the native peer data path. Creating a system AWDL interface alone
cannot make AirDrop work. The factory prototype remains untested on hardware.
