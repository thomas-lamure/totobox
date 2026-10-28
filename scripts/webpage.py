# SPDX-License-Identifier: GPL-3.0-or-later
"""Assemble la page web servie par la boîte à partir des sources de web/.

Les sources restent séparées pour être lisibles et servies telles quelles par le faux serveur de développement
(scripts/mock-server.mjs): index.html (structure), style.css, app.js et vendor/ (bibliothèques reprises telles quelles).
La boîte, elle, ne sert qu'un fichier: chaque `<link rel="stylesheet" href="…">` et `<script src="…"></script>`
qui pointe vers un fichier local de web/ est remplacé par son contenu, dans une balise <style> ou <script>.

Utilisé par scripts/build_webfs.py (image LittleFS) et testé par scripts/test_webpage.py.
"""

import re
from pathlib import Path

STYLESHEET = re.compile(r'<link rel="stylesheet" href="([^"]+)"\s*/?>')
SCRIPT = re.compile(r'<script src="([^"]+)"></script>')


def _local(source: Path, reference: str) -> Path:
    if "://" in reference or reference.startswith("/"):
        raise ValueError(f"référence non locale dans index.html: {reference}")
    path = (source / reference).resolve()
    if source.resolve() not in path.parents:
        raise ValueError(f"référence hors de web/: {reference}")
    return path


def _inline(source: Path, html: str, pattern: re.Pattern, tag: str, used: set) -> str:
    def replace(match: re.Match) -> str:
        path = _local(source, match.group(1))
        content = path.read_text(encoding="utf-8").rstrip("\n")
        if f"</{tag}" in content:
            raise ValueError(f"{match.group(1)} contient </{tag}: impossible à intégrer dans la page")
        used.add(path)
        return f"<{tag}>\n{content}\n</{tag}>"

    return pattern.sub(replace, html)


def assemble(source: Path) -> tuple[str, set]:
    """Renvoie la page complète et l'ensemble des fichiers intégrés (chemins absolus)."""
    used = {(source / "index.html").resolve()}
    html = (source / "index.html").read_text(encoding="utf-8")
    html = _inline(source, html, STYLESHEET, "style", used)
    html = _inline(source, html, SCRIPT, "script", used)
    return html, used
