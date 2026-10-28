#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests de webpage.py: assemblage de la page servie par la boîte (bibliothèque standard seulement).

Lancer depuis la racine du dépôt:
  python3 -m unittest discover -s scripts -p "test_*.py"
"""

import sys
import tempfile
import unittest
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPTS))

import webpage  # noqa: E402

WEB = SCRIPTS.parent / "web"


def write(directory: Path, files: dict) -> Path:
    for name, content in files.items():
        path = directory / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")
    return directory


class AssembleTests(unittest.TestCase):
    def test_stylesheet_and_scripts_are_inlined_in_order(self):
        with tempfile.TemporaryDirectory() as tmp:
            source = write(Path(tmp), {
                "index.html": '<head><link rel="stylesheet" href="style.css" /></head>'
                              '<body><script src="vendor/lib.js"></script><script src="app.js"></script></body>',
                "style.css": "body { color: red; }\n",
                "vendor/lib.js": "var lib = 1;\n",
                "app.js": "lib + 1;\n",
            })
            page, used = webpage.assemble(source)
        self.assertIn("<style>\nbody { color: red; }\n</style>", page)
        self.assertLess(page.index("var lib = 1;"), page.index("lib + 1;"))
        self.assertNotIn("src=", page)
        self.assertNotIn("<link", page)
        self.assertEqual({p.name for p in used}, {"index.html", "style.css", "lib.js", "app.js"})

    def test_remote_reference_is_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            source = write(Path(tmp), {"index.html": '<script src="https://cdn.example.com/x.js"></script>'})
            with self.assertRaises(ValueError):
                webpage.assemble(source)

    def test_reference_outside_web_is_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            source = write(Path(tmp) / "web", {"index.html": '<script src="../secret.js"></script>'})
            write(Path(tmp), {"secret.js": "x"})
            with self.assertRaises(ValueError):
                webpage.assemble(source)

    def test_closing_tag_inside_a_script_is_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            source = write(Path(tmp), {"index.html": '<script src="app.js"></script>', "app.js": 'x = "</script>";'})
            with self.assertRaises(ValueError):
                webpage.assemble(source)


class RealPageTests(unittest.TestCase):
    """La page du dépôt: un seul fichier, autonome (la boîte n'est pas forcément reliée à Internet)."""

    def test_page_is_self_contained(self):
        page, used = webpage.assemble(WEB)
        self.assertEqual(page.count("<style>"), 1)
        self.assertNotIn("<link rel=\"stylesheet\"", page)
        self.assertNotIn("<script src=", page)
        self.assertNotIn("http://cdn", page)
        self.assertNotIn("https://cdn", page)
        self.assertIn("__UI_VERSION__", page)
        self.assertIn("nacl", page)
        # Tous les fichiers de web/ sont intégrés: rien d'autre à copier dans l'image LittleFS.
        sources = {p.resolve() for p in WEB.rglob("*") if p.is_file() and not p.name.startswith(".")}
        self.assertEqual(sources, used)


if __name__ == "__main__":
    unittest.main()
