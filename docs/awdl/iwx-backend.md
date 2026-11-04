# IWX firmware backend investigation

This increment hardens the existing Wi-Fi queue command path. It does not
allocate an AWDL station, create a second MAC/PHY context, transmit AWDL frames,
or connect the offline overlap planner to hardware. No candidate kext has been
installed or loaded.

## Existing context constraints

Inspection of `itlwm/hal_iwx/ItlIwx.cpp` and `if_iwxvar.h` shows:

| Area | Current implementation | Required before concurrent AWDL use |
| --- | --- | --- |
| MAC context | `iwx_mac_ctxt_cmd_common` supports STA and monitor; active state uses one `IWX_FLAG_MAC_ACTIVE` | Explicit second-context identity, supported firmware MAC type, independent lifecycle |
| PHY binding | `iwx_binding_cmd` uses shared binding-active state; normal association uses PHY context 0 | Ownership and coexistence rules for each MAC/PHY binding |
| Station | Normal station 0, auxiliary 1, monitor 2; `iwx_add_sta_cmd` has shared station-active state | Peer/station allocation and teardown independent of the AP |
| Queue | `iwx_tvqm_enable_txq` uses station 0; `sc_tid_data` and default data queue serve the existing interface | Per-peer queue ownership, completion routing and cancellation |
| Time event | One `sc_time_event_uid` and shared active state | Per-context event identity, cancellation and suspend/reset handling |

This is a source-level dependency map, not evidence that firmware accepts a
particular AWDL configuration. The IWM backend has not been implemented or
validated for AWDL by this investigation. Reusing the current queue or changing
station 0 to an iPhone would interfere with the existing AP association.

## Legacy queue command codec

`iwx_txq_v2.h` independently serializes the existing 24-byte
`IWX_SCD_QUEUE_CFG` request and decodes its exact 8-byte reply. This is the
legacy API v2 layout, not the later station-mask command variants. The source
for the wire contract is Intel's published
[Linux firmware API header](https://kernel.googlesource.com/pub/scm/linux/kernel/git/torvalds/linux/+/1c8c5a9d38f607c0b6fd12c91cbe1a4418762a21/drivers/net/wireless/intel/iwlwifi/fw/api/txq.h).
The response flags field is documented as set on failure. Both current queue
handlers now reject nonzero flags before adopting the returned queue/index.
This closes an unchecked response condition; no hardware failure has been
attributed to it. Linux's legacy response consumer also does not check this
field, so this change is not described as copying an upstream fix.

The encoder preserves the current station/TID, enable flag, exponent and DMA
addresses for valid existing requests. The static handler rejects an out-of-array
queue ID before indexing or resetting a ring. Invalid station/TID/slot inputs
are rejected before command submission; the dynamic handler validates them
before allocating DMA. Existing queue-ID, write-index and AIRPORT_VTD epoch,
collision and deferred-recycling policies remain in their original callers.
ABI assertions tie the tested codec to every packed request/response field.

The codec accepts byte-sized station IDs, TIDs 0..15 and power-of-two slot
counts 8..65536 as a bounded serialization profile. These are not hardware
capability claims. In particular, it preserves the existing 1024-slot static
Gen3 path despite the older API comment mentioning 256. The dynamic path keeps
its existing 256-slot default and AIRPORT_VTD maximum. DMA addresses, including
zero, are encoded verbatim; the caller must own valid DMA allocations. Queue
ownership, actual station capacity, firmware versions and index interpretation
are outside the codec. It does not normalize a sequence index to a ring index.

## Offline validation

`tests/iwx_txq_v2_test.c` uses independent literal request/reply bytes. Coverage
includes both bytes of failure flags, exact response lengths, unaligned input,
short output buffers with untouched sentinels, address byte order, the existing
1024-slot encoding and boundary/invalid parameters. It runs in C11 and C++11
with ASan/UBSan as part of `sh tests/run-awdl-tests.sh`. The full runner passed
all 11 executions (8 distinct suites). Debug x86_64 Tahoe and Sequoia builds
both exited 0 using the pinned MacKernelSDK and Xcode macOS 26.5 SDK. Builds
still emit warnings, including deprecated APIs, aggregate initialization,
unused variables and deployment-target settings; these are not warning-free
builds. Neither build was loaded or tested on hardware.

Before implementing actual AWDL TX, introduce explicit ownership/state for
MAC/PHY bindings, peer station/queue handles and event lifetime. Validate command
version/capability negotiation independently of wire encoding. Radio scheduling,
Wi-Fi coexistence, DMA teardown and actual over-the-air operation still require
hardware validation; a successful offline build cannot establish these.
