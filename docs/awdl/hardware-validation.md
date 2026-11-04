# Native AWDL creation: first hardware validation

This validates interface creation and lifetime, not AirDrop. No firmware peer
transport is implemented. Do not use file transfer as the pass/fail criterion.
The experimental build has not yet been loaded on a machine.

## Deployment scope

The candidate is the Tahoe Debug `AirportItlwm.kext` from this branch. It uses
the existing bundle identifier and version; a successful rebuild does not prove
that firmware/EFI loaded this candidate. Preserve the current working EFI kext
and boot arguments before a separately authorized installation. Do not install
the Sequoia artifact on Tahoe. No installation is performed by the test suite.

Enable `itlwm_awdl_vif=1` for native creation. `itlwm_awdl_diag=1` separately
enables passive RX diagnostics. The native switch already enables its factory.

## Observe after loading the candidate and rebooting

Inspect kernel logs for `itlwm: AWDL native lifecycle experiment ready`.
This confirms the instrumented driver reached the experimental startup point.
With normal Wi-Fi working, open Finder AirDrop and make the iPhone discoverable.
Record request result logs and inspect interfaces without changing them:

```sh
ifconfig -l
ifconfig awdl0
ioreg -r -c IO80211VirtualInterface -l
log show --last 10m --style compact --predicate 'eventMessage CONTAINS "itlwm:" AND eventMessage CONTAINS "AWDL"'
```

- No startup marker: establish which binary/boot arguments actually loaded.
- Startup marker but no create logs: determine the WCL/native request route.
- Child-attached marker but only not-ready results: inspect BSD registration;
  the object allocation alone is not a successful interface.
- A real BSD name and a success result establish creation only. Link activity,
  peer discovery and packet delivery are separate requirements.
- The first 16 create results and first 32 passive RX observations are bounded;
  silent logs after the budgets expire are not evidence that requests stopped.

## Teardown observations

Once basic creation is confirmed, a controlled reboot should show lifecycle
quiescence and, where shutdown logs are available, `AWDL framework stop
childAttached=0`. A value of 1 contradicts the assumed framework ordering and
must be investigated before enabling the feature again. Do not claim shutdown
verified merely because no panic was noticed or because shutdown logs are absent.

Repeated Finder requests, Wi-Fi off/on, and sleep/resume need separate hardware
observations. Stop any failing experiment by restoring the backed-up working
kext and original boot arguments. Dynamic unloading is not part of this test.
