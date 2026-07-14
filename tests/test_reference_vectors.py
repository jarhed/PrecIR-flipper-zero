"""Golden vectors for the PrecIR wire protocol.

These tests deliberately use the original Python reference implementation in
``tools_python/pr.py``.  The FAP also runs equivalent C self-tests at startup;
this host suite keeps the documented reference bytes easy to inspect and
regenerate without a Flipper attached.
"""

from __future__ import annotations

import importlib.util
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
PR_PATH = ROOT / "tools_python" / "pr.py"
SPEC = importlib.util.spec_from_file_location("precir_reference", PR_PATH)
assert SPEC is not None and SPEC.loader is not None
pr = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(pr)


BARCODE = "G4591371776312423"
PLID = [0xE7, 0x01, 0x45, 0x63]


def with_checksum(first_sixteen: str) -> str:
    assert len(first_sixteen) == 16
    return first_sixteen + str(sum(map(ord, first_sixteen)) % 10)


def barcode_is_valid(barcode: str) -> bool:
    """Document the validation contract implemented by the FAP."""
    if len(barcode) != 17:
        return False
    if not ("A" <= barcode[0] <= "Z") or barcode[1] != "4":
        return False
    if not all("0" <= char <= "9" for char in barcode[2:]):
        return False
    if int(barcode[2:7]) > 0xFFFF or int(barcode[7:12]) > 0xFFFF:
        return False
    return sum(map(ord, barcode[:16])) % 10 == int(barcode[16])


class ReferenceVectorTests(unittest.TestCase):
    def test_crc_documented_vector(self) -> None:
        body = bytes.fromhex("84 00 00 00 00 AB 11 00 00")
        self.assertEqual(pr.crc16(body), 0xE4A5)

    def test_barcode_to_plid_and_wire_order(self) -> None:
        self.assertTrue(barcode_is_valid(BARCODE))
        self.assertEqual(pr.get_plid(BARCODE), PLID)
        frame = pr.make_raw_frame(0x84, PLID, 0xAB)
        self.assertEqual(frame[:6], [0x84, 0x63, 0x45, 0x01, 0xE7, 0xAB])

    def test_barcode_rejections(self) -> None:
        bad_checksum = BARCODE[:-1] + str((int(BARCODE[-1]) + 1) % 10)
        invalid = [
            BARCODE[:-1],
            with_checksum("g" + BARCODE[1:16]),
            with_checksum("G5" + BARCODE[2:16]),
            with_checksum(BARCODE[:8] + "X" + BARCODE[9:16]),
            with_checksum("G4" + "99999" + BARCODE[7:16]),
            with_checksum(BARCODE[:7] + "99999" + BARCODE[12:16]),
            bad_checksum,
        ]
        for barcode in invalid:
            with self.subTest(barcode=barcode):
                self.assertFalse(barcode_is_valid(barcode))

    def test_wake_frame(self) -> None:
        frame = pr.make_ping_frame(PLID, False, 400)
        expected = bytes.fromhex(
            "85 63 45 01 E7 17 01 00 00 00 "
            + "01 " * 22
            + "8E 61"
        )
        self.assertEqual(bytes(frame[:-2]), expected)
        self.assertEqual(frame[-2:], [0x90, 0x01])

    def test_pp16_preamble_is_outside_crc(self) -> None:
        pp4 = pr.make_ping_frame(PLID, False, 400)
        pp16 = pr.make_ping_frame(PLID, True, 400)
        self.assertEqual(pp16[:4], [0x00, 0x00, 0x00, 0x40])
        self.assertEqual(pp16[4:-2], pp4[:-2])
        self.assertEqual(pp16[-2:], pp4[-2:])

    def test_refresh_frame(self) -> None:
        frame = pr.make_refresh_frame(PLID, False)
        expected = bytes.fromhex(
            "85 63 45 01 E7 34 00 00 00 01 "
            + "00 " * 22
            + "F3 A3"
        )
        self.assertEqual(bytes(frame[:-2]), expected)

    def test_params_frame(self) -> None:
        frame = pr.make_mcu_frame(PLID, 0x05)
        pr.append_word(frame, 40)
        frame.extend([0x00, 0x02, 0x03])
        pr.append_word(frame, 8)
        pr.append_word(frame, 1)
        pr.append_word(frame, 0)
        pr.append_word(frame, 0)
        pr.append_word(frame, 0)
        frame.append(0x88)
        pr.append_word(frame, 0)
        frame.extend([0, 0, 0, 0])
        pr.terminate_frame(frame, False, 1)
        expected = bytes.fromhex(
            "85 63 45 01 E7 34 00 00 00 05 00 28 00 02 03 00 08 "
            "00 01 00 00 00 00 00 00 88 00 00 00 00 00 00 06 A3"
        )
        self.assertEqual(bytes(frame[:-2]), expected)


if __name__ == "__main__":
    unittest.main()
