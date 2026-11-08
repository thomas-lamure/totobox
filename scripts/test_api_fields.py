# SPDX-License-Identifier: GPL-3.0-or-later
"""Champs de GET /api/status et GET /api/config: firmware (src/web_admin.cpp), faux serveur et docs/api.md.

    python3 -m unittest scripts/test_api_fields.py      (make test-scripts)

La page se teste sur le faux serveur: un champ qu'il renvoie et que la boîte oublie (ou l'inverse) passe
inaperçu jusqu'à l'essai sur la boîte. Ce test lit les champs écrits par le firmware (doc["…"], objets et
tableaux imbriqués, cartes écrites par ConfigStore::writeBindings), les compare à la réponse du faux serveur
(node scripts/mock-server.mjs --dump) et vérifie que docs/api.md nomme chaque champ.
"""

import json
import re
import shutil
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Fonctions du firmware qui construisent chaque réponse.
RESPONSES = {"status": "WebAdmin::handleStatus", "config": "WebAdmin::sendJsonConfig"}
# Tableaux remplis par une fonction partagée: chemin -> fonction dont les item["…"] donnent les champs.
SHARED_WRITERS = {"ConfigStore::writeBindings": "ConfigStore::writeBindings"}

CHILD = re.compile(r'Json(Object|Array)\s+(\w+)\s*=\s*(\w+)\["(\w+)"\]\.to<Json(?:Object|Array)>\(\)')
ITEM = re.compile(r'Json(Object|Array)\s+(\w+)\s*=\s*(\w+)\.add<Json(?:Object|Array)>\(\)')
FIELD = re.compile(r'\b(\w+)\["(\w+)"\]\s*=(?!=)')
SHARED = re.compile(r'([\w:]+)\(\s*(\w+)\["(\w+)"\]\.to<JsonArray>\(\)')


def function_body(source: str, name: str) -> str:
    match = re.search(r"^[\w:<>&\s]*\b" + re.escape(name) + r"\([^)]*\)\s*(?:const\s*)?\{", source, re.MULTILINE)
    if not match:
        raise AssertionError(f"fonction {name} introuvable")
    depth, start = 0, match.end() - 1
    for index in range(start, len(source)):
        depth += {"{": 1, "}": -1}.get(source[index], 0)
        if depth == 0:
            return source[start:index + 1]
    raise AssertionError(f"fonction {name} mal fermée")


def written_fields(body: str, sources: str, root: str = "doc", prefix: str = "") -> set:
    """Chemins des champs écrits (« networkAttempt.ssid », « bindings[].emoji ») à partir de la variable root."""
    paths = {root: prefix}
    fields = set()
    for line in body.splitlines():
        if match := CHILD.search(line):
            kind, name, parent, key = match.groups()
            if parent in paths:
                path = paths[parent] + key
                fields.add(path)
                paths[name] = path + ("[]." if kind == "Array" else ".")
                # Tableau d'objets: les éléments s'ajoutent ensuite avec .add<JsonObject>().
                paths[name + "#array"] = path + "[]."
            continue
        if match := ITEM.search(line):
            _, name, parent, = match.groups()
            if parent + "#array" in paths:
                paths[name] = paths[parent + "#array"]
            continue
        if match := SHARED.search(line):
            function, parent, key = match.groups()
            if parent in paths and function in SHARED_WRITERS:
                path = paths[parent] + key
                fields.add(path)
                shared = function_body(sources, SHARED_WRITERS[function])
                fields |= {path + "[]." + key for key in re.findall(r'\bitem\["(\w+)"\]\s*=(?!=)', shared)}
            continue
        for variable, key in FIELD.findall(line):
            if variable in paths:
                fields.add(paths[variable] + key)
    return fields


def json_fields(value, prefix: str = "") -> set:
    fields = set()
    if isinstance(value, dict):
        for key, child in value.items():
            fields.add(prefix + key)
            if isinstance(child, dict):
                fields |= json_fields(child, prefix + key + ".")
            elif isinstance(child, list):
                for item in child:
                    fields |= json_fields(item, prefix + key + "[].")
    return fields


class ApiFieldsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        sources = "\n".join(
            (ROOT / "src" / name).read_text(encoding="utf-8") for name in ("web_admin.cpp", "config_store.cpp")
        )
        cls.firmware = {
            response: written_fields(function_body(sources, function), sources)
            for response, function in RESPONSES.items()
        }

    def test_firmware_is_parsed(self):
        # Garde-fou: un changement d'écriture du firmware ne doit pas vider silencieusement la comparaison.
        self.assertIn("networkAttempt.apClosesInSec", self.firmware["status"])
        self.assertIn("bindings[].emoji", self.firmware["config"])
        self.assertIn("lockCards[].label", self.firmware["config"])
        self.assertGreater(len(self.firmware["status"]), 40)

    @unittest.skipUnless(shutil.which("node"), "node absent")
    def test_mock_server_returns_the_same_fields(self):
        dump = subprocess.run(
            ["node", str(ROOT / "scripts" / "mock-server.mjs"), "--dump"],
            capture_output=True, text=True, check=True, timeout=30,
        )
        mock = json.loads(dump.stdout.strip().splitlines()[-1])
        for response, fields in self.firmware.items():
            simulated = json_fields(mock[response])
            self.assertEqual(
                sorted(fields - simulated), [], f"/api/{response}: champs du firmware absents du faux serveur"
            )
            self.assertEqual(
                sorted(simulated - fields), [], f"/api/{response}: champs du faux serveur absents du firmware"
            )

    def test_documentation_names_every_field(self):
        doc = (ROOT / "docs" / "api.md").read_text(encoding="utf-8")
        named = set(re.findall(r"`(\w+)`", doc))
        for response, fields in self.firmware.items():
            # Chaque nom, imbriqué compris (« ssid » de networkAttempt.ssid est décrit avec son parent).
            names = {part.removesuffix("[]") for field in fields for part in field.split(".")}
            self.assertEqual(sorted(names - named), [], f"/api/{response}: champs absents de docs/api.md")


if __name__ == "__main__":
    unittest.main()
