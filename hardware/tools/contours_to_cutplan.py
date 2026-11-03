#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Plan de coupe à la scie d'une plaque (hardware/laser/*_contours.txt), sur deux pages A4 portrait.

Pour la version faite à la main, sans gabarit imprimé. Les coupes sont déduites du calepinage: d'abord les traits
horizontaux sur toute la plaque (bandes), puis les traits verticaux de chaque bande, puis les recoupes, jusqu'à ce
que chaque pièce soit seule à ses cotes (coupes « guillotine »: un calepinage qui n'en permet pas est refusé).
Les pièces sont celles de la plaque, à 2 mm l'une de l'autre: le trait de scie (2 mm) tombe entre elles. Chaque
cote est mesurée depuis le bord qui vient d'être scié; on scie du côté chute du trait.

Page 1: schéma de la plaque avec les traits numérotés et le tableau des coupes. Page 2: finitions (coins
chanfreinés, encoches, biseaux) et perçages, en coordonnées depuis le coin bas gauche de chaque pièce vue de sa
face intérieure. Bibliothèque standard seulement.

Usage: python3 hardware/tools/contours_to_cutplan.py hardware/laser/totobox_manuel_contours.txt "version fabriquée à la main"
Sortie: <base>_plan_de_coupe.pdf à côté du fichier d'entrée.
"""

import math
import sys
from pathlib import Path

from contours_to_dxf import Snapper, arc_angles, clean_part, extent, read_parts
from contours_to_pdf import NOMS, OPTIONS, PT, Page, drill_label, text_width, write_pdf

A4_PORTRAIT = (210.0, 297.0)
KERF = 2.0  # trait de scie, égal à l'écart entre les pièces du calepinage
PLAQUE = (300.0, 200.0)
TOL = 0.05
ROUGE = "0.85 0.10 0.10"

# Finitions qui ne se lisent pas dans les contours (README du boîtier).
NOTES = {
    "BANDE_GUIDE": "Découpée droite: biseau à 80° au ponçage.",
    "COUVRE_LEDS": "Tracé par sa grande face: biseau à 45° de ses deux grands bords au ponçage.",
}


def mm(value):
    """Cote au dixième, virgule décimale (« 57,4 », « 64 »)."""
    text = f"{value:.1f}".replace(".", ",")
    return text[:-2] if text.endswith(",0") else text


def mm2(value):
    """Coordonnée au centième au plus (« 72,05 », « 12,6 »)."""
    return f"{value:.2f}".rstrip("0").rstrip(".").replace(".", ",")


def piece_boxes(parts):
    """Rectangle englobant de chaque pièce (sans les découpes optionnelles), dans l'ordre du fichier."""
    return {name: extent(clean_part(entities, Snapper())[0]) for name, entities in parts.items() if "." not in name}


def plan_cuts(boxes, kerf=KERF):
    """Coupes guillotine, dans l'ordre: traits horizontaux de la plaque, puis de niveau en niveau.

    Chaque coupe: numéro `n`, sens `axis` ("h": trait horizontal, "v": vertical), niveau `level`, bord de la
    pièce sur la plaque `at`, cote depuis le bord de départ de la zone `size`, zone obtenue `result` (x0, y0, x1,
    y1; inf du côté du reste de la plaque) et ses pièces `names`, noms de la zone sciée et de la zone obtenue
    (`where`, `zone`), coupe qui a donné la zone sciée `source`, coupe précédente dans la même zone `after` et
    `waste` quand il ne reste qu'une chute."""
    x0 = min(b[0] for b in boxes.values())
    y0 = min(b[1] for b in boxes.values())
    queue = [((x0, y0, math.inf, math.inf), list(boxes), "h", 0, False, "Plaque", None)]
    cuts = []
    while queue:
        region, names, axis, level, flipped, label, source = queue.pop(0)
        lo, hi = (1, 3) if axis == "h" else (0, 2)
        groups = []
        for name in sorted(names, key=lambda n: boxes[n][lo]):
            start, end = boxes[name][lo], boxes[name][hi]
            if groups and start < groups[-1][1] - TOL:
                groups[-1][1] = max(groups[-1][1], end)
                groups[-1][2].append(name)
            else:
                if groups and start - groups[-1][1] < kerf - TOL:
                    raise ValueError(f"moins d'un trait de scie entre {groups[-1][2][-1]} et {name}")
                groups.append([start, end, [name]])
        tail = region[hi] - groups[-1][1]
        if len(groups) == 1 and tail <= TOL:
            box = boxes[names[0]]
            if len(names) == 1 and all(abs(box[i] - region[i]) <= TOL for i in range(4)):
                continue  # pièce seule à ses cotes
            if flipped:
                raise ValueError(f"pas de coupe droite pour séparer {', '.join(names)}")
            queue.append((region, names, "v" if axis == "h" else "h", level, True, label, source))
            continue
        previous = None
        for index, (start, end, members) in enumerate(groups):
            sub = list(region)
            sub[lo], sub[hi] = start, end
            if level == 0:
                zone = "bande " + "ABCDEFGHIJKLMNOPQRSTUVWXYZ"[index]
            elif len(members) == 1:
                zone = NOMS.get(members[0], members[0])
            else:
                zone = "bloc " + " + ".join(NOMS.get(n, n) for n in members)
            last = index == len(groups) - 1
            if not last or tail > TOL:
                cut = {"axis": axis, "level": level, "at": end, "size": end - start, "region": region,
                       "result": tuple(sub), "names": members, "where": label, "source": source,
                       "zone": zone, "after": previous, "waste": last}
                cuts.append(cut)
                previous = cut
            else:
                cut = None
            # Zone qu'on recoupe ensuite: une pièce seule n'y est encore qu'ébauchée.
            where = zone[0].upper() + zone[1:] + (" (ébauche)" if len(members) == 1 else "")
            queue.append((tuple(sub), members, "v" if axis == "h" else "h", level + 1, False, where, cut))
    cuts.sort(key=lambda c: c["level"])
    for number, cut in enumerate(cuts, start=1):
        cut["n"] = number
    return cuts


def is_final(cut, boxes):
    """La coupe donne une pièce seule, à ses cotes."""
    if len(cut["names"]) != 1:
        return False
    box, sub = boxes[cut["names"][0]], cut["result"]
    return all(abs(box[i] - sub[i]) <= TOL for i in range(4))


def describe(cut, cuts, boxes):
    """Les trois colonnes du tableau: où scier, cote à tracer, résultat."""
    start = "depuis le bas" if cut["axis"] == "h" else "depuis la gauche"
    source = cut["source"]
    # Un bloc de plusieurs pièces se désigne par le trait qui l'a détaché (nom court pour le tableau).
    zone = f"Bloc du trait {source['n']}" if source and source["level"] > 0 and len(source["names"]) > 1 else cut["where"]
    where = zone + ", " + (f"depuis le trait {cut['after']['n']}" if cut["after"] else start)
    x0, y0, x1, y1 = cut["result"]
    if cut["level"] == 0:
        result = f"{cut['zone']} ({mm(y1 - y0)} de haut)"
        if cut["waste"]:
            result += ", le reste est une chute"
    else:
        dims = f"{mm(x1 - x0)} × {mm(y1 - y0)}"
        name = cut["zone"]
        if len(cut["names"]) == 1 and not is_final(cut, boxes):
            result = f"ébauche: {name} {dims}"
        else:
            result = f"{name} {dims}"
    return where, mm(cut["size"]), result


# --- Finitions et perçages ---------------------------------------------------------------------------------

def _ends(kind, data):
    if kind == "L":
        return [data[0], data[1]]
    return [data[2], data[4]]


def _near(p, q, tol=0.02):
    return abs(p[0] - q[0]) <= tol and abs(p[1] - q[1]) <= tol


def corner_features(entities, box):
    """Coins de la pièce qui ne sont pas à angle droit: [(coin, "chanfrein" | "encoche", dx, dy)].

    Coin: "bas gauche", "bas droit", "haut gauche", "haut droit". dx, dy: retrait le long des bords bas/haut et
    gauche/droit. Un trait reliant directement les deux retraits est un chanfrein, sinon c'est une encoche."""
    x0, y0, x1, y1 = box
    lines = [data for kind, data in entities if kind == "L"]
    out = []
    for label, cx, cy in (("bas gauche", x0, y0), ("bas droit", x1, y0), ("haut gauche", x0, y1), ("haut droit", x1, y1)):
        ends = [p for line in lines for p in line]
        if any(_near(p, (cx, cy)) for p in ends):
            continue
        on_h = [p for p in ends if abs(p[1] - cy) <= 0.02]
        on_v = [p for p in ends if abs(p[0] - cx) <= 0.02]
        if not on_h or not on_v:
            continue
        ph = min(on_h, key=lambda p: abs(p[0] - cx))
        pv = min(on_v, key=lambda p: abs(p[1] - cy))
        chamfer = any((_near(a, ph) and _near(b, pv)) or (_near(a, pv) and _near(b, ph)) for a, b in lines)
        out.append((label, "chanfrein" if chamfer else "encoche", abs(ph[0] - cx), abs(pv[1] - cy)))
    return out


def inner_cutouts(entities, box):
    """Découpes intérieures (fentes), hors trous ronds: [(x0, y0, x1, y1, [(cx, cy, r) des arrondis])].

    Tracés regroupés par extrémités communes; une découpe touche le bord de la pièce n'est pas intérieure."""
    inner = []
    for kind, data in entities:
        if kind == "C":
            continue
        ex = extent([(kind, data)])
        if ex[0] > box[0] + TOL and ex[1] > box[1] + TOL and ex[2] < box[2] - TOL and ex[3] < box[3] - TOL:
            inner.append((kind, data))
    groups = []
    for item in inner:
        ends = _ends(*item)
        joined = [g for g in groups if any(_near(p, q) for p in ends for q in g["ends"])]
        merged = {"items": [item], "ends": list(ends)}
        for g in joined:
            merged["items"] += g["items"]
            merged["ends"] += g["ends"]
            groups.remove(g)
        groups.append(merged)
    out = []
    for g in groups:
        sx0, sy0, sx1, sy1 = extent(g["items"])
        rounds = sorted({(round(d[0][0], 3), round(d[0][1], 3), d[1]) for k, d in g["items"] if k == "A"},
                        key=lambda c: (c[1], c[0]))
        out.append((sx0, sy0, sx1, sy1, rounds))
    return sorted(out, key=lambda s: (s[0], s[1]))


def finishing_lines(parts, name, box):
    """Lignes de texte de la page 2 pour une pièce: coins, découpes, trous, notes."""
    x0, y0 = box[0], box[1]

    def at(x, y):
        return f"({mm(x - x0)} ; {mm(y - y0)})"

    entities = clean_part(parts[name], Snapper())[0]
    lines = []
    by_shape = {}
    for corner, kind, dx, dy in corner_features(entities, box):
        by_shape.setdefault((kind, round(dx, 2), round(dy, 2)), []).append(corner)
    for (kind, dx, dy), corners in by_shape.items():
        where = ("au coin " if len(corners) == 1 else "aux coins ") + " et ".join(corners)
        if kind == "chanfrein":
            lines.append(f"Chanfrein de {mm2(dx)} × {mm2(dy)} {where}, à la lime ou au ponçage.")
        else:
            lines.append(f"Encoche de {mm2(dx)} de large × {mm2(dy)} de haut {where} (deux traits de scie).")

    sources = [(name, entities)] + [(n, clean_part(e, Snapper())[0]) for n, e in parts.items()
                                    if n.startswith(name + ".")]
    for source, ents in sources:
        option = source.partition(".")[2]
        suffix = f" {OPTIONS.get(option, option.lower())}" if option else ""
        for sx0, sy0, sx1, sy1, rounds in inner_cutouts(ents, box):
            lines.append(f"Fente{suffix} {mm2(sx1 - sx0)} × {mm2(sy1 - sy0)}: de x = {mm2(sx0 - x0)} à "
                         f"{mm2(sx1 - x0)}, de y = {mm2(sy0 - y0)} à {mm2(sy1 - y0)}"
                         + (", angles vifs." if not rounds else ","))
            if rounds:
                r = rounds[0][2]
                lines.append(f"    coins R{mm2(r)}: {len(rounds)} × {drill_label(2 * r)} en "
                             + "  ".join(at(cx, cy) for cx, cy, _ in rounds) + ", puis scier ou limer entre.")

    holes = {}
    for kind, data in entities:
        if kind == "C":
            holes.setdefault(round(2 * data[1], 2), []).append(data[0])
    for diameter, centers in sorted(holes.items()):
        label = drill_label(diameter)
        coords = [at(cx, cy) for cx, cy in sorted(centers, key=lambda c: (round(c[1], 1), c[0]))]
        head = f"{len(centers)} × {label}: " if len(centers) > 1 else f"1 × {label}: "
        lines.append(head + "  ".join(coords[:6]))
        for i in range(6, len(coords), 6):
            lines.append("    " + "  ".join(coords[i:i + 6]))

    if name in NOTES:
        lines.append(NOTES[name])
    return lines or ["Rien à faire après la découpe."]


# --- Pages -------------------------------------------------------------------------------------------------

def _disc(page, x, y, label):
    r = 2.1
    page.add(f"{ROUGE} rg " + " ".join(
        f"{(x + r * math.cos(k * math.pi / 12)) * PT:.2f} {(y + r * math.sin(k * math.pi / 12)) * PT:.2f} "
        + ("m" if k == 0 else "l") for k in range(24)) + " h f")
    page.add(f"1 g BT /F2 6 Tf {(x - text_width(label, 6) / 2 + 0.15) * PT:.2f} {(y - 0.75) * PT:.2f} Td "
             f"({label}) Tj ET 0 g")


def plan_page(parts, title, boxes, cuts):
    page = Page(*A4_PORTRAIT)
    left, width = 15.0, 180.0
    ox, oy = min(b[0] for b in boxes.values()), min(b[1] for b in boxes.values())
    ux = max(b[2] for b in boxes.values()) - ox
    uy = max(b[3] for b in boxes.values()) - oy
    page.text(left, 282, f"TotoBox, {title}: plan de coupe à la scie", size=14, bold=True)
    page.text(left, 275, f"Contreplaqué de 3 mm, plaque de {mm(PLAQUE[0])} × {mm(PLAQUE[1])} (au moins "
              f"{math.ceil(ux)} × {math.ceil(uy)}). Cotes en mm, pièces vues de leur face intérieure.",
              size=8, gray=0.25)
    page.text(left, 271, "Mesurer depuis le bord qui vient d'être scié et scier du côté chute du trait: "
              f"la lame mange {mm(KERF)} mm, déjà prévus entre les pièces.", size=8, gray=0.25)

    scale = width / PLAQUE[0]
    top = 266.0
    base = top - PLAQUE[1] * scale

    def P(x, y):
        return left + (x - ox) * scale, base + (y - oy) * scale

    page.stroke_style(0.3, 0.55)
    page.add(f"{left * PT:.2f} {base * PT:.2f} {width * PT:.2f} {PLAQUE[1] * scale * PT:.2f} re S")
    page.text(left, base - 3.5, f"plaque {mm(PLAQUE[0])} × {mm(PLAQUE[1])}; bords de départ: en bas et à gauche",
              size=6, gray=0.5)

    for name, entities in parts.items():
        page.stroke_style(0.2, 0.0, dash=(1.0, 0.7) if "." in name else None)
        for kind, data in clean_part(entities, Snapper())[0]:
            if kind == "L":
                page.line(*P(*data[0]), *P(*data[1]))
            elif kind == "C":
                page.arc(*P(*data[0]), data[1] * scale, 0, 360)
            else:
                (cx, cy), r, a, m, b = data
                page.arc(*P(cx, cy), r * scale, *arc_angles(cx, cy, a, m, b))
    for name, (x0, y0, x1, y1) in boxes.items():
        page.text(*P((x0 + x1) / 2, y0 + max(5.0, (y1 - y0) / 5)), NOMS.get(name, name), size=6, align="center",
                  gray=0.3)

    xmax, ymax = ox + PLAQUE[0], oy + PLAQUE[1]
    for cut in cuts:
        main = cut["level"] <= 1
        page.stroke_style(0.45 if main else 0.35, 0.0, dash=None if main else (1.2, 0.6))
        page.add(f"{ROUGE} RG")
        rx0, ry0, rx1, ry1 = cut["region"]
        pos = cut["at"] + KERF / 2
        if cut["axis"] == "h":
            (u0, v), (u1, _) = P(rx0, pos), P(min(rx1, xmax), pos)
            page.line(u0, v, u1, v)
            label = (u1 + 3.2, v) if cut["level"] == 0 else ((u0 + u1) / 2, v)
        else:
            (u, v0), (_, v1) = P(pos, ry0), P(pos, min(ry1, ymax))
            page.line(u, v0, u, v1)
            label = (u, (v0 + v1) / 2)
        page.add("0 G")
        _disc(page, *label, str(cut["n"]))

    y = base - 13
    page.text(left, y, "Ordre des coupes", size=11, bold=True)
    y -= 6
    rows = [describe(cut, cuts, boxes) for cut in cuts]
    col_size = left + 10 + max(text_width(r[0], 8.5) for r in rows) + 4
    cols = (left, left + 10, col_size, col_size + 14)
    for x, head in zip(cols, ("N°", "Où", "Cote", "Résultat")):
        page.text(x, y, head, size=8, bold=True)
    y -= 1.5
    page.stroke_style(0.2, 0.4)
    page.line(left, y, left + width, y)
    headings = {0: "Traits horizontaux, sur toute la longueur de la plaque",
                1: "Traits verticaux, dans chaque bande"}
    shown = set()
    for cut in cuts:
        level = min(cut["level"], 2)
        if level not in shown:
            shown.add(level)
            y -= 6
            page.text(left, y, headings.get(level, "Recoupes (pointillés sur le schéma)"), size=8.5, bold=True,
                      gray=0.15)
            y -= 1
        where, size, result = rows[cut["n"] - 1]
        y -= 4.6
        page.text(cols[0] + 1, y, str(cut["n"]), size=8.5, bold=True)
        page.text(cols[1], y, where, size=8.5)
        page.text(cols[2], y, size, size=9, bold=True)
        page.text(cols[3], y, result, size=8.5)
    if y < 18:
        raise ValueError("trop de coupes pour une page")
    page.text(left, 10, "Encoches, chanfreins, biseaux et perçages en page 2.", size=7.5, gray=0.35)
    return page


def finishing_page(parts, boxes, cuts, template):
    page = Page(*A4_PORTRAIT)
    left = 15.0
    page.text(left, 282, "Finitions et perçages", size=14, bold=True)
    page.text(left, 275, "Coordonnées (x ; y) en mm depuis le coin bas gauche de la pièce posée face intérieure vers "
              "soi, dans le sens du schéma", size=8, gray=0.25)
    page.text(left, 271, "de la page 1 (x vers la droite, y vers le haut). Ø: diamètre du foret, au centre indiqué.",
              size=8, gray=0.25)
    y = 261.0
    for name, box in boxes.items():
        page.text(left, y, f"{NOMS.get(name, name)} ({mm(box[2] - box[0])} × {mm(box[3] - box[1])})", size=10,
                  bold=True)
        y -= 5
        for line in finishing_lines(parts, name, box):
            page.text(left + 4, y, line, size=8.2)
            y -= 4.3
        y -= 3
    if y < 18:
        raise ValueError("trop de finitions pour une page")
    page.text(left, 10, f"Pour pointer les trous, le gabarit {template} (échelle 1:1, à imprimer) reste le plus sûr.",
              size=7.5, gray=0.35)
    return page


def build_pages(parts, title, template="à l'échelle 1:1"):
    boxes = piece_boxes(parts)
    cuts = plan_cuts(boxes)
    return [plan_page(parts, title, boxes, cuts), finishing_page(parts, boxes, cuts, template)]


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    source = Path(sys.argv[1])
    base = source.stem.replace("_contours", "")
    try:
        pages = build_pages(read_parts(source), sys.argv[2], f"{base}_calepinage_a4.pdf")
    except ValueError as error:
        sys.exit(str(error))
    target = source.with_name(base + "_plan_de_coupe.pdf")
    write_pdf(pages, target)
    print(f"-> {target}")


if __name__ == "__main__":
    main()
