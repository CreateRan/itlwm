# Legacy firmware queue lifetime

This increment audits firmware metadata and extends the independent resource
ledger. It does not integrate that ledger into the driver, send removal commands,
or change the installed driver/EFI.

## Bundled firmware evidence

Run `python3 -B scripts/iwx_firmware_audit.py` from the checkout. It inspects
all twelve bundled `*.ucode` images, reports hashes and command/notification
versions, and performs no firmware execution. To inspect one image, pass its
path. Successful output includes only file basenames and public binary metadata.
The full input is checked before JSON output; malformed input fails without a
partial report.

All twelve images in the tested checkout advertise this relevant command set:

| Group / command | Command version | Notification version |
| --- | --- | --- |
| 1 / 0x18 ADD_STA | 12 | 2 |
| 1 / 0x19 REMOVE_STA | 2 | 0 |
| 1 / 0x1d legacy SCD_QUEUE_CFG | 2 | 2 |
| 1 / 0x1e TXPATH_FLUSH | 2 | 1 |
| 5 / 0x17 SCD_QUEUE_CONFIG_CMD | Not advertised | Not advertised |

The local IORegistry reports `IOModel=iwlwifi-cc-a0-68.ucode`. This identifies
the driver's advertised firmware name, not a byte-for-byte readback of loaded
firmware. The repository copy has SHA-256 `4847c55420863d20d111b6a72f1fe2cf9ecd143081c57353020099be6e2af40c`.
A firmware name or API number alone is not a supported-command proof.

The audit validates the 88-byte TLV container header, complete/padded TLVs,
one nonempty command-version table and complete 4-byte entries. Parser acceptance limits
are 32 MiB per image and 4096 bytes per command table. Missing/malformed tables
cannot imply an older command path. Unknown/unhandled versions, including 99,
are not treated as newer supported versions. A duplicated audited command ID
fails closed. Firmware 68 has an unrelated group14/command23 duplicate; this is
reported as ambiguous without choosing or merging versions. All relevant IDs
are unambiguous (177 raw entries, 176 unique IDs).

`legacy_station_bound` is a metadata candidate requiring legacy queue v2/rsp v2,
REMOVE_STA v2/rsp v0, and no advertised newer queue configuration command.
Recognized new-command versions 3/4 are reported as needing a separate backend;
other combinations are unsupported. These labels do not enable any command or
prove station type, binding, queue deletion, DMA fencing or coexistence support.

Intel's [Linux queue API](https://github.com/torvalds/linux/blob/v6.12/drivers/net/wireless/intel/iwlwifi/fw/api/datapath.h)
defines the newer REMOVE operation separately from the old allocation format.
The [Linux station teardown](https://github.com/torvalds/linux/blob/v6.12/drivers/net/wireless/intel/iwlwifi/mvm/sta.c)
selects separate queue removal conditionally. Its
[initialization](https://github.com/torvalds/linux/blob/v6.12/drivers/net/wireless/intel/iwlwifi/mvm/ops.c)
looks up the new queue command with default version 0 and requires separate
removal when that version is positive. This is reference-driver policy, not
proof of macOS DMA safety. The metadata supports investigating
the older station lifetime path here; it does not justify manufacturing a
standalone queue-delete command for this firmware. Exact adapter completion and
DMA semantics still need verification.

## Explicit ledger policy

`config.queue_lifetime` is now required. Unknown/zero and unrecognized values
are rejected. This is an offline API change: callers must explicitly choose
SEPARATE_REMOVE or STATION_BOUND using separately validated evidence. No automatic
profile selection is added to the kernel.

The existing separate-removal path is unchanged: drain -> remove queue -> remove
station. In station-bound mode, independent REMOVE_QUEUE is prohibited. A peer
with a queue follows:

1. DRAIN_QUEUE completes under the existing confirmed-completion contract.
2. REMOVE_STATION confirms station absence. The ledger enters STATION_GONE and
   retains its station lease and queue ownership. Neither ID may be reused yet.
3. RELEASE_HOST_QUEUE confirms the host-side lifetime fence and cleanup. This
   is **not a firmware command**. The adapter must prove no future device DMA is
   possible and finish host cleanup before reporting success; only then are both
   reservations released. A mere station-command acknowledgement is insufficient.

A station with no queue may still be removed directly. A no-effect rejection
preserves the appropriate retry state, a close request remains permanent, and
uncertain outcomes quarantine the lease and known queue until a confirmed reset.
Each stage uses a fresh operation ticket; late station replies cannot complete
host cleanup. Existing capacity accounting includes retained/pending queues.
The ledger keeps station IDs reserved even after firmware removal, deliberately
trading capacity for safe lifetime handling. It cannot verify the adapter's
completion attestations and allocates/frees no DMA memory itself.

## Validation and remaining work

C11/C++11 ASan/UBSan tests cover the new staged transitions, rejection/retry,
uncertain station/host release, retained ownership, invalid policy, wrong-policy
actions and stale callbacks. Python tests use synthetic complete, truncated,
ambiguous and unsupported metadata, plus real CLI exit/output behavior. The
full runner passed all 15 C/C++ sanitizer executions and the five Python test
cases. The final public-binary scan passed for 12/12 images (12 distinct hashes).
Independent read-only review found no critical/important issues and separately
ran the five Python tests successfully. The CLI reads files before applying its
parser acceptance size limit; a bounded streaming input reader remains optional
hardening if inputs extend beyond the trusted bundled/offline workflow.
The bundled-firmware scan is separate evidence against the actual public binaries.
No kernel build or hardware lifecycle verification is claimed for this independent
ledger/tool increment. Next is an explicit station-command adapter and a verified
host queue fence; AirDrop remains unavailable.
