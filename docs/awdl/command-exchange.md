# Compiled, disabled IWX command exchange

This increment adds `ItlIwx::iwx_awdl_exchange`, a synchronous kernel transport
entry using the existing `iwx_send_cmd` machinery and the shared host-tested
`iwx_command_exchange.h` policy. Its private enable flag is initialized false;
there is no setter, boot argument or call site. It therefore returns EOPNOTSUPP
before touching transport state. No command was issued, no kext was installed
or loaded, and EFI remains unchanged. Enabling it requires a later source change
and validated lifetime/context integration, not just an EFI parameter.

## Command preparation and wire versions

The shared planner checks the active ledger ticket, exact current firmware
command/reply versions, complete request bytes and a per-driver binding containing
ledger epoch, driver generation, stop epoch and (where available) DMA epoch.
The command must be pending for the same lease/station/action. Global resource
faults block both new exchanges and exposing successful replies, even when the
operation was already pending. The pool is never modified by planning/capture.

Only the prepared station ADD, drain-flow MODIFY, single-TID FLUSH and station
REMOVE profiles are accepted. The planner independently reconstructs their
allowed bytes using the current peer/station identity and rejects other fields.
Context ownership, station role and real queue/TID binding still need validation
outside this module. A version-table match alone is not a hardware capability
or coexistence proof.

Layout version is distinct from command-header version. Existing local
`iwx_send_cmd` sends bare legacy opcodes by promoting their group internally and
setting CMD_IS_NARROW. The RX path uses that flag to route LONG_GROUP replies
through the legacy response cases. Packing the layout version/group directly
into the command ID would bypass that association. The planner deliberately
returns bare IDs 0x18, 0x1e or 0x19, preserving wire header version 0, while
separately requiring the expected firmware layout versions. This follows the
current driver convention; it is not a generic mapping for other commands.

## Gate, slot and incarnation

The compiled kernel entry requires its owning softc, an already-held main command
gate, a ready command ring and no concurrent experimental exchange. It does not
queue borrowed caller storage for later use. It makes private copies of the
request and epoch binding before sleeping. The slot is captured under the gate
immediately before calling the existing synchronous sender; that sender allocates
its response buffer and records pending/waiting ownership for the selected slot.
Ordinary command dispatch behavior is not changed by this entry.

A dedicated nonwrapping stop epoch is advanced in `iwx_stop_device` under the
gate in all builds. Its only effect on existing paths is that bookkeeping write.
This covers stops that do not change `sc_generation` and builds without the VTD
DMA epoch. Counter exhaustion permanently becomes zero and refuses transport
bindings, rather than reusing a wrapped value. It is scoped to one driver instance.

After the sender returns, shutdown/hardware-error flags and (where enabled)
DMA detaching also prevent reply exposure, even before a reset advances epochs.
Before exposing a response, the capture policy rechecks all binding fields and
the still-pending resource ticket. It validates the owned allocation's capacity,
declared frame length, command opcode, expected command-ring slot, queue 0,
legacy/long response group and exact payload size. Malformed, failed, unsolicited
or mismatched frames expose no bytes. The response buffer is freed exactly once
on the successful-send ownership path. On send error/reset, the existing sender
retains responsibility for its old buffers.

These checks invalidate an exchange that crosses a known stop/reset. They do
not authenticate a firmware packet or distinguish an indistinguishable duplicate
that arrives in a later reused slot after reset. Safe hardware reuse still
requires actual device/DMA reset fencing and the original command-ring sequencing
contract. The existing sender's slot lifetime assumptions have not been hardware
validated for this experiment.

## Ownership and completion limits

A future controller must establish an epoch binding only for a valid current
firmware/ledger incarnation, pin the controller and ledger storage for the whole
synchronous call, keep original request storage immutable, and serialize all
lifecycle operations. Bindings/plans are per-driver-instance; do not transfer
old plans to a recreated driver or rebind an old live ledger after reset. The
entry adds no controller factory, binding initializer or lifetime pinning API.

The caller is responsible for exactly-once stage dispatch and cancellation;
this transport does not retry or manage timers. It refuses concurrent experimental
exchanges, but is not a persistent anti-replay journal. It also collects no
host ring/DMA evidence and does not manipulate resource state after transport.
A valid payload must still pass the station/drain adapter with the original
request. The REMOVE transport profile currently accepts only an empty ack;
notification version 0 does not itself establish that framing. Its actual reply
shape still needs verification, and nonempty replies fail closed in this narrow
profile. The synthetic empty-ack fixture proves no hardware compatibility.
An empty REMOVE ack is transport evidence only and cannot confirm station
absence. Neither transport success nor a frame match proves drain, station
removal or DMA quiescence. Error codes that could occur after submission remain
uncertain; a generic send error must not be treated as an atomic no-effect result.

## Validation

C11/C++11 ASan/UBSan tests cover bare command IDs despite layout versions,
reconstructed station identity, current pending tickets, opcode/group/slot/queue
mismatches, failed/short/wrong-sized frames, legacy response group, empty removal
ack without ledger completion, each epoch field, permanent counter exhaustion,
global fault refusal and the actual exchange -> drain-adapter FLOW/FLUSH chain.
Kernel builds check the unused entry against real command transport types.
The full final runner passed all 21 C/C++ sanitizer executions and five Python
cases. Final Debug x86_64 Tahoe and Sequoia default non-VTD builds both exited 0
using pinned MacKernelSDK and Xcode macOS 26.5 SDK; compiler/linker warnings
remain. Symbol inspection confirms the exchange entry was linked. The VTD
conditional branch was not compiled or hardware-tested in this increment.
Independent read-only review found no critical/important issues, checked the
post-send fault/cleanup hunk and ran the focused C11/C++11 sanitizer fixture.
Neither the main-gate entry nor its disabled flag/lifetime branches were executed
on hardware. Live enablement and AirDrop interoperability remain unavailable.
