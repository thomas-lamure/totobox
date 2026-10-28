#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Imprime le calepinage d'une plaque de 300 x 200 mm (hardware/laser/*_contours.txt) sur une page A4 paysage.

Échelle 1:1: les pièces (pas le bord de la plaque, plus grand que la feuille) sont centrées sur la page. Le script
refuse un calepinage qui ne laisserait pas MARGE_MIN mm sur chaque bord (zone non imprimable des imprimantes
courantes: 4 à 6,35 mm). Une ligne de texte n'est ajoutée sous les pièces que si elle tient dans la marge restante.
Imprimer en « taille réelle » (100 %), jamais « ajuster à la page ».

Les découpes optionnelles (# PIECE.OPTION dans le fichier de contours, fente microSD...) sont en pointillés.
Chaque trou et chaque arrondi a une croix de centrage, et chaque groupe de trous identiques son diamètre de
foret, quand le texte tient dans la pièce sans chevaucher une découpe.
Bibliothèque standard seulement (PDF écrit à la main, tracés vectoriels).

Usage: python3 hardware/tools/contours_to_pdf.py hardware/laser/totobox_castle_contours.txt "version à créneaux"
Sortie: <base>_calepinage_a4.pdf à côté du fichier d'entrée.
"""

import math
import sys
import zlib
from pathlib import Path

from contours_to_dxf import Snapper, arc_angles, clean_part, extent, read_parts

PT = 72 / 25.4  # points par millimètre
A4_PAYSAGE = (297.0, 210.0)
MARGE_MIN = 6.5
TEXTE_MM = 2.5  # hauteur d'une ligne de texte en 7 pt, interligne compris

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


def pdf_text(value):
    return "(" + value.replace("\\", "\\\\").replace("(", "\\(").replace(")", "\\)") + ")"


class Page:
    """Flux de dessin d'une page, en millimètres (origine en bas à gauche de la feuille)."""

    def __init__(self, width, height):
        self.width = width
        self.height = height
        self.ops = []

    def add(self, op):
        self.ops.append(op)

    def stroke_style(self, width_mm=0.25, gray=0.0, dash=None):
        pattern = "[" + " ".join(f"{d * PT:.2f}" for d in dash) + "] 0 d" if dash else "[] 0 d"
        self.add(f"{width_mm * PT:.3f} w {gray:.2f} G {pattern}")

    def line(self, x0, y0, x1, y1):
        self.add(f"{x0 * PT:.3f} {y0 * PT:.3f} m {x1 * PT:.3f} {y1 * PT:.3f} l S")

    def arc(self, cx, cy, r, start_deg, end_deg, move=True):
        # Arc dans le sens trigonométrique, en courbes de Bézier de 90° au plus.
        sweep = (end_deg - start_deg) % 360 or 360
        steps = max(1, math.ceil(sweep / 90))
        delta = math.radians(sweep / steps)
        k = 4 / 3 * math.tan(delta / 4)
        a = math.radians(start_deg)
        ops = []
        if move:
            ops.append(f"{(cx + r * math.cos(a)) * PT:.3f} {(cy + r * math.sin(a)) * PT:.3f} m")
        for _ in range(steps):
            b = a + delta
            p1 = (cx + r * (math.cos(a) - k * math.sin(a)), cy + r * (math.sin(a) + k * math.cos(a)))
            p2 = (cx + r * (math.cos(b) + k * math.sin(b)), cy + r * (math.sin(b) - k * math.cos(b)))
            p3 = (cx + r * math.cos(b), cy + r * math.sin(b))
            ops.append(" ".join(f"{v * PT:.3f}" for v in (*p1, *p2, *p3)) + " c")
            a = b
        self.add(" ".join(ops) + " S")

    def text(self, x, y, value, size=8, bold=False, align="left", gray=0.0):
        font = "F2" if bold else "F1"
        width = text_width(value, size)
        if align == "center":
            x -= width / 2
        elif align == "right":
            x -= width
        self.add(f"{gray:.2f} g BT /{font} {size} Tf {x * PT:.3f} {y * PT:.3f} Td {pdf_text(value)} Tj ET 0 g")


def text_width(value, size):
    # Largeur prudente en Helvetica (0,55 em en moyenne), en millimètres: pour centrer et éviter les chevauchements.
    return len(value) * size * 0.55 / PT


def text_box(x, y, value, size, align="left"):
    """Rectangle occupé par un texte (x, y: ligne de base), en millimètres."""
    width = text_width(value, size)
    left = x - width / 2 if align == "center" else (x - width if align == "right" else x)
    return (left, y - 0.3 * size / PT, left + width, y + 0.75 * size / PT)


def overlaps(a, b, gap=0.6):
    return a[0] < b[2] + gap and b[0] < a[2] + gap and a[1] < b[3] + gap and b[1] < a[3] + gap


def entity_box(kind, data):
    if kind == "L":
        (x0, y0), (x1, y1) = data
        return (min(x0, x1), min(y0, y1), max(x0, x1), max(y0, y1))
    (cx, cy), r = data[0], data[1]
    return (cx - r, cy - r, cx + r, cy + r)


def drill_label(diameter):
    return f"Ø{diameter:.2f}".rstrip("0").rstrip(".").replace(".", ",")


def round_groups(entities):
    """Trous (C, centres à moins de 12 mm) et arrondis d'une même fente (A, à moins de 25 mm) par diamètre."""
    groups = []
    for kind, data in entities:
        if kind == "L":
            continue
        (cx, cy), r = data[0], data[1]
        diameter = round(2 * r, 2)
        for group in groups:
            if group["diameter"] == diameter and group["kind"] == kind and any(
                    math.hypot(cx - gx, cy - gy) < (12 if kind == "C" else 25) for gx, gy, _ in group["centers"]):
                group["centers"].append((cx, cy, r))
                break
        else:
            groups.append({"kind": kind, "diameter": diameter, "centers": [(cx, cy, r)]})
    return groups


def draw_entities(page, entities, ox, oy):
    for kind, data in entities:
        if kind == "L":
            (x0, y0), (x1, y1) = data
            page.line(ox + x0, oy + y0, ox + x1, oy + y1)
        elif kind == "C":
            (cx, cy), r = data
            page.arc(ox + cx, oy + cy, r, 0, 360)
        else:
            (cx, cy), r, a, m, b = data
            start, end = arc_angles(cx, cy, a, m, b)
            page.arc(ox + cx, oy + cy, r, start, end)


def draw_parts(page, parts, ox, oy):
    """Dessine les pièces (coordonnées de plaque, échelle 1:1) à partir de (ox, oy) sur la page.

    Croix de centrage sur chaque trou et chaque arrondi, et diamètre de foret par groupe de trous identiques,
    seulement là où le texte tient dans la pièce sans toucher une découpe ni un autre texte."""
    pieces = {}
    for name, entities in parts.items():
        piece, _, option = name.partition(".")
        pieces.setdefault(piece, []).append((option, clean_part(entities, Snapper())[0]))

    for piece, groups in pieces.items():
        px0, py0, px1, py1 = extent(groups[0][1])
        # Le texte reste dans la pièce, à l'écart de tous ses tracés (bords, créneaux, découpes) et des autres textes.
        inner = (px0 + 0.8, py0 + 0.8, px1 - 0.8, py1 - 0.8)
        obstacles = [entity_box(kind, data) for _, cleaned in groups for kind, data in cleaned]

        def place(box):
            if not (inner[0] <= box[0] and box[2] <= inner[2] and inner[1] <= box[1] and box[3] <= inner[3]):
                return False
            if any(overlaps(box, other) for other in obstacles):
                return False
            obstacles.append(box)
            return True

        for option, cleaned in groups:
            if option:
                page.stroke_style(0.3, 0.0, dash=(1.6, 1.0))
            else:
                page.stroke_style(0.25, 0.0)
            draw_entities(page, cleaned, ox, oy)

        # Croix de centrage (traits fins, pleins), un peu au-delà des trous, petites dans les arrondis.
        page.stroke_style(0.12, 0.0)
        for _, cleaned in groups:
            for group in round_groups(cleaned):
                for cx, cy, r in group["centers"]:
                    arm = max(1.2, r + 0.8) if group["kind"] == "C" else min(1.2, r)
                    page.line(ox + cx - arm, oy + cy, ox + cx + arm, oy + cy)
                    page.line(ox + cx, oy + cy - arm, ox + cx, oy + cy + arm)

        # Nom de la pièce (au milieu, au cinquième de la hauteur), puis libellé des options.
        name = NOMS.get(piece, piece)
        x, y = (px0 + px1) / 2, py0 + max(5.0, (py1 - py0) / 5)
        if place(text_box(x, y, name, 6, "center")):
            page.text(ox + x, oy + y, name, size=6, align="center", gray=0.35)
        for option, cleaned in groups:
            if not option:
                continue
            sx0, sy0, sx1, sy1 = extent(cleaned)
            label = OPTIONS.get(option, option.lower())
            x, y = (sx0 + sx1) / 2, sy1 + 1.2
            if place(text_box(x, y, label, 5, "center")):
                page.text(ox + x, oy + y, label, size=5, align="center", gray=0.3)

        # Diamètre de foret: autour de chaque groupe (côtés, puis diagonales), au premier endroit libre.
        for _, cleaned in groups:
            for group in round_groups(cleaned):
                count = len(group["centers"])
                label = drill_label(group["diameter"])
                if group["kind"] == "C" and count > 1:
                    label = f"{count} × {label}"
                gx0 = min(cx - r for cx, _, r in group["centers"])
                gx1 = max(cx + r for cx, _, r in group["centers"])
                gy0 = min(cy - r for _, cy, r in group["centers"])
                gy1 = max(cy + r for _, cy, r in group["centers"])
                mid = (gy0 + gy1) / 2 - 0.6
                right, left = (gx1 + 1.2, mid, "left"), (gx0 - 1.2, mid, "right")
                above, below = ((gx0 + gx1) / 2, gy1 + 1.2, "center"), ((gx0 + gx1) / 2, gy0 - 2.6, "center")
                # Côté tourné vers le milieu de la pièce d'abord: les bords restent libres pour les trous des coins.
                sides = [right, left] if gx0 + gx1 < px0 + px1 else [left, right]
                ends = [above, below] if gy0 + gy1 < py0 + py1 else [below, above]
                diagonals = [(gx1 + 0.8, gy1 + 0.8, "left"), (gx0 - 0.8, gy1 + 0.8, "right"),
                             (gx1 + 0.8, gy0 - 2.4, "left"), (gx0 - 0.8, gy0 - 2.4, "right")]
                for x, y, align in sides + ends + diagonals:
                    if place(text_box(x, y, label, 5, align)):
                        page.text(ox + x, oy + y, label, size=5, align=align, gray=0.15)
                        break


def layout_page(parts, title):
    """Les pièces à l'échelle 1:1, centrées sur une page A4 paysage."""
    x0, y0, x1, y1 = extent([e for entities in parts.values() for e in clean_part(entities, Snapper())[0]])
    page = Page(*A4_PAYSAGE)
    margin_x = (page.width - (x1 - x0)) / 2
    margin_y = (page.height - (y1 - y0)) / 2
    if min(margin_x, margin_y) < MARGE_MIN:
        raise ValueError(f"pièces de {x1 - x0:.1f} x {y1 - y0:.1f} mm: moins de {MARGE_MIN:g} mm de marge sur A4")
    draw_parts(page, parts, margin_x - x0, margin_y - y0)
    # Une ligne sous les pièces, seulement si elle tient entre la zone non imprimable et le dessin.
    note = (f"TotoBox, {title}: échelle 1:1, imprimer en taille réelle (100 %). Pointillés: découpe optionnelle. "
            "Ø: foret à utiliser (centre marqué d'une croix).")
    if margin_y - MARGE_MIN >= TEXTE_MM + 1 and text_width(note, 7) <= page.width - 2 * MARGE_MIN:
        page.text(page.width / 2, MARGE_MIN + 0.5, note, size=7, align="center", gray=0.3)
    return page


def write_pdf(pages, path):
    objects = []

    def add(body):
        objects.append(body)
        return len(objects)

    font = add(b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>")
    bold = add(b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold /Encoding /WinAnsiEncoding >>")
    pages_id = len(objects) + 1
    objects.append(None)  # réservé pour /Pages
    kids = []
    for page in pages:
        content = zlib.compress("\n".join(page.ops).encode("cp1252"))
        stream = add(b"<< /Length %d /Filter /FlateDecode >>\nstream\n" % len(content) + content + b"\nendstream")
        kids.append(add(
            f"<< /Type /Page /Parent {pages_id} 0 R /MediaBox [0 0 {page.width * PT:.2f} {page.height * PT:.2f}] "
            f"/Resources << /Font << /F1 {font} 0 R /F2 {bold} 0 R >> >> /Contents {stream} 0 R >>".encode()))
    objects[pages_id - 1] = (f"<< /Type /Pages /Kids [{' '.join(f'{k} 0 R' for k in kids)}] "
                             f"/Count {len(kids)} >>").encode()
    catalog = add(f"<< /Type /Catalog /Pages {pages_id} 0 R >>".encode())

    out = bytearray(b"%PDF-1.4\n%\xe2\xe3\xcf\xd3\n")
    offsets = []
    for number, body in enumerate(objects, start=1):
        offsets.append(len(out))
        out += f"{number} 0 obj\n".encode() + body + b"\nendobj\n"
    xref = len(out)
    out += f"xref\n0 {len(objects) + 1}\n0000000000 65535 f \n".encode()
    out += b"".join(f"{offset:010d} 00000 n \n".encode() for offset in offsets)
    out += f"trailer\n<< /Size {len(objects) + 1} /Root {catalog} 0 R >>\nstartxref\n{xref}\n%%EOF\n".encode()
    Path(path).write_bytes(bytes(out))


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    source = Path(sys.argv[1])
    try:
        page = layout_page(read_parts(source), sys.argv[2])
    except ValueError as error:
        sys.exit(str(error))
    target = source.with_name(source.stem.replace("_contours", "") + "_calepinage_a4.pdf")
    write_pdf([page], target)
    print(f"-> {target}")


if __name__ == "__main__":
    main()
