#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests de contours_to_dxf.py et contours_to_pdf.py (bibliothèque standard seulement).

Lancer depuis la racine du dépôt:
  python3 -m unittest discover -s hardware/tools -p "test_*.py"
"""

import math
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))

import contours_to_dxf as c2d  # noqa: E402
import contours_to_pdf as c2p  # noqa: E402

LASER = TOOLS.parent / "laser"
CONTOURS = LASER / "totobox_castle_contours.txt"
VERSIONS = [LASER / "totobox_castle_contours.txt", LASER / "totobox_manuel_contours.txt"]


def square(x0, y0, size, gap=0.0):
    """Carré fermé; `gap` décale une extrémité (écart de modélisation)."""
    x1, y1 = x0 + size, y0 + size
    return [
        ("L", [x0, y0, x1, y0]),
        ("L", [x1, y0, x1, y1]),
        ("L", [x1, y1, x0, y1]),
        ("L", [x0 + gap, y1, x0 + gap, y0]),
    ]


def read_dxf_entities(text):
    codes = text.split("\n")
    pairs = list(zip(codes[0::2], codes[1::2]))
    entities, current, inside = [], None, False
    for code, value in pairs:
        if code == "2" and value == "ENTITIES":
            inside = True
            continue
        if not inside:
            continue
        if code == "0":
            if current:
                entities.append(current)
            current = {"type": value} if value in ("LINE", "CIRCLE", "ARC") else None
        elif current is not None:
            current[code] = value
    return entities


class SnapTests(unittest.TestCase):
    def test_closed_square_has_no_open_end(self):
        cleaned, open_ends = c2d.clean_part(square(0, 0, 10), c2d.Snapper())
        self.assertEqual(len(cleaned), 4)
        self.assertEqual(open_ends, [])

    def test_model_gap_of_a_hundredth_is_merged(self):
        # Pied de créneau: 0,01 mm entre deux extrémités qui doivent se rejoindre.
        cleaned, open_ends = c2d.clean_part(square(0, 0, 10, gap=0.01), c2d.Snapper())
        self.assertEqual(open_ends, [])

    def test_real_gap_is_reported(self):
        cleaned, open_ends = c2d.clean_part(square(0, 0, 10, gap=0.5), c2d.Snapper())
        self.assertEqual(len(open_ends), 4)

    def test_zero_length_segment_is_dropped(self):
        entities = square(0, 0, 10) + [("L", [5, 5, 5.01, 5])]
        cleaned, open_ends = c2d.clean_part(entities, c2d.Snapper())
        self.assertEqual(len(cleaned), 4)
        self.assertEqual(open_ends, [])

    def test_full_circle_needs_no_neighbour(self):
        cleaned, open_ends = c2d.clean_part([("C", [5, 5, 1.25])], c2d.Snapper())
        self.assertEqual(open_ends, [])


class ArcTests(unittest.TestCase):
    def test_counter_clockwise_arc_keeps_its_ends(self):
        # Quart de cercle de (1, 0) à (0, 1) passant par 45°: sens trigonométrique.
        mid = (math.cos(math.radians(45)), math.sin(math.radians(45)))
        start, end = c2d.arc_angles(0, 0, (1, 0), mid, (0, 1))
        self.assertAlmostEqual(start, 0)
        self.assertAlmostEqual(end, 90)

    def test_clockwise_arc_is_reversed(self):
        # Même quart de cercle parcouru dans l'autre sens: le DXF le décrit de 0° à 90°.
        mid = (math.cos(math.radians(45)), math.sin(math.radians(45)))
        start, end = c2d.arc_angles(0, 0, (0, 1), mid, (1, 0))
        self.assertAlmostEqual(start, 0)
        self.assertAlmostEqual(end, 90)

    def test_major_arc_through_the_far_side(self):
        # De 0° à 90° en passant par 225°: arc de 270° de 90° à 360°.
        mid = (math.cos(math.radians(225)), math.sin(math.radians(225)))
        start, end = c2d.arc_angles(0, 0, (1, 0), mid, (0, 1))
        self.assertAlmostEqual(start, 90)
        self.assertAlmostEqual(end % 360, 0)

    def test_rounded_slot_end_is_closed(self):
        # Bout arrondi d'une fente (USB-C): deux segments et un demi-cercle, fermé par l'arc.
        entities = [
            ("L", [0, 1, 4, 1]),
            ("L", [0, -1, 4, -1]),
            ("L", [0, -1, 0, 1]),
            ("A", [4, 0, 1, 4, -1, 5, 0, 4, 1]),
        ]
        cleaned, open_ends = c2d.clean_part(entities, c2d.Snapper())
        self.assertEqual(open_ends, [])


class FileTests(unittest.TestCase):
    def test_reader_keeps_parts_and_skips_comments(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "x_contours.txt"
            path.write_text("# Commentaire d'en-tête avec espaces\n# FOND\nL 0 0 1 0\n# DESSUS\nC 1 1 0.5\n", encoding="utf-8")
            parts = c2d.read_parts(path)
        self.assertEqual(list(parts), ["FOND", "DESSUS"])
        self.assertEqual(parts["FOND"], [("L", [0.0, 0.0, 1.0, 0.0])])

    def test_dxf_document_is_well_formed(self):
        cleaned, _ = c2d.clean_part(square(0, 0, 10) + [("C", [5, 5, 1])], c2d.Snapper())
        text = c2d.dxf_document(["FOND"], c2d.dxf_entities("FOND", cleaned))
        self.assertTrue(text.startswith("0\nSECTION\n2\nHEADER"))
        self.assertTrue(text.rstrip().endswith("0\nEOF"))
        entities = read_dxf_entities(text)
        self.assertEqual(sorted(e["type"] for e in entities), ["CIRCLE", "LINE", "LINE", "LINE", "LINE"])
        self.assertTrue(all(e["8"] == "FOND" for e in entities))

    def test_optional_cutout_is_its_own_part_and_layer(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "x_contours.txt"
            path.write_text("# ARRIERE\nL 0 0 1 0\n# ARRIERE.MICROSD\nC 1 1 0.5\n", encoding="utf-8")
            parts = c2d.read_parts(path)
        self.assertEqual(list(parts), ["ARRIERE", "ARRIERE.MICROSD"])
        self.assertEqual(c2d.layer_name("ARRIERE.MICROSD"), "ARRIERE_MICROSD")

    def test_part_file_is_moved_to_the_origin(self):
        cleaned, _ = c2d.clean_part(square(40, 30, 10), c2d.Snapper())
        x0, y0, _, _ = c2d.extent(cleaned)
        entities = read_dxf_entities("0\nSECTION\n2\nENTITIES\n" + "\n".join(c2d.dxf_entities("P", cleaned, -x0, -y0)) + "\n0\nEOF")
        xs = [float(e[k]) for e in entities for k in ("10", "11")]
        self.assertAlmostEqual(min(xs), 0.0)


@unittest.skipUnless(CONTOURS.exists(), "contours de la version à créneaux absents")
class CastleJointExportTests(unittest.TestCase):
    """Contours réels de la version à créneaux: toutes les pièces fermées, fond sans trous."""

    def test_every_part_is_closed(self):
        for name, entities in c2d.read_parts(CONTOURS).items():
            with self.subTest(piece=name):
                _, open_ends = c2d.clean_part(entities, c2d.Snapper())
                self.assertEqual(open_ends, [])

    def test_bottom_plate_has_no_holes(self):
        parts = c2d.read_parts(CONTOURS)
        self.assertTrue(all(kind == "L" for kind, _ in parts["FOND"]))
        self.assertEqual(len(parts["FOND"]), 4)

    def test_parts_stay_on_the_sheet_with_a_margin(self):
        for name, entities in c2d.read_parts(CONTOURS).items():
            with self.subTest(piece=name):
                cleaned, _ = c2d.clean_part(entities, c2d.Snapper())
                x0, y0, x1, y1 = c2d.extent(cleaned)
                self.assertGreaterEqual(x0, 2 - 1e-6)
                self.assertGreaterEqual(y0, 2 - 1e-6)
                self.assertLessEqual(x1, 298 + 1e-6)
                self.assertLessEqual(y1, 198 + 1e-6)


class VersionTests(unittest.TestCase):
    """Les deux versions (créneaux et fabriquée à la main): pièces fermées, sur la plaque, fente microSD optionnelle."""

    def test_every_part_is_closed_and_on_the_sheet(self):
        for path in VERSIONS:
            for name, entities in c2d.read_parts(path).items():
                with self.subTest(version=path.stem, piece=name):
                    cleaned, open_ends = c2d.clean_part(entities, c2d.Snapper())
                    self.assertEqual(open_ends, [])
                    x0, y0, x1, y1 = c2d.extent(cleaned)
                    self.assertGreaterEqual(min(x0, y0), 2 - 1e-6)
                    self.assertLessEqual(x1, 298 + 1e-6)
                    self.assertLessEqual(y1, 198 + 1e-6)

    def test_microsd_slot_is_optional_and_inside_the_back(self):
        for path in VERSIONS:
            with self.subTest(version=path.stem):
                parts = c2d.read_parts(path)
                bx0, by0, bx1, by1 = c2d.extent(c2d.clean_part(parts["ARRIERE"], c2d.Snapper())[0])
                sx0, sy0, sx1, sy1 = c2d.extent(c2d.clean_part(parts["ARRIERE.MICROSD"], c2d.Snapper())[0])
                self.assertTrue(bx0 < sx0 < sx1 < bx1 and by0 < sy0 < sy1 < by1)
                # Fente de 18,6 x 4,9 mm, arrondie (rayon 1,5 mm).
                self.assertAlmostEqual(sx1 - sx0, 18.6, places=2)
                self.assertAlmostEqual(sy1 - sy0, 4.9, places=2)

    def test_back_is_seen_from_the_inside_in_both_versions(self):
        # Pièces vues de leur face intérieure (gabarit papier collé de ce côté): vu de l'intérieur, la fente
        # microSD est dans la moitié droite de l'arrière. Une pièce vue de l'extérieur l'aurait à gauche.
        for path in VERSIONS:
            with self.subTest(version=path.stem):
                parts = c2d.read_parts(path)
                bx0, _, bx1, _ = c2d.extent(c2d.clean_part(parts["ARRIERE"], c2d.Snapper())[0])
                sx0, _, sx1, _ = c2d.extent(c2d.clean_part(parts["ARRIERE.MICROSD"], c2d.Snapper())[0])
                self.assertGreater((sx0 + sx1) / 2, (bx0 + bx1) / 2)


class PdfTests(unittest.TestCase):
    def test_pdf_is_a_single_a4_landscape_page(self):
        page = c2p.layout_page(c2d.read_parts(CONTOURS), "essai")
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "x.pdf"
            c2p.write_pdf([page], out)
            data = out.read_bytes()
        self.assertTrue(data.startswith(b"%PDF-1.4"))
        self.assertTrue(data.rstrip().endswith(b"%%EOF"))
        self.assertIn(b"/Count 1", data)
        self.assertIn(b"/MediaBox [0 0 841.89 595.28]", data)

    def test_both_versions_fit_at_full_scale_with_printer_margins(self):
        for path in VERSIONS:
            with self.subTest(version=path.stem):
                parts = c2d.read_parts(path)
                x0, y0, x1, y1 = c2d.extent([e for v in parts.values() for e in c2d.clean_part(v, c2d.Snapper())[0]])
                self.assertLessEqual(x1 - x0 + 2 * c2p.MARGE_MIN, c2p.A4_PAYSAGE[0])
                self.assertLessEqual(y1 - y0 + 2 * c2p.MARGE_MIN, c2p.A4_PAYSAGE[1])
                c2p.layout_page(parts, "essai")

    def test_layout_wider_than_a4_is_refused(self):
        parts = {"FOND": square(0, 0, 10) + square(290, 0, 10)}
        with self.assertRaises(ValueError):
            c2p.layout_page(parts, "essai")

    def test_note_is_dropped_when_the_margin_is_too_small(self):
        # 197 mm de haut: 6,5 mm de marge, pas de place pour la ligne de texte.
        page = c2p.layout_page({"FOND": square(0, 0, 197)}, "essai")
        self.assertFalse(any("1:1" in op for op in page.ops))
        page = c2p.layout_page({"FOND": square(0, 0, 150)}, "essai")
        self.assertTrue(any("1:1" in op for op in page.ops))

    def test_every_hole_gets_its_drill_size(self):
        import re
        page = c2p.layout_page(c2d.read_parts(VERSIONS[1]), "essai")
        labels = [re.search(r"\((.*)\) Tj", op).group(1) for op in page.ops if " Tj " in op]
        # Fond: 4 avant-trous d'entretoise et 4 trous de vis; arrière: USB-C et microSD; côté droit: encodeur.
        self.assertEqual(labels.count("Ø1,6"), 4)
        self.assertEqual(labels.count("Ø2,2"), 4)
        for label in ("Ø3,5", "Ø3", "Ø7,5", "16 × Ø2,5"):
            self.assertIn(label, labels)

    def test_drill_label_uses_a_decimal_comma(self):
        self.assertEqual(c2p.drill_label(7.5), "Ø7,5")
        self.assertEqual(c2p.drill_label(3.0), "Ø3")
        self.assertEqual(c2p.drill_label(1.6), "Ø1,6")

    def test_label_that_does_not_fit_is_left_out(self):
        # Trou de 8 mm dans un carré de 10 mm: pas de place pour « Ø8 », la croix reste.
        page = c2p.layout_page({"FOND": square(0, 0, 10) + [("C", [5, 5, 4])]}, "essai")
        self.assertFalse(any("Ø8" in op for op in page.ops))

    def test_arc_is_drawn_counter_clockwise_in_quarter_turns(self):
        page = c2p.Page(*c2p.A4_PAYSAGE)
        page.arc(0, 0, 10, 0, 180)
        self.assertEqual(page.ops[-1].count(" c"), 2)


if __name__ == "__main__":
    unittest.main()
