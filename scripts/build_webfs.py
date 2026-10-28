# SPDX-License-Identifier: GPL-3.0-or-later
# Prépare le contenu de l'image LittleFS (pio run -t buildfs / uploadfs).
#
# web/ reste la source éditée à la main (index.html, style.css, lib.js, app.js, vendor/). Ce script assemble la page en
# un seul fichier (scripts/webpage.py: feuille de style et scripts intégrés), la compresse en gzip (~200 Ko ->
# ~45 Ko) dans le dossier de build, et en fait le contenu de l'image: moins de flash occupée, une seule requête
# et moins de temps à servir la page pendant la lecture. Le serveur web envoie le .gz avec l'en-tête
# Content-Encoding: gzip.
#
# Régénéré à chaque exécution de PlatformIO: aucun fichier compressé périmé dans le dépôt.
#
# Version de l'interface: empreinte de la page assemblée, écrite dans la page (balise ui-version) et dans
# ui_version.txt que la boîte renvoie dans /api/status. Une page ouverte avant une mise à jour de l'image
# voit la différence et propose de se recharger.

import gzip
import hashlib
import shutil
import sys
from pathlib import Path

Import("env")  # noqa: F821 (fourni par PlatformIO)

project = Path(env.subst("$PROJECT_DIR"))  # noqa: F821
sys.path.insert(0, str(project / "scripts"))
from webpage import assemble  # noqa: E402

COMPRESSED_SUFFIXES = {".html", ".css", ".js", ".svg", ".json"}
VERSION_PLACEHOLDER = "__UI_VERSION__"

source = project / "web"
target = Path(env.subst("$BUILD_DIR")) / "littlefs_data"  # noqa: F821


def write_gzip(destination: Path, content: bytes) -> Path:
    compressed = destination.with_name(destination.name + ".gz")
    # mtime=0 et nom vide: sortie identique d'une compilation à l'autre.
    with open(compressed, "wb") as raw, gzip.GzipFile(
        filename="", mode="wb", fileobj=raw, mtime=0, compresslevel=9
    ) as archive:
        archive.write(content)
    return compressed


def build_web_filesystem() -> None:
    if target.exists():
        shutil.rmtree(target)
    target.mkdir(parents=True)

    page, inlined = assemble(source)
    # Même contenu, même version: une reconstruction sans changement ne propose pas de rechargement.
    version = hashlib.sha256(page.encode("utf-8")).hexdigest()[:12]
    (target / "ui_version.txt").write_text(version)
    print(f"webfs: version de l'interface {version}")

    compressed = write_gzip(target / "index.html", page.replace(VERSION_PLACEHOLDER, version).encode("utf-8"))
    print(f"webfs: index.html {len(page.encode('utf-8'))} -> {compressed.name} {compressed.stat().st_size} octets")

    # Autres fichiers éventuels de web/ (pas intégrés dans la page): copiés, compressés s'ils sont du texte.
    for path in sorted(source.rglob("*")):
        if path.is_dir() or path.name.startswith(".") or path.resolve() in inlined:
            continue
        destination = target / path.relative_to(source)
        destination.parent.mkdir(parents=True, exist_ok=True)
        if path.suffix in COMPRESSED_SUFFIXES:
            write_gzip(destination, path.read_bytes())
        else:
            shutil.copy2(path, destination)


build_web_filesystem()
env.Replace(PROJECT_DATA_DIR=str(target))  # noqa: F821
