#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests de release.py: numéro de version, journal des modifications (bibliothèque standard seulement).

Lancer depuis la racine du dépôt:
  python3 -m unittest discover -s scripts -p "test_*.py"
"""

import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPTS))

import release  # noqa: E402

CHANGELOG = """# Journal des modifications

## Non publié

### Firmware

- Correction.

## 1.0.1 - 2026-10-28

Corrections.

## 1.0.0 - 2026-10-28

Première publication.
"""

HEADER = 'constexpr const char* kFirmwareVersion = "1.0.1";\n'


class VersionTests(unittest.TestCase):
    def test_semver_is_parsed_and_compared(self):
        self.assertEqual(release.parse_version("1.10.0"), (1, 10, 0))
        self.assertGreater(release.parse_version("1.10.0"), release.parse_version("1.9.9"))
        for bad in ("v1.0.0", "1.0", "1.0.0-rc1", "01.0.0", ""):
            with self.subTest(version=bad), self.assertRaises(release.ReleaseError):
                release.parse_version(bad)

    def test_version_header_is_read_and_updated(self):
        self.assertEqual(release.current_version(HEADER), "1.0.1")
        self.assertEqual(release.current_version(release.set_version(HEADER, "1.1.0")), "1.1.0")


class ChangelogTests(unittest.TestCase):
    def test_unreleased_section_becomes_the_version(self):
        updated = release.release_changelog(CHANGELOG, "1.1.0", "2026-11-02")
        self.assertIn("## Non publié\n\n## 1.1.0 - 2026-11-02\n\n### Firmware\n\n- Correction.\n", updated)
        self.assertEqual(release.section_body(updated, "## Non publié"), "")
        self.assertEqual(release.section_body(updated, "## 1.1.0"), "### Firmware\n\n- Correction.")
        # Les versions précédentes ne bougent pas.
        self.assertEqual(release.section_body(updated, "## 1.0.0"), "Première publication.")

    def test_empty_unreleased_section_is_refused(self):
        released = release.release_changelog(CHANGELOG, "1.1.0", "2026-11-02")
        with self.assertRaises(release.ReleaseError):
            release.release_changelog(released, "1.2.0", "2026-11-03")

    def test_existing_version_is_refused(self):
        with self.assertRaises(release.ReleaseError):
            release.release_changelog(CHANGELOG, "1.0.1", "2026-11-02")

    def test_section_prefix_does_not_match_another_version(self):
        changelog = "## 1.0.10 - 2026-12-01\n\nDix.\n\n## 1.0.1 - 2026-10-28\n\nUn.\n"
        self.assertEqual(release.section_body(changelog, "## 1.0.1"), "Un.")
        with self.assertRaises(release.ReleaseError):
            release.section_body(changelog, "## 1.0.2")


class UpdateBlockTests(unittest.TestCase):
    def test_block_describes_the_files_for_the_page(self):
        with tempfile.TemporaryDirectory() as tmp:
            firmware = Path(tmp) / "totobox-1.1.0-firmware.bin"
            page = Path(tmp) / "totobox-1.1.0-page.html.gz"
            firmware.write_bytes(b"\xe9firmware")
            page.write_bytes(b"page")
            block = release.update_block("1.1.0", "8291aeae5293", firmware, page)
        self.assertTrue(block.startswith("<!-- totobox-update {") and block.endswith("} -->"))
        info = json.loads(block[len("<!-- totobox-update "):-len(" -->")])
        self.assertEqual(info["version"], "1.1.0")
        self.assertEqual(info["uiVersion"], "8291aeae5293")
        self.assertEqual(info["page"], {"name": "totobox-1.1.0-page.html.gz", "size": 4,
                                        "sha256": hashlib.sha256(b"page").hexdigest()})
        self.assertEqual(info["firmware"]["sha256"], hashlib.sha256(b"\xe9firmware").hexdigest())


class CheckTests(unittest.TestCase):
    def test_tag_matches_version_and_changelog(self):
        release.check("1.0.1", HEADER, CHANGELOG)

    def test_tag_ahead_of_version_header_is_refused(self):
        with self.assertRaises(release.ReleaseError):
            release.check("1.1.0", HEADER, CHANGELOG)

    def test_repository_is_consistent(self):
        # La version du dépôt a sa section dans le journal (rappel de faire une release complète).
        header = release.VERSION_FILE.read_text(encoding="utf-8")
        changelog = release.CHANGELOG.read_text(encoding="utf-8")
        release.check(release.current_version(header), header, changelog)


if __name__ == "__main__":
    unittest.main()
