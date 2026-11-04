# AWDL data codec contract

This code is an independently written byte-format component for future data
transport; its only live integration is opt-in passive RX diagnostics.
It does not submit any packet to Intel firmware or a macOS peer interface.

## Encode

`itl_awdl_write_data_header(out, capacity, sequence, ether_type)` writes exactly
16 bytes on success and changes no output on failure. It requires a nonnull
output and an Ethernet-II type of at least `0x0600`. A smaller output capacity
returns `ITL_AWDL_TRUNCATED`; invalid arguments return `ITL_AWDL_MALFORMED`.
The caller supplies both sequence and EtherType; zero/65535 sequences are valid.

Wire example for sequence `0x1234` and IPv6 EtherType `0x86dd`:

```
aa aa 03 00 17 f2 08 00 03 04 34 12 00 00 86 dd
```

The caller still owes an outer 802.11 header, payload, length/ownership handling,
peer selection, channel scheduling and a proven firmware submission path.

## Decode

`itl_awdl_parse_data(reader, out)` expects the complete FCS-free 802.11 frame,
without radiotap. `reader.read` returns zero on success and must support bounded
reads across fragments. The codec neither keeps the reader/context nor changes
its bytes. `out` must be separate writable storage; it is zeroed on failure.

The supported profile is unprotected, unfragmented ordinary/QoS data, with no
DS addresses or A-MSDU. QoS HT Control is skipped when indicated. It checks
BSSID `00:25:00:ff:94:73`, Apple LLC/SNAP and the fixed marker/padding.
A valid result contains source/destination, 16-bit AWDL sequence, EtherType,
and payload offset/length into the original reader. These are wire metadata;
empty payloads and payloads that are not valid IP packets remain structurally
valid. Callers must perform their own payload/peer/replay validation.

The maximum reader length is 4096 bytes. Ordinary frames, null data, other
BSSIDs and other LLC protocols are not AWDL in this profile. Unsupported frame
features/header variants are rejected; truncation and reader failures leave
no partial result for a consumer to accidentally deliver.

## Evidence and verification

Protocol facts checked against [OWL frame definitions](https://github.com/seemoo-lab/owl/blob/master/src/frame.h)
and [OWL transmit construction](https://github.com/seemoo-lab/owl/blob/master/src/tx.c).
No OWL implementation body or captured device packet is included here.

Run `sh tests/run-awdl-tests.sh` for all five ASan/UBSan suites. Encoding golden
bytes and decoding golden frames are checked independently. Boundary, segment,
failure and mutation tests cover the error contracts. Passing them establishes
the tested byte operations, not radio reception/transmission or iPhone support.
