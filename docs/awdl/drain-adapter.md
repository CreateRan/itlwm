# Offline staged drain adapter

`iwx_drain_adapter.h` adds a real offline state machine over the existing resource
ledger and flush decoder. It constructs commands and checks supplied evidence;
it does not send a command, stop TX/DMA, reclaim descriptors or prove a hardware
fence. Its header is compiled into the kernel target for ABI checks only. No
adapter dispatch is enabled and EFI/installed kexts are untouched.

## Three stages and callback identity

Preparation requires a current ready lease with a queue, explicit queue TID,
caller-validated context ID/color and exact version pairs: station 12/reply 2,
flush 2/reply 1. It builds the request before starting DRAIN_QUEUE. That ledger
intent closes admission immediately. The backend must actually fence the real
TX path at this point; changing offline state cannot do that. Successful prepare
returns a fresh session and immutable command carrying the original epoch,
lease, station and operation serial. Input/output storage must be separate.

1. FLOW: 48-byte ADD_STA modify request sets only DRAIN_FLOW and its mask for
   the explicit station/context. Successful transport, no failure flag and an
   exact 4-byte status with success low byte permit the next stage. This alone
   does not complete DRAIN_QUEUE.
2. FLUSH: 8-byte TXPATH_FLUSH request names the same station and only the explicit
   TID. The entire fixed 132-byte response is validated by the existing decoder.
   It must contain exactly one matching queue/TID entry. Empty payload, zero
   entries, wrong ownership, malformed replies or failed transport quarantine the
   lease and retain queue ownership. A valid reply records raw before/after read
   pointers and advances to HOST, while the ledger remains pending.
3. HOST: a separately obtained live snapshot must match epoch/station/queue/TID,
   a validated ring geometry and explicit sequence-number domain. Only zero
   outstanding packets, equal producer/consumer, matching normalized firmware
   read-after position, a full-interval TX fence and DMA quiescence can complete
   DRAIN_QUEUE. The queue remains owned in QUEUE_IDLE; neither station nor host
   queue is released by this adapter.

Each callback must carry the original immutable command, not a mutable current
command buffer. Stage, opcode/version/length and full ticket must match the
active ledger operation. A late FLOW callback cannot act as FLUSH confirmation;
a previous lease/operation or firmware epoch cannot mutate the current ledger.
Malformed session TIDs are rejected before bit shifts. These are software
correlation checks, not proof that an actual radio reply belongs to a command.
The transport adapter must still correlate hardware command-ring sequence and
incarnation, and fence recycled slots/reset races.

## Host evidence and waiting

All calls must execute under caller serialization, including obtaining/using
snapshots and acting on outcomes. The session is internal mutable state; callers
may not edit its fields or overwrite it with a second prepare while pending.
Contexts and TID-to-queue binding must be validated against the same firmware
incarnation; this module does not infer those from the base Wi-Fi tables.

`tx_blocked=1` certifies the entire interval since prepare, not merely the instant
of the snapshot. Otherwise it cannot be known whether new frames raced the flush.
A host identity mismatch, invalid geometry or broken TX fence quarantines the
operation. A valid snapshot that still has queued work, unmatched positions or
no DMA quiescence returns STATE and stays pending for another snapshot. The
caller needs a bounded deadline and must call `itl_iwx_drain_abort` on timeout,
transport cancellation or shutdown; abort records UNCERTAIN and retains resources.
No timer, retry dispatch or asynchronous worker is created here.

The accepted ring count is a power of two from 8 through 65536, and the supplied
sequence modulus must be a power of two at least as large, up to 65536. These
are bounded calculation inputs, not hardware capability statements. Producer
and consumer are sequence-domain positions from independent host completion
bookkeeping; copying the tested firmware reply into those fields would not prove
a host drain. Raw firmware read-after is reduced
by the explicitly supplied sequence modulus; it is never reduced by ring size.
The backend must separately validate that this normalization describes its
actual hardware/firmware. Equal low bits alone cannot establish hardware drain.

DMA quiescence is an external attestation that this module cannot measure. It
must cover device DMA lifetime, completion processing, descriptor ownership and
future accesses. No host snapshot or firmware read pointer substitutes for that
proof. Station deletion and the later host-queue release retain their separate
contracts in the resource ledger.

Wire facts are checked against local packed structures and Intel's
[station API](https://github.com/torvalds/linux/blob/v6.12/drivers/net/wireless/intel/iwlwifi/fw/api/sta.h)
and [TX API](https://github.com/torvalds/linux/blob/v6.12/drivers/net/wireless/intel/iwlwifi/fw/api/tx.h).
The evidence policy above is a conservative design choice, not a claim those
headers document a complete macOS DMA fence protocol.

## Offline verification

Tests exercise independent literal FLOW/FLUSH bytes, mandatory three-stage
progression, late callbacks, reset, malformed/empty/wrong-queue replies, incomplete
host evidence (each waiting guard independently), geometry endpoints 8/65536,
sequence domains distinct from ring size, interval-fence/geometry failures,
malformed session TIDs, extra queues and abort at each stage. Fixtures explicitly
supply host/firmware evidence and do not perform real TX or DMA. The final full
runner passed all 19 C/C++ ASan/UBSan executions and five Python test cases.
Debug x86_64 Tahoe and Sequoia builds both exited 0 using pinned MacKernelSDK
and Xcode macOS 26.5 SDK; compiler/linker warnings remain. Independent read-only
review found no critical/important issues and separately ran the drain C11/C++11
sanitizer fixtures. Its suggested isolated waiting-condition and geometry-boundary
cases were added, then the full runner passed again.

Next integration still requires an independent MAC/PHY context, a real TX
admission fence, command-ring transaction association, live completion and host
snapshot collection, deadlines and reset/DMA fencing. AirDrop remains unavailable.
