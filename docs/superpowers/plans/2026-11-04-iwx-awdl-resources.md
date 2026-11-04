# Offline IWX AWDL resources implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Track independently granted AWDL station/queue ownership and fail closed on uncertain firmware outcomes.
**Architecture:** One fixed ledger indexed by station ID, stable lease handles, one pending command per lease, distinct command tickets, conservative quarantine and explicit confirmed-reset boundary.
**Tech Stack:** Allocation-free C11/C++11 header, clang ASan/UBSan host tests.
**Spec:** `docs/superpowers/specs/2026-11-04-iwx-awdl-resources-design.md`

## Global Constraints

- Offline only; do not install/load kexts or change EFI.
- Maximum 16 stations, 512 queues; station IDs 0..2 and queue IDs 0..9 excluded.
- Explicit exclusive grants, externally serialized access, per-ledger handles.
- No ID reuse on UNKNOWN; no serial/epoch wrap; no TX after close.

## Review Focus

- Late duplicate completion during another command must be STALE.
- Close during pending allocation must never reopen TX on success.
- Queue collision must retain both leases and fault TX globally.
- Failed teardown must retain ownership for safe retry.
- Reset/counter limits must reject old handles without changing new state.

### Task 1: Resource ledger and evidence

**Files:** Create `itlwm/hal_iwx/iwx_awdl_resources.h`, `tests/iwx_awdl_resources_test.c`, `docs/awdl/resource-ownership.md`; modify `tests/run-awdl-tests.sh` and `docs/awdl/development.md`.
**Interfaces:** `itl_iwx_awdl_resources_init/reset/reserve/cancel/close/begin/complete/tx_queue` on caller-owned pool, epoch/lease handles, command tickets and explicit outcomes. Return enums distinguish stale/state/invalid/full/conflict/fault/exhaustion/rejection/unknown. Outputs are separate storage and cleared on failure.

- [x] Write behavior tests for the spec's transition paths and five review focus cases.
- [x] Compile and run against stubs; observe missing allocation behavior fail.
- [x] Implement validation, grant reservation, ticketed state transitions and TX lookup.
- [x] Run `sh tests/run-awdl-tests.sh`; all C/C++ sanitizer executions must pass.
- [x] Document actual integration obligations and offline limitations; independently review the complete increment and resolve important issues.
- [ ] Run `git diff --check`, commit the verified increment and publish the exact commit to the existing feature branch.


## Execution record

Baseline: `da73b500027b33e2c782e6edd41c25b41c360c43`, clean feature checkout at the user's requested path. Existing 11 test executions passed before edits. Initial stubs failed at the expected missing initialization behavior; implementation then passed the full runner. A later empty-grant test exposed over-admission, followed by capacity accounting and a green full runner. Latest full runner: 13 executions, exit 0. Independent review reran it successfully and found no critical/important issues.

Ruling: keep this increment independent of the live driver — a central allocator and validated firmware cancellation/DMA reset boundary are prerequisites for safe integration. No new hardware behavior or kernel-build claim is made.

Optional coverage noted by review: unknown allocation's effect on an already-ready second peer and collision against an owner pending drain/removal. The corresponding global-fault and retained-ownership paths were checked by inspection; no correctness defect identified.
