"""Synthetic TLVs and real CLI behavior; no driver or firmware execution."""
import json
from pathlib import Path
import runpy
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'scripts/iwx_firmware_audit.py'
audit = runpy.run_path(str(SCRIPT))['audit_firmware']

def tlv(kind, data):
    return struct.pack('<II',kind,len(data)) + data + bytes((-len(data))%4)

def firmware(table=bytes([0x18,1,12,2,0x19,1,2,0,0x1d,1,2,2]), extra=b''):
    header=struct.pack('<II',0,0x0a4c5749) + bytes(80)
    return header + tlv(48,table) + extra

class FirmwareAuditTests(unittest.TestCase):
    def test_literal_legacy_metadata(self):
        report=audit(firmware())
        self.assertEqual(report.get('queue_lifetime_candidate'),'legacy_station_bound')
        self.assertEqual(report['command_count'],3)
        self.assertIsNone(report['commands']['queue_config'])
        self.assertEqual(report['commands']['legacy_queue'],{'command':2,'notification':2})

    def test_new_command_is_not_legacy_and_unknown_is_not_supported(self):
        for version in [0,1,2,3,4,99,255]:
            report=audit(firmware(
                bytes([0x18,1,12,2,0x19,1,2,0,0x1d,1,2,2,0x17,5,version,2])))
            want='separate_remove_backend_required' if version in [3,4] else 'unsupported'
            self.assertEqual(report['queue_lifetime_candidate'],want)
        for table in [bytes([0x18,1,12,2]),bytes([0x1d,1,99,2,0x19,1,2,0])]:
            self.assertEqual(audit(firmware(table))['queue_lifetime_candidate'],'unsupported')

    def test_malformed_images_never_imply_legacy_support(self):
        valid=firmware()
        cases=[b'',valid[:87],bytes([1])+valid[1:],valid[:4]+bytes(4)+valid[8:],
               valid[:88],firmware(b''),firmware(b'abc'),firmware(extra=tlv(48,b'abcd')),
               firmware(bytes([0x19,1,2,0,0x19,1,3,0])),valid+b'x',
               valid+struct.pack('<II',42,99)+b'abc',firmware(extra=tlv(42,b'x'))[:-1]]
        for data in cases:
            with self.subTest(size=len(data)):
                with self.assertRaises(ValueError): audit(data)
        self.assertEqual(audit(firmware(extra=tlv(42,b'abc')))['command_count'],3)

    def test_unrelated_duplicate_is_reported_without_inventing_a_version(self):
        # Real firmware 68 advertises group14/cmd23 twice: (1,0) and (99,1).
        table=bytes([0x18,1,12,2,0x19,1,2,0,0x1d,1,2,2,0x17,14,1,0,0x17,14,99,1])
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'split.ucode'; path.write_bytes(firmware(table))
            result=subprocess.run([sys.executable,'-B',str(SCRIPT),str(path)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            report=json.loads(result.stdout)['firmwares'][0]
            self.assertEqual(report['command_count'],5)
            self.assertEqual(report['unique_command_count'],4)
            self.assertEqual(report['queue_lifetime_candidate'],'legacy_station_bound')
            self.assertEqual(report['ambiguous_commands'],[{'group':14,'command':23}])

    def test_cli_outputs_public_metadata_and_fails_without_partial_json(self):
        with tempfile.TemporaryDirectory() as directory:
            good=Path(directory)/'good.ucode'; good.write_bytes(firmware())
            bad=Path(directory)/'bad.ucode'; bad.write_bytes(b'bad')
            result=subprocess.run([sys.executable,'-B',str(SCRIPT),str(good)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0)
            self.assertEqual(json.loads(result.stdout)['firmwares'][0]['name'],'good.ucode')
            self.assertNotIn(str(directory),result.stdout)
            result=subprocess.run([sys.executable,'-B',str(SCRIPT),str(good),str(bad)],capture_output=True,text=True)
            self.assertEqual(result.returncode,1); self.assertEqual(result.stdout,'')

if __name__=='__main__': unittest.main()
