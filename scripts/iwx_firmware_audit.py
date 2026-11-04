#!/usr/bin/env python3
"""Read-only inspection of bundled Intel firmware command-version metadata."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

# Bounds for this offline metadata reader, not firmware capability constants.
MAX_IMAGE = 32 * 1024 * 1024
MAX_VERSION_BYTES = 4096
COMMANDS = {
    'add_station': (1, 0x18), 'remove_station': (1, 0x19),
    'legacy_queue': (1, 0x1d), 'queue_config': (5, 0x17), 'flush': (1, 0x1e),
}

def audit_firmware(data):
    if len(data) < 88 or len(data) > MAX_IMAGE:
        raise ValueError('invalid firmware image size')
    if struct.unpack_from('<II', data) != (0, 0x0a4c5749):
        raise ValueError('invalid TLV firmware header')
    versions = None
    count = 0
    ambiguous = set()
    offset = 88
    while offset < len(data):
        if len(data) - offset < 8:
            raise ValueError('truncated TLV header')
        kind, length = struct.unpack_from('<II', data, offset)
        offset += 8
        padded = (length + 3) & ~3
        if padded > len(data) - offset:
            raise ValueError('truncated TLV payload or alignment padding')
        if kind == 48:
            if versions is not None or not length or length % 4 or length > MAX_VERSION_BYTES:
                raise ValueError('invalid or duplicate command-version TLV')
            versions = {}
            for index in range(offset, offset + length, 4):
                command, group, version, notification = data[index:index + 4]
                key = (group, command)
                count += 1
                if key in versions:
                    # Preserve ambiguity; never synthesize a combined version.
                    ambiguous.add(key)
                else:
                    versions[key] = {'command': version, 'notification': notification}
        offset += padded
    if versions is None:
        raise ValueError('missing command-version TLV; absence is not legacy evidence')
    if ambiguous.intersection(COMMANDS.values()):
        raise ValueError('ambiguous command-version identity in audited commands')
    commands = {name: versions.get(key) for name, key in COMMANDS.items()}
    candidate = 'unsupported'
    if commands['queue_config'] is not None:
        if commands['queue_config']['command'] in (3, 4):
            candidate = 'separate_remove_backend_required'
    elif commands['legacy_queue'] == {'command': 2, 'notification': 2} and \
            commands['remove_station'] == {'command': 2, 'notification': 0}:
        candidate = 'legacy_station_bound'
    return {
        'size': len(data), 'sha256': hashlib.sha256(data).hexdigest(),
        'command_count': count, 'unique_command_count': len(versions),
        'ambiguous_commands': [{'group': g, 'command': c} for g, c in sorted(ambiguous)],
        'commands': commands,
        'queue_lifetime_candidate': candidate,
    }

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('paths', nargs='*', type=Path)
    args = parser.parse_args()
    paths = args.paths or sorted((Path(__file__).resolve().parents[1] / 'itlwm/firmware').glob('*.ucode'))
    try:
        if not paths:
            raise ValueError('no firmware inputs')
        rows = [dict(name=p.name, **audit_firmware(p.read_bytes())) for p in paths]
    except (OSError, ValueError) as error:
        print(str(error), file=sys.stderr)
        return 1
    print(json.dumps({'schema': 1, 'firmwares': rows}, indent=2, sort_keys=True))
    return 0

if __name__ == '__main__':
    sys.exit(main())
