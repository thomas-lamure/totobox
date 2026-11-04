#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Convertit les contours des pièces de 3 mm (hardware/laser/*_contours.txt) en DXF pour la découpe laser.

Format d'entrée (millimètres, origine au coin de la plaque):
  # NOM_DE_PIECE           début d'une pièce
  L x0 y0 x1 y1            segment
  C cx cy r                cercle
  P cx cy r                perçage à la main (trou fraisé...): pas découpé, seulement marqué d'une croix (GRAVURE)
  A cx cy r x0 y0 xm ym x1 y1   arc de (x0, y0) à (x1, y1) passant par (xm, ym)
  # PIECE.OPTION           découpe optionnelle de PIECE (fente microSD...), sur son propre calque

Sorties, à côté du fichier d'entrée:
  <base>_plaque.dxf        toute la plaque (calepinage), un calque par pièce et par option, plus GRAVURE
  <base>_<piece>.dxf       chaque pièce seule (avec ses options et sa gravure), ramenée à l'origine

Calque GRAVURE (à graver avant la découpe, ou à désactiver): une croix de centrage sur chaque trou, chaque perçage à
la main (P, jamais découpé) et chaque arrondi
(un peu au-delà des trous, petite dans les arrondis, comme le gabarit PDF) et le nom de la pièce, sans accents (police
du logiciel de découpe), là où il tient sans toucher un tracé. Rien pour les découpes optionnelles. Les pièces sont vues
de leur face intérieure: la gravure reste cachée une fois la boîte montée.

Les extrémités distantes de moins de SNAP mm sont fusionnées (le modèle Onshape laisse des écarts de 0,01 mm aux
pieds des créneaux), puis chaque contour est contrôlé: une extrémité qui n'est partagée que par une seule arête
signale un contour ouvert. Pas de compensation de saignée: elle se règle dans le logiciel de découpe (ou par la
variable #saignee_compensation dans Onshape).

Usage: python3 hardware/tools/contours_to_dxf.py hardware/laser/totobox_castle_contours.txt
       python3 hardware/tools/contours_to_dxf.py hardware/laser/totobox_manuel_contours.txt
"""

import math
import sys
import unicodedata
from collections import defaultdict
from pathlib import Path

SNAP = 0.02
GRAVURE = "GRAVURE"
TEXTE_GRAVURE = 3.0  # hauteur du nom des pièces, en mm

NOMS = {
    "FOND": "Fond",
    "FACADE": "Façade",
    "ARRIERE": "Arrière",
    "COTE_GAUCHE": "Côté gauche",
    "COTE_DROIT": "Côté droit",
    "CLOISON": "Cloison",
    "COUVRE_LEDS": "Couvre-LEDs",
    "DESSUS": "Dessus",
    "BANDE_GUIDE": "Bande guide-cartes",
}
OPTIONS = {"MICROSD": "microSD (optionnel)"}


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
        elif kind in ("C", "P"):
            cleaned.append((kind, ((v[0], v[1]), v[2])))
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
        elif kind == "P":
            continue  # percé à la main: seule sa croix est gravée
        elif kind == "T":
            # Texte centré (72 = 4, « middle ») sur son point d'alignement.
            (x, y), height, value = data
            out += ["0", "TEXT", "8", layer, "10", f"{x + dx:.4f}", "20", f"{y + dy:.4f}", "30", "0",
                    "40", f"{height:.4f}", "1", value, "72", "4", "11", f"{x + dx:.4f}", "21", f"{y + dy:.4f}", "31", "0"]
        else:
            (cx, cy), r, a, m, b = data
            start, end = arc_angles(cx, cy, a, m, b)
            out += ["0", "ARC", "8", layer, "10", f"{cx + dx:.4f}", "20", f"{cy + dy:.4f}", "30", "0", "40", f"{r:.4f}",
                    "50", f"{start:.4f}", "51", f"{end:.4f}"]
    return out


def ascii_text(value):
    """Texte DXF R12 en ASCII: « Côté droit » -> « Cote droit »."""
    return unicodedata.normalize("NFKD", value).encode("ascii", "ignore").decode("ascii")


def engraving(piece, cleaned):
    """Gravure d'une pièce: croix de centrage des trous (C), des perçages à la main (P) et des arrondis (A), puis son
    nom s'il tient.

    Le nom (hauteur TEXTE_GRAVURE, largeur estimée à 0,7 em par caractère) est centré en largeur, au cinquième de la
    hauteur, sinon au milieu, sinon aux quatre cinquièmes; il doit rester dans la pièce, à l'écart de tous ses tracés."""
    out = []
    centers = set()
    px0, py0, px1, py1 = extent(cleaned)
    for kind, data in cleaned:
        if kind == "L":
            continue
        (cx, cy), r = data[0], data[1]
        key = (round(cx, 3), round(cy, 3))
        if key in centers:
            continue
        centers.add(key)
        arm = max(1.2, r + 0.8) if kind in ("C", "P") else min(1.2, r)
        # Pas jusqu'au bord de la pièce (trous de vis dans les coins du fond).
        arm = min(arm, cx - px0 - 0.5, px1 - cx - 0.5, cy - py0 - 0.5, py1 - cy - 0.5)
        out += [("L", ((cx - arm, cy), (cx + arm, cy))), ("L", ((cx, cy - arm), (cx, cy + arm)))]

    x0, y0, x1, y1 = px0, py0, px1, py1
    name = ascii_text(NOMS.get(piece, piece))
    half_w, half_h = len(name) * 0.7 * TEXTE_GRAVURE / 2, TEXTE_GRAVURE / 2
    obstacles = [extent([e]) for e in cleaned] + [extent([e]) for e in out]
    for y in (y0 + max(5.0, (y1 - y0) / 5), (y0 + y1) / 2, y1 - max(5.0, (y1 - y0) / 5)):
        x = (x0 + x1) / 2
        box = (x - half_w - 0.8, y - half_h - 0.8, x + half_w + 0.8, y + half_h + 0.8)
        inside = x0 < box[0] and box[2] < x1 and y0 < box[1] and box[3] < y1
        if inside and not any(box[0] < b[2] and b[0] < box[2] and box[1] < b[3] and b[1] < box[3] for b in obstacles):
            out.append(("T", ((x, y), TEXTE_GRAVURE, name)))
            break
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
        if kind == "T":
            continue
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

    # Une pièce, ses options et sa gravure dans le même fichier, ramenées à l'origine de la pièce.
    for piece, groups in pieces.items():
        x0, y0, _, _ = extent(groups[0][1])
        marks = engraving(piece, groups[0][1])
        sheet_body += dxf_entities(GRAVURE, marks)
        body = []
        for name, cleaned in groups:
            body += dxf_entities(layer_name(name), cleaned, -x0, -y0)
        body += dxf_entities(GRAVURE, marks, -x0, -y0)
        part_file = Path(f"{base}_{piece.lower()}.dxf")
        part_file.write_text(dxf_document([layer_name(name) for name, _ in groups] + [GRAVURE], body), encoding="ascii")

    sheet_file = Path(f"{base}_plaque.dxf")
    sheet_file.write_text(dxf_document([layer_name(name) for name in parts] + [GRAVURE], sheet_body), encoding="ascii")
    print(f"-> {sheet_file} (+ un fichier par pièce)")
    if problems:
        sys.exit(f"{problems} pièce(s) avec un contour ouvert")


if __name__ == "__main__":
    main()
