#!/usr/bin/env python3
"""Read-only x86_64 kernel-collection AWDL symbol/slot audit.

No system image is extracted, executed, patched, or included in the repository.
This checks ABI symbol/slot mappings, not method semantics or interoperability.
"""
import argparse
import hashlib
import json
import mmap
import struct
import uuid


def commands(data, header):
    if struct.unpack_from("<I", data, header)[0] != 0xfeedfacf:
        raise ValueError("Expected a 64-bit little-endian Mach-O header")
    offset = header + 32
    for _ in range(struct.unpack_from("<I", data, header + 16)[0]):
        command, size = struct.unpack_from("<II", data, offset)
        if size < 8 or offset + size > len(data):
            raise ValueError("Invalid load command extent")
        yield command, offset, size
        offset += size


def audit(path):
    with open(path, "rb") as source:
        with mmap.mmap(source.fileno(), 0, access=mmap.ACCESS_READ) as data:
            if struct.unpack_from("<I", data, 4)[0] != 0x1000007:
                raise ValueError("Only x86_64 kernel collections are supported")
            header = None
            collection_uuid = None
            for command, offset, size in commands(data, 0):
                if command == 0x1b:
                    collection_uuid = str(uuid.UUID(bytes=data[offset + 8:offset + 24]))
                if command == 0x80000035:
                    _, file_offset, string_offset = struct.unpack_from("<QQI", data, offset + 8)
                    name = data[offset + string_offset:offset + size].split(b"\0")[0]
                    if name == b"com.apple.iokit.IO80211Family":
                        header = file_offset
            if header is None:
                raise ValueError("IO80211Family is absent from the collection")
            segments = []
            symtab = None
            for command, offset, _ in commands(data, header):
                if command == 0x19:
                    vm, _, file_offset, file_size = struct.unpack_from("<QQQQ", data, offset + 24)
                    segments.append((vm, file_offset, file_size))
                if command == 2:
                    symtab = struct.unpack_from("<IIII", data, offset + 8)
            if not symtab:
                raise ValueError("IO80211Family symbol table is unavailable")
            symbol_offset, count, strings, string_size = symtab
            symbols = {}
            for index in range(count):
                name_offset, flags, _, _, address = struct.unpack_from("<IBBHQ", data, symbol_offset + index * 16)
                if name_offset >= string_size:
                    raise ValueError("Invalid symbol string offset")
                end = data.find(b"\0", strings + name_offset, strings + string_size)
                if end < 0:
                    raise ValueError("Unterminated symbol name")
                name = data[strings + name_offset:end].decode("utf-8", errors="replace")
                if address:
                    symbols[name] = (address, flags)

            def file_position(address):
                for vm, position, size in segments:
                    if vm <= address < vm + size:
                        return position + address - vm
                raise ValueError("Address is not backed by an image segment")

            slots = [
                ("__ZTV17IO80211Controller", 0xdd8, "__ZN17IO80211Controller22createVirtualInterfaceEP10ether_addrj"),
                ("__ZTV17IO80211Controller", 0xde0, "__ZN17IO80211Controller22attachVirtualInterfaceEPP23IO80211VirtualInterfaceP10ether_addrjb"),
                ("__ZTV17IO80211Controller", 0xde8, "__ZN17IO80211Controller22detachVirtualInterfaceEP23IO80211VirtualInterfaceb"),
                ("__ZTV23IO80211VirtualInterface", 0x670, "__ZN23IO80211VirtualInterface6attachEP9IOService"),
                ("__ZTV23IO80211VirtualInterface", 0xe68, "__ZN23IO80211VirtualInterface4initEP17IO80211ControllerP10ether_addrjPKc"),
            ]
            fixup_base = None
            checked = []
            for table, slot, name in slots:
                position = file_position(symbols[table][0]) + 16 + slot
                raw = struct.unpack_from("<Q", data, position)[0]
                target, flags = symbols[name]
                candidate_base = target - (raw & 0x3fffffff)
                if fixup_base is None:
                    fixup_base = candidate_base
                if candidate_base != fixup_base:
                    raise ValueError("Unexpected vtable/fixup mapping at " + name)
                checked.append({"table": table, "slot": hex(slot), "symbol": name,
                                "external_symbol": bool(flags & 1)})
            return {"collection_uuid": collection_uuid,
                    "collection_sha256": hashlib.sha256(data).hexdigest(),
                    "checked_slots": checked,
                    "scope": "symbol/slot audit only; no runtime interface verification"}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("collection", nargs="?", default="/System/Library/KernelCollections/BootKernelExtensions.kc")
    args = parser.parse_args()
    print(json.dumps(audit(args.collection), indent=2))
