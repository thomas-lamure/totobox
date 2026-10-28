#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Publication d'une version de TotoBox (bibliothèque standard seulement).

  python3 scripts/release.py prepare 1.1.0 [--dry-run]   # make release VERSION=1.1.0
      Sur main, dépôt propre: met la version dans include/version.h, renomme la section « Non publié » de
      CHANGELOG.md en « 1.1.0 - date » (en rouvre une vide), commite « Release 1.1.0 » et étiquette v1.1.0.
      Ne pousse rien: `git push --follow-tags` déclenche ensuite la publication sur GitHub
      (.github/workflows/release.yml).
  python3 scripts/release.py check 1.1.0       # version.h et CHANGELOG.md cohérents avec l'étiquette
  python3 scripts/release.py notes 1.1.0       # notes de version (section du CHANGELOG) sur la sortie standard
  python3 scripts/release.py dist              # make dist: fichiers de la Release dans dist/
  python3 scripts/release.py update-block      # bloc des notes de version lu par la page (mise à jour en ligne)

Règles de numérotation (versionnage sémantique): voir docs/development.md.
"""

import datetime
import hashlib
import json
import re
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VERSION_FILE = ROOT / "include" / "version.h"
CHANGELOG = ROOT / "CHANGELOG.md"
BUILD = ROOT / ".pio" / "build" / "totobox-s3"
DIST = ROOT / "dist"

UNRELEASED = "## Non publié"
# Commentaire HTML (invisible sur GitHub) des notes de version: la page y lit de quoi demander la mise à jour à
# la boîte (web/lib.js, parseUpdateInfo).
UPDATE_BLOCK = "totobox-update"
VERSION_PATTERN = re.compile(r'kFirmwareVersion = "([^"]+)"')
SEMVER = re.compile(r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$")


class ReleaseError(Exception):
    pass


def parse_version(text: str) -> tuple:
    match = SEMVER.match(text)
    if not match:
        raise ReleaseError(f"version invalide « {text} »: attendu X.Y.Z (ex. 1.1.0), sans « v »")
    return tuple(int(part) for part in match.groups())


def current_version(header: str) -> str:
    match = VERSION_PATTERN.search(header)
    if not match:
        raise ReleaseError("kFirmwareVersion introuvable dans include/version.h")
    return match.group(1)


def set_version(header: str, version: str) -> str:
    return VERSION_PATTERN.sub(f'kFirmwareVersion = "{version}"', header, count=1)


def _sections(changelog: str) -> list:
    """(titre, début, fin) de chaque section « ## » du journal."""
    starts = [match.start() for match in re.finditer(r"^## ", changelog, re.M)]
    ends = starts[1:] + [len(changelog)]
    return [(changelog[start:changelog.index("\n", start)].strip(), start, end) for start, end in zip(starts, ends)]


def section_body(changelog: str, title_prefix: str) -> str:
    for title, start, end in _sections(changelog):
        if title == title_prefix or title.startswith(title_prefix + " "):
            return changelog[changelog.index("\n", start) + 1:end].strip()
    raise ReleaseError(f"section « {title_prefix} » introuvable dans CHANGELOG.md")


def release_changelog(changelog: str, version: str, date: str) -> str:
    """La section « Non publié » devient « ## version - date »; une section « Non publié » vide la précède."""
    if re.search(rf"^## {re.escape(version)}( |$)", changelog, re.M):
        raise ReleaseError(f"CHANGELOG.md a déjà une section {version}")
    body = section_body(changelog, UNRELEASED)
    if not body:
        raise ReleaseError("la section « Non publié » de CHANGELOG.md est vide: rien à publier")
    return changelog.replace(UNRELEASED + "\n", f"{UNRELEASED}\n\n## {version} - {date}\n", 1)


def check(version: str, header: str, changelog: str) -> None:
    parse_version(version)
    if current_version(header) != version:
        raise ReleaseError(f"include/version.h annonce {current_version(header)}, l'étiquette {version}")
    if not section_body(changelog, f"## {version}"):
        raise ReleaseError(f"la section {version} de CHANGELOG.md est vide")


def git(*args: str, capture: bool = True) -> str:
    result = subprocess.run(["git", *args], cwd=ROOT, check=True, text=True, capture_output=capture)
    return result.stdout.strip() if capture else ""


def prepare(version: str, dry_run: bool) -> None:
    header = VERSION_FILE.read_text(encoding="utf-8")
    changelog = CHANGELOG.read_text(encoding="utf-8")
    previous = current_version(header)
    if parse_version(version) <= parse_version(previous):
        raise ReleaseError(f"{version} doit être plus grande que la version actuelle ({previous})")
    if git("rev-parse", "--abbrev-ref", "HEAD") != "main":
        raise ReleaseError("à lancer sur la branche main")
    if git("status", "--porcelain"):
        raise ReleaseError("des fichiers sont modifiés: commiter ou mettre de côté d'abord")
    if git("tag", "--list", f"v{version}"):
        raise ReleaseError(f"l'étiquette v{version} existe déjà")

    new_changelog = release_changelog(changelog, version, datetime.date.today().isoformat())
    print(f"Version {previous} -> {version}\n")
    print(section_body(new_changelog, f"## {version}"))
    if dry_run:
        print("\n(essai: rien n'est modifié)")
        return
    VERSION_FILE.write_text(set_version(header, version), encoding="utf-8")
    CHANGELOG.write_text(new_changelog, encoding="utf-8")
    git("add", str(VERSION_FILE), str(CHANGELOG))
    git("commit", "-q", "-m", f"Release {version}")
    git("tag", "-a", f"v{version}", "-m", f"TotoBox {version}")
    print(f"\nCommit et étiquette v{version} créés. Pour publier: git push --follow-tags")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def dist() -> None:
    """Fichiers de la Release (après pio run, pio run -t buildfs et pio run -t merged)."""
    version = current_version(VERSION_FILE.read_text(encoding="utf-8"))
    files = {
        f"totobox-{version}-complet.bin": BUILD / "merged.bin",
        f"totobox-{version}-firmware.bin": BUILD / "firmware.bin",
        f"totobox-{version}-page-web.bin": BUILD / "littlefs.bin",
        # Page seule, pour la mise à jour en ligne (remplace /index.html.gz dans LittleFS).
        f"totobox-{version}-page.html.gz": BUILD / "littlefs_data" / "index.html.gz",
    }
    missing = [str(path) for path in files.values() if not path.exists()]
    if missing:
        raise ReleaseError("à construire d'abord (make firmware webfs, pio run -t merged): " + ", ".join(missing))
    if DIST.exists():
        shutil.rmtree(DIST)
    DIST.mkdir()
    for name, path in files.items():
        shutil.copyfile(path, DIST / name)
    # Fichiers de découpe et gabarits des deux versions du boîtier.
    with zipfile.ZipFile(DIST / f"totobox-{version}-decoupe.zip", "w", zipfile.ZIP_DEFLATED) as archive:
        for path in sorted((ROOT / "hardware" / "laser").iterdir()):
            if path.suffix in (".dxf", ".pdf"):
                archive.write(path, f"totobox-{version}-decoupe/{path.name}")
    sums = [f"{sha256(path)}  {path.name}" for path in sorted(DIST.iterdir())]
    (DIST / "SHA256SUMS.txt").write_text("\n".join(sums) + "\n", encoding="utf-8")
    for path in sorted(DIST.iterdir()):
        print(f"dist/{path.name} ({path.stat().st_size} octets)")


def update_block(version: str, ui_version: str, firmware: Path, page: Path) -> str:
    """Commentaire HTML ajouté aux notes de version: fichiers (nom, taille, SHA-256) et version de la page."""
    def asset(path: Path) -> dict:
        return {"name": path.name, "size": path.stat().st_size, "sha256": sha256(path)}

    info = {"version": version, "uiVersion": ui_version, "firmware": asset(firmware), "page": asset(page)}
    return f"<!-- {UPDATE_BLOCK} {json.dumps(info, separators=(',', ':'))} -->"


def main(argv: list) -> int:
    try:
        if len(argv) >= 2 and argv[0] == "prepare":
            prepare(argv[1], "--dry-run" in argv[2:])
        elif len(argv) == 2 and argv[0] == "check":
            check(argv[1], VERSION_FILE.read_text(encoding="utf-8"), CHANGELOG.read_text(encoding="utf-8"))
            print(f"{argv[1]}: version.h et CHANGELOG.md cohérents")
        elif len(argv) == 2 and argv[0] == "notes":
            print(section_body(CHANGELOG.read_text(encoding="utf-8"), f"## {argv[1]}"))
        elif argv == ["dist"]:
            dist()
        elif argv == ["update-block"]:
            version = current_version(VERSION_FILE.read_text(encoding="utf-8"))
            ui_version = (BUILD / "littlefs_data" / "ui_version.txt").read_text(encoding="utf-8").strip()
            print(update_block(version, ui_version, DIST / f"totobox-{version}-firmware.bin",
                               DIST / f"totobox-{version}-page.html.gz"))
        else:
            print(__doc__)
            return 2
    except ReleaseError as error:
        print(f"Erreur: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
