# SPDX-License-Identifier: GPL-3.0-or-later
"""Concordance de la table des routes du firmware (src/api_routes.cpp) avec docs/api.md et le faux serveur.

    python3 -m unittest scripts/test_api_routes.py      (make test-scripts)

La table dit, pour chaque route /api, si elle est refusée quand la boîte est verrouillée et pendant une lecture.
Une route ajoutée ou modifiée dans le firmware sans mettre à jour la documentation ou le faux serveur fait
échouer ce test.
"""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

TABLE_ROW = re.compile(r'\{Method::(Get|Post), "(/api/[^"]+)", (true|false), (true|false)\}')
DOC_ROW = re.compile(r"^\| `(GET|POST) (/api/[^`?]+)(?:\?[^`]*)?` \| (oui|non) \| (oui|non|—) \|", re.MULTILINE)
MOCK_ROW = re.compile(r'^\s*\["(GET|POST)", "(/api/[^"]+)", (true|false), (true|false)', re.MULTILINE)
# Routes que le faux serveur traite à part (envoi multipart, réponse binaire), avec leurs propres contrôles.
MOCK_SPECIAL = {("POST", "/api/upload"), ("GET", "/api/download")}


def firmware_routes() -> dict:
    source = (ROOT / "src" / "api_routes.cpp").read_text(encoding="utf-8")
    routes = {}
    for method, path, locked, busy in TABLE_ROW.findall(source):
        routes[(method.upper(), path)] = (locked == "true", busy == "true")
    return routes


class ApiRoutesTest(unittest.TestCase):
    def setUp(self):
        self.routes = firmware_routes()
        self.assertGreater(len(self.routes), 20, "table des routes illisible")

    def test_documentation_matches_the_firmware(self):
        doc = (ROOT / "docs" / "api.md").read_text(encoding="utf-8")
        documented = {}
        for method, path, when_locked, while_playing in DOC_ROW.findall(doc):
            # Colonnes « Verrouillée » et « En lecture »: oui = servie; « — »: sans objet (jamais refusée).
            documented[(method, path)] = (when_locked == "non", while_playing == "non")
        self.assertEqual(set(documented), set(self.routes), "routes de docs/api.md et de src/api_routes.cpp")
        for route, flags in self.routes.items():
            self.assertEqual(documented[route], flags, f"{route[0]} {route[1]}: docs/api.md diffère du firmware")

    def test_mock_server_matches_the_firmware(self):
        mock = (ROOT / "scripts" / "mock-server.mjs").read_text(encoding="utf-8")
        simulated = {(method, path): (locked == "true", busy == "true") for method, path, locked, busy in MOCK_ROW.findall(mock)}
        self.assertEqual(set(simulated) | MOCK_SPECIAL, set(self.routes), "routes du faux serveur et du firmware")
        for route, flags in simulated.items():
            self.assertEqual(flags, self.routes[route], f"{route[0]} {route[1]}: le faux serveur diffère du firmware")
        for method, path in MOCK_SPECIAL:
            self.assertEqual(self.routes[(method, path)], (True, True), f"{path}: contrôles du faux serveur à revoir")


if __name__ == "__main__":
    unittest.main()
