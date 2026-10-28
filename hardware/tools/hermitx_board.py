#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Modèle 3D simplifié de la carte HermitX CardReader (99 x 62 mm).

Source unique des cotes: génère la Feature Onshape (FeatureScript) et, en option,
une vue de contrôle qui superpose les composants à la photo cotée de la carte.

    python3 hardware/tools/hermitx_board.py                  # écrit hardware/onshape/hermitx_board.fs
    python3 hardware/tools/hermitx_board.py --overlay photo.png out.svg

Repère: origine au coin inférieur gauche de la carte vue de dessus (côté LEDs en bas,
USB-C en haut), X vers la droite, Y vers le haut, Z = 0 sur la face inférieure du PCB.
Toutes les cotes sont en mm, relevées sur la photo cotée (précision ~ ±0,5 mm):
à vérifier au pied à coulisse avant de dessiner un boîtier serré.
"""

import base64
import sys
from pathlib import Path

BOARD_W = 99.0
BOARD_H = 62.0
BOARD_T = 1.6
CORNER_R = 4.0

# Trous de fixation (entraxe 91 x 54 mm d'après la cote), M3.
MOUNT_HOLE_D = 3.2
MOUNT_HOLES = [(4.0, 4.0), (95.0, 4.0), (4.0, 58.0), (95.0, 58.0)]

# Trous traversants des connecteurs non soudés (pas 2,54 mm).
PIN_HOLE_D = 1.0
IO_PINS = [(43.37 + 2.54 * i, 20.2, label) for i, label in enumerate(["IO9", "IO10", "IO11", "IO45", "IO46", "IO47", "IO48"])]
POWER_PINS = [(57.45, 47.3, "+5V"), (57.45, 44.76, "+3V3"), (57.45, 42.22, "GND")]
SERIAL_PINS = [(90.7, 18.1, "RX"), (93.24, 18.1, "TX")]
PIN_HOLES = [(x, y) for x, y, _ in IO_PINS + POWER_PINS + SERIAL_PINS]

# Barrettes mâles droites 2,54 mm soudées (RX/TX laissés en simples trous).
HEADER_PITCH = 2.54
HEADER_BASE_H = 2.5      # socle plastique au-dessus du PCB
PIN_SECTION = 0.64       # broche carrée
PIN_ABOVE_BASE = 6.0     # longueur au-dessus du socle
PIN_BELOW_PCB = 3.0      # longueur sous le PCB
HEADERS = [("Barrette IO", IO_PINS), ("Barrette alimentation", POWER_PINS)]


def header_base(pins):
    """Socle plastique: enveloppe des broches + demi-pas de chaque côté."""
    half = HEADER_PITCH / 2
    xs = [x for x, _, _ in pins]
    ys = [y for _, y, _ in pins]
    return min(xs) - half, min(ys) - half, max(xs) + half, max(ys) + half

COLORS = {
    "metal": (0.75, 0.76, 0.78),
    "black": (0.08, 0.08, 0.09),
    "white": (0.93, 0.93, 0.90),
    "shield": (0.82, 0.83, 0.85),
    "module": (0.10, 0.12, 0.25),
    "led": (0.96, 0.96, 0.92),
    "button": (0.35, 0.35, 0.38),
    "antenna": (0.85, 0.65, 0.20),
    "pin": (0.90, 0.75, 0.30),
}

# (nom, x0, y0, x1, y1, hauteur au-dessus du PCB, couleur, z de départ au-dessus du PCB)
BOXES = [
    ("USB-C", 59.8, 55.0, 68.8, 62.6, 3.2, "metal", 0.0),
    ("Support microSD", 75.2, 43.0, 90.8, 60.2, 1.9, "metal", 0.0),
    # ESP32-S3-WROOM-1 (18 x 25,5 mm): l'antenne imprimée dépasse de 4,5 mm du bord droit de la carte.
    ("Module ESP32-S3 (PCB)", 78.0, 21.25, 103.5, 39.25, 0.8, "module", 0.0),
    ("Module ESP32-S3 (blindage)", 79.2, 22.5, 96.6, 38.0, 2.3, "shield", 0.8),
    ("Module ESP32-S3 (zone antenne)", 97.2, 22.0, 103.0, 38.5, 0.05, "antenna", 0.8),
    ("Connecteur NFC", 1.4, 26.9, 6.0, 35.2, 4.5, "white", 0.0),
    ("Connecteur haut-parleur", 44.3, 54.8, 52.8, 60.5, 4.5, "white", 0.0),
    ("Bouton RESET", 67.9, 34.6, 73.9, 40.6, 3.5, "button", 0.0),
    ("Bouton BOOT", 67.9, 18.9, 73.9, 24.9, 3.5, "button", 0.0),
    ("Regulateur U2", 46.4, 38.6, 54.3, 44.8, 1.7, "black", 0.0),
    ("PN532", 25.6, 27.5, 32.0, 34.4, 0.9, "black", 0.0),
    ("Ampli MAX98357", 42.7, 48.6, 46.0, 51.9, 0.8, "black", 0.0),
    ("Quartz", 28.25, 23.3, 31.8, 26.1, 0.8, "metal", 0.0),
]

# LEDs WS2812B 5 x 5 mm, en bas de la carte.
LED_SIZE = 5.0
LED_Y = 7.5
LED_X = [9.1, 20.6, 32.1, 43.5, 54.9, 66.4, 77.9, 89.3]
for index, x in enumerate(LED_X):
    BOXES.append((f"LED {index + 1}", x - LED_SIZE / 2, LED_Y - LED_SIZE / 2, x + LED_SIZE / 2,
                  LED_Y + LED_SIZE / 2, 1.6, "led", 0.0))

# Poussoirs des boutons: (nom, x, y, diamètre, hauteur, départ au-dessus du PCB)
CYLINDERS = [
    ("Poussoir RESET", 70.9, 37.6, 3.5, 1.5, 3.5),
    ("Poussoir BOOT", 70.9, 21.9, 3.5, 1.5, 3.5),
]


def featurescript() -> str:
    def num(value: float) -> str:
        return f"{value:g}"

    lines = []
    out = lines.append
    out("// Carte HermitX CardReader: modèle simplifié pour dessiner le boîtier TotoBox.")
    out("// Fichier généré par hardware/tools/hermitx_board.py: modifier les cotes là-bas, pas ici.")
    out("// Coller sous l'en-tête 'FeatureScript ...; import(...)' créé par Onshape dans un nouveau Feature Studio.")
    out("")
    out("const PCB_THICKNESS = { (millimeter) : [0.4, %s, 5] } as LengthBoundSpec;" % num(BOARD_T))
    out("")
    # Onshape n'accepte que l'ASCII imprimable dans les annotations: pas d'accents ici.
    out('annotation { "Feature Type Name" : "Carte HermitX", "Feature Type Description" : "Carte HermitX CardReader simplifiee (99 x 62 mm), origine au coin inferieur gauche, Z = 0 sous le PCB." }')
    out("export const hermitxBoard = defineFeature(function(context is Context, id is Id, definition is map)")
    out("    precondition")
    out("    {")
    out('        annotation { "Name" : "Epaisseur du PCB" }')
    out("        isLength(definition.pcbThickness, PCB_THICKNESS);")
    out("    }")
    out("    {")
    out("        const t = definition.pcbThickness / millimeter;")
    out("")
    out("        // PCB aux coins arrondis")
    out('        fCuboid(context, id + "pcb", { "corner1" : vector(0, 0, 0) * millimeter, "corner2" : vector(%s, %s, t) * millimeter });'
        % (num(BOARD_W), num(BOARD_H)))
    out('        opFillet(context, id + "pcbCorners", { "entities" : qParallelEdges(qCreatedBy(id + "pcb", EntityType.EDGE), Z_DIRECTION), "radius" : %s * millimeter });'
        % num(CORNER_R))
    out("")
    out("        // Trous de fixation et trous des connecteurs non soudés")
    holes = [(x, y, MOUNT_HOLE_D) for x, y in MOUNT_HOLES] + [(x, y, PIN_HOLE_D) for x, y in PIN_HOLES]
    for index, (x, y, d) in enumerate(holes):
        out('        fCylinder(context, id + "holes" + "h%d", { "bottomCenter" : vector(%s, %s, -1) * millimeter, "topCenter" : vector(%s, %s, t + 1) * millimeter, "radius" : %s * millimeter });'
            % (index, num(x), num(y), num(x), num(y), num(d / 2)))
    out('        opBoolean(context, id + "holesCut", { "tools" : qCreatedBy(id + "holes", EntityType.BODY), "targets" : qCreatedBy(id + "pcb", EntityType.BODY), "operationType" : BooleanOperationType.SUBTRACTION });')
    out('        dressPart(context, qCreatedBy(id + "pcb", EntityType.BODY), "PCB HermitX", color(0.07, 0.07, 0.08));')
    out("")
    out("        // Composants (face supérieure)")
    for index, (name, x0, y0, x1, y1, h, col, z0) in enumerate(BOXES):
        r, g, b = COLORS[col]
        out('        fCuboid(context, id + "c%d", { "corner1" : vector(%s, %s, t + %s) * millimeter, "corner2" : vector(%s, %s, t + %s) * millimeter });'
            % (index, num(x0), num(y0), num(z0), num(x1), num(y1), num(z0 + h)))
        out('        dressPart(context, qCreatedBy(id + "c%d", EntityType.BODY), "%s", color(%s, %s, %s));'
            % (index, name, num(r), num(g), num(b)))
    for index, (name, x, y, d, h, z0) in enumerate(CYLINDERS):
        r, g, b = COLORS["black"]
        out('        fCylinder(context, id + "p%d", { "bottomCenter" : vector(%s, %s, t + %s) * millimeter, "topCenter" : vector(%s, %s, t + %s) * millimeter, "radius" : %s * millimeter });'
            % (index, num(x), num(y), num(z0), num(x), num(y), num(z0 + h), num(d / 2)))
        out('        dressPart(context, qCreatedBy(id + "p%d", EntityType.BODY), "%s", color(%s, %s, %s));'
            % (index, name, num(r), num(g), num(b)))
    out("")
    out("        // Barrettes mâles: socle noir et broches traversantes dorées")
    pin_top = HEADER_BASE_H + PIN_ABOVE_BASE
    half = PIN_SECTION / 2
    for h_index, (name, pins) in enumerate(HEADERS):
        x0, y0, x1, y1 = header_base(pins)
        r, g, b = COLORS["black"]
        out('        fCuboid(context, id + "hb%d", { "corner1" : vector(%s, %s, t) * millimeter, "corner2" : vector(%s, %s, t + %s) * millimeter });'
            % (h_index, num(x0), num(y0), num(x1), num(y1), num(HEADER_BASE_H)))
        out('        dressPart(context, qCreatedBy(id + "hb%d", EntityType.BODY), "%s (socle)", color(%s, %s, %s));'
            % (h_index, name, num(r), num(g), num(b)))
        r, g, b = COLORS["pin"]
        for p_index, (x, y, label) in enumerate(pins):
            out('        fCuboid(context, id + "hp%d_%d", { "corner1" : vector(%s, %s, -%s) * millimeter, "corner2" : vector(%s, %s, t + %s) * millimeter });'
                % (h_index, p_index, num(x - half), num(y - half), num(PIN_BELOW_PCB), num(x + half), num(y + half), num(pin_top)))
            out('        dressPart(context, qCreatedBy(id + "hp%d_%d", EntityType.BODY), "Broche %s", color(%s, %s, %s));'
                % (h_index, p_index, label, num(r), num(g), num(b)))
    out("    });")
    out("")
    out("function dressPart(context is Context, part is Query, name is string, appearance is Color)")
    out("{")
    out('    setProperty(context, { "entities" : part, "propertyType" : PropertyType.NAME, "value" : name });')
    out('    setProperty(context, { "entities" : part, "propertyType" : PropertyType.APPEARANCE, "value" : appearance });')
    out("}")
    return "\n".join(lines) + "\n"


# Repère de la photo cotée (1920 x 1080): bords de la carte relevés en pixels.
PHOTO_LEFT, PHOTO_RIGHT, PHOTO_TOP, PHOTO_BOTTOM = 388, 1578, 165, 908


def overlay(photo: Path, output: Path) -> None:
    sx = (PHOTO_RIGHT - PHOTO_LEFT) / BOARD_W
    sy = (PHOTO_BOTTOM - PHOTO_TOP) / BOARD_H

    def px(x: float, y: float):
        return PHOTO_LEFT + x * sx, PHOTO_BOTTOM - y * sy

    shapes = []
    x0, y0 = px(0, BOARD_H)
    shapes.append(f'<rect x="{x0}" y="{y0}" width="{BOARD_W * sx}" height="{BOARD_H * sy}" rx="{CORNER_R * sx}" '
                  'fill="none" stroke="#ff00ff" stroke-width="3"/>')
    for x, y in MOUNT_HOLES:
        cx, cy = px(x, y)
        shapes.append(f'<circle cx="{cx}" cy="{cy}" r="{MOUNT_HOLE_D / 2 * sx}" fill="none" stroke="#ff00ff" stroke-width="3"/>')
    for x, y in PIN_HOLES:
        cx, cy = px(x, y)
        shapes.append(f'<circle cx="{cx}" cy="{cy}" r="{PIN_HOLE_D / 2 * sx}" fill="none" stroke="#00c853" stroke-width="2"/>')
    for name, bx0, by0, bx1, by1, *_ in BOXES:
        left, top = px(bx0, by1)
        shapes.append(f'<rect x="{left}" y="{top}" width="{(bx1 - bx0) * sx}" height="{(by1 - by0) * sy}" '
                      'fill="#ff00ff" fill-opacity="0.18" stroke="#ff00ff" stroke-width="2"/>')
    for name, x, y, d, *_ in CYLINDERS:
        cx, cy = px(x, y)
        shapes.append(f'<circle cx="{cx}" cy="{cy}" r="{d / 2 * sx}" fill="none" stroke="#00c853" stroke-width="2"/>')
    for _, pins in HEADERS:
        hx0, hy0, hx1, hy1 = header_base(pins)
        left, top = px(hx0, hy1)
        shapes.append(f'<rect x="{left}" y="{top}" width="{(hx1 - hx0) * sx}" height="{(hy1 - hy0) * sy}" '
                      'fill="#00c853" fill-opacity="0.15" stroke="#00c853" stroke-width="2"/>')
    data = base64.b64encode(photo.read_bytes()).decode()
    output.write_text(
        '<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="1920" height="1080">'
        f'<image width="1920" height="1080" xlink:href="data:image/png;base64,{data}"/>' + "".join(shapes) + "</svg>"
    )


if __name__ == "__main__":
    if len(sys.argv) == 4 and sys.argv[1] == "--overlay":
        overlay(Path(sys.argv[2]), Path(sys.argv[3]))
    else:
        target = Path(__file__).resolve().parent.parent / "onshape" / "hermitx_board.fs"
        target.write_text(featurescript(), encoding="utf-8")
        print(f"{target} écrit")
