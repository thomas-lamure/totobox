#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Convertit les contours des pièces de 3 mm (hardware/laser/*_contours.txt) en DXF pour la découpe laser.

Format d'entrée (millimètres, origine au coin de la plaque):
  # NOM_DE_PIECE           début d'une pièce
  L x0 y0 x1 y1            segment
  C cx cy r                cercle
  A cx cy r x0 y0 xm ym x1 y1   arc de (x0, y0) à (x1, y1) passant par (xm, ym)
  # PIECE.OPTION           découpe optionnelle de PIECE (fente microSD...), sur son propre calque

Sorties, à côté du fichier d'entrée:
  <base>_plaque.dxf        toute la plaque (calepinage), un calque par pièce et par option
  <base>_<piece>.dxf       chaque pièce seule (avec ses options), ramenée à l'origine

Les extrémités distantes de moins de SNAP mm sont fusionnées (le modèle Onshape laisse des écarts de 0,01 mm aux
pieds des créneaux), puis chaque contour est contrôlé: une extrémité qui n'est partagée que par une seule arête
signale un contour ouvert. Pas de compensation de saignée: elle se règle dans le logiciel de découpe (ou par la
variable #saignee_compensation dans Onshape).

Usage: python3 hardware/tools/contours_to_dxf.py hardware/laser/totobox_castle_contours.txt
       python3 hardware/tools/contours_to_dxf.py hardware/laser/totobox_manuel_contours.txt
"""

import math
import sys
from collections import defaultdict
from pathlib import Path

SNAP = 0.02


class Snapper:
    def __init__(self):
        self.points = []

    def __call__(self, x, y):
        for px, py in self.points:
            if abs(px - x) <= SNAP and abs(py - y) <= SNAP:
                return (px, py)
        self.points.append((x, y))
        return (x, y)


def read_parts(path):
    parts = {}
    current = None
    for raw in Path(path).read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line:
            continue
        if line.startswith("#"):
            name = line[1:].strip()
            # Commentaires d'en-tête: phrases avec espaces; noms de pièces: un seul mot
            if name and " " not in name:
                current = name
                parts[current] = []
            continue
        kind, *values = line.split()
        parts[current].append((kind, [float(v) for v in values]))
    return parts


def arc_angles(cx, cy, start, mid, end):
    """Angles de départ et d'arrivée (degrés, sens trigonométrique) de l'arc passant par mid."""
    def angle(p):
        return math.degrees(math.atan2(p[1] - cy, p[0] - cx)) % 360

    a0, am, a1 = angle(start), angle(mid), angle(end)
    sweep = (a1 - a0) % 360
    if (am - a0) % 360 <= sweep:
        return a0, a1
    return a1, a0


def clean_part(entities, snap):
    """Fusionne les extrémités proches, retire les segments nuls; renvoie les entités et les extrémités libres."""
    cleaned = []
    degree = defaultdict(int)
    for kind, v in entities:
        if kind == "L":
            a, b = snap(v[0], v[1]), snap(v[2], v[3])
            if math.dist(a, b) <= SNAP:
                continue
            cleaned.append(("L", (a, b)))
            degree[a] += 1
            degree[b] += 1
        elif kind == "C":
            cleaned.append(("C", ((v[0], v[1]), v[2])))
        elif kind == "A":
            a, m, b = snap(v[3], v[4]), (v[5], v[6]), snap(v[7], v[8])
            cleaned.append(("A", ((v[0], v[1]), v[2], a, m, b)))
            degree[a] += 1
            degree[b] += 1
        else:
            raise ValueError(f"entité inconnue: {kind}")
    open_ends = [p for p, d in degree.items() if d % 2]
    return cleaned, open_ends


def dxf_entities(layer, entities, dx=0.0, dy=0.0):
    out = []
    for kind, data in entities:
        if kind == "L":
            (x0, y0), (x1, y1) = data
            out += ["0", "LINE", "8", layer, "10", f"{x0 + dx:.4f}", "20", f"{y0 + dy:.4f}", "30", "0",
                    "11", f"{x1 + dx:.4f}", "21", f"{y1 + dy:.4f}", "31", "0"]
        elif kind == "C":
            (cx, cy), r = data
            out += ["0", "CIRCLE", "8", layer, "10", f"{cx + dx:.4f}", "20", f"{cy + dy:.4f}", "30", "0", "40", f"{r:.4f}"]
        else:
            (cx, cy), r, a, m, b = data
            start, end = arc_angles(cx, cy, a, m, b)
            out += ["0", "ARC", "8", layer, "10", f"{cx + dx:.4f}", "20", f"{cy + dy:.4f}", "30", "0", "40", f"{r:.4f}",
                    "50", f"{start:.4f}", "51", f"{end:.4f}"]
    return out


def dxf_document(layers, body):
    header = ["0", "SECTION", "2", "HEADER", "9", "$ACADVER", "1", "AC1009", "9", "$INSUNITS", "70", "4",
              "9", "$MEASUREMENT", "70", "1", "0", "ENDSEC"]
    tables = ["0", "SECTION", "2", "TABLES", "0", "TABLE", "2", "LAYER", "70", str(len(layers))]
    for index, layer in enumerate(layers):
        tables += ["0", "LAYER", "2", layer, "70", "0", "62", str(index % 6 + 1), "6", "CONTINUOUS"]
    tables += ["0", "ENDTAB", "0", "ENDSEC"]
    entities = ["0", "SECTION", "2", "ENTITIES"] + body + ["0", "ENDSEC", "0", "EOF"]
    return "\n".join(header + tables + entities) + "\n"


def layer_name(name):
    # ARRIERE.MICROSD -> calque ARRIERE_MICROSD (le point n'est pas sûr dans un nom de calque DXF R12).
    return name.replace(".", "_")


def extent(entities):
    xs, ys = [], []
    for kind, data in entities:
        if kind == "L":
            for x, y in data:
                xs.append(x)
                ys.append(y)
        else:
            (cx, cy), r = data[0], data[1]
            xs += [cx - r, cx + r]
            ys += [cy - r, cy + r]
    return min(xs), min(ys), max(xs), max(ys)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    source = Path(sys.argv[1])
    base = source.with_name(source.stem.replace("_contours", ""))
    parts = read_parts(source)

    sheet_body = []
    problems = 0
    pieces = {}
    for name, entities in parts.items():
        cleaned, open_ends = clean_part(entities, Snapper())
        x0, y0, x1, y1 = extent(cleaned)
        status = "fermé" if not open_ends else f"{len(open_ends)} extrémité(s) libre(s): {open_ends[:4]}"
        problems += bool(open_ends)
        print(f"{name:16s} {x1 - x0:7.2f} x {y1 - y0:6.2f} mm, {len(cleaned):3d} entités, contour {status}")
        sheet_body += dxf_entities(layer_name(name), cleaned)
        pieces.setdefault(name.split(".")[0], []).append((name, cleaned))

    # Une pièce et ses options dans le même fichier, ramenées à l'origine de la pièce.
    for piece, groups in pieces.items():
        x0, y0, _, _ = extent(groups[0][1])
        body = []
        for name, cleaned in groups:
            body += dxf_entities(layer_name(name), cleaned, -x0, -y0)
        part_file = Path(f"{base}_{piece.lower()}.dxf")
        part_file.write_text(dxf_document([layer_name(name) for name, _ in groups], body), encoding="ascii")

    sheet_file = Path(f"{base}_plaque.dxf")
    sheet_file.write_text(dxf_document([layer_name(name) for name in parts], sheet_body), encoding="ascii")
    print(f"-> {sheet_file} (+ un fichier par pièce)")
    if problems:
        sys.exit(f"{problems} pièce(s) avec un contour ouvert")


if __name__ == "__main__":
    main()
