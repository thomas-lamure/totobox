#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests du paquet d'autorités racines (src/certs/x509_crt_bundle.bin, scripts/gen_ca_bundle.py).

Lancer depuis la racine du dépôt:
  python3 -m unittest discover -s scripts -p "test_*.py"
"""

import sys
import unittest
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPTS))

import gen_ca_bundle  # noqa: E402

BUNDLE = SCRIPTS.parent / "src" / "certs" / "x509_crt_bundle.bin"


def entries(data: bytes) -> list:
    count = int.from_bytes(data[:2], "big")
    offset = 2
    out = []
    for _ in range(count):
        name_length = int.from_bytes(data[offset:offset + 2], "big")
        key_length = int.from_bytes(data[offset + 2:offset + 4], "big")
        offset += 4
        out.append((data[offset:offset + name_length], data[offset + name_length:offset + name_length + key_length]))
        offset += name_length + key_length
    return out, offset


class BundleTests(unittest.TestCase):
    def test_bundle_is_complete_and_sorted_for_the_binary_search(self):
        data = BUNDLE.read_bytes()
        found, end = entries(data)
        self.assertEqual(end, len(data))
        self.assertGreater(len(found), 100)
        names = [name for name, _ in found]
        self.assertEqual(names, sorted(names))
        for name, key in found:
            self.assertEqual(name[0], 0x30)  # Name: SEQUENCE DER
            self.assertEqual(key[0], 0x30)   # SubjectPublicKeyInfo: SEQUENCE DER

    def test_github_authorities_are_present(self):
        # github.com (Sectigo, USERTrust) et fichiers des Releases (Let's Encrypt: ISRG).
        names = b"".join(name for name, _ in entries(BUNDLE.read_bytes())[0])
        for authority in (b"USERTrust ECC Certification Authority", b"ISRG Root X1", b"ISRG Root X2"):
            self.assertIn(authority, names)

    def test_der_lengths_are_read(self):
        self.assertEqual(gen_ca_bundle.read_tlv(bytes([0x30, 0x03, 1, 2, 3]), 0), (0, 2, 5))
        long_form = bytes([0x30, 0x82, 0x01, 0x00]) + bytes(256)
        self.assertEqual(gen_ca_bundle.read_tlv(long_form, 0), (0, 4, 260))


if __name__ == "__main__":
    unittest.main()
