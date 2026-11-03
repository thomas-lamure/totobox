#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests de contours_to_cutplan.py (bibliothèque standard seulement).

Lancer depuis la racine du dépôt:
  python3 -m unittest discover -s hardware/tools -p "test_*.py"
"""

import sys
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))

import contours_to_cutplan as cut  # noqa: E402
import contours_to_dxf as c2d  # noqa: E402
import contours_to_pdf as c2p  # noqa: E402

MANUEL = TOOLS.parent / "laser" / "totobox_manuel_contours.txt"


def rect(x0, y0, x1, y1):
    return [("L", [x0, y0, x1, y0]), ("L", [x1, y0, x1, y1]), ("L", [x1, y1, x0, y1]), ("L", [x0, y1, x0, y0])]


def clean(entities):
    return c2d.clean_part(entities, c2d.Snapper())[0]


class PlanTests(unittest.TestCase):
    def test_manual_version_is_cut_in_strips_then_pieces(self):
        boxes = cut.piece_boxes(c2d.read_parts(MANUEL))
        cuts = cut.plan_cuts(boxes)
        self.assertEqual([c["n"] for c in cuts], list(range(1, len(cuts) + 1)))
        # Trois bandes sur toute la plaque d'abord, puis les traits verticaux de chaque bande.
        strips = [c for c in cuts if c["level"] == 0]
        self.assertEqual([c["axis"] for c in strips], ["h", "h", "h"])
        self.assertEqual([round(c["size"], 2) for c in strips], [64, 50, 57.43])
        self.assertEqual(cuts[:3], strips)
        self.assertTrue(all(c["axis"] == "v" for c in cuts if c["level"] == 1))
        self.assertEqual(len(cuts), 15)

    def test_every_piece_ends_alone_at_its_size(self):
        boxes = cut.piece_boxes(c2d.read_parts(MANUEL))
        finals = [c["names"][0] for c in cut.plan_cuts(boxes) if cut.is_final(c, boxes)]
        self.assertCountEqual(finals, boxes)

    def test_measures_start_from_the_previous_cut(self):
        boxes = {"A": (0, 0, 30, 10), "B": (32, 0, 52, 10)}
        cuts = cut.plan_cuts(boxes)
        self.assertEqual([(c["axis"], c["size"]) for c in cuts], [("h", 10), ("v", 30), ("v", 20)])
        rows = [cut.describe(c, cuts, boxes) for c in cuts]
        self.assertEqual(rows[1][0], "Bande A, depuis la gauche")
        self.assertEqual(rows[2][0], "Bande A, depuis le trait 2")
        self.assertEqual(rows[0][2], "bande A (10 de haut), le reste est une chute")

    def test_trim_is_added_when_a_piece_is_lower_than_its_strip(self):
        boxes = {"A": (0, 0, 30, 10), "B": (32, 0, 52, 20)}
        cuts = cut.plan_cuts(boxes)
        trims = [c for c in cuts if c["level"] == 2]
        self.assertEqual([(c["axis"], c["names"], c["size"]) for c in trims], [("h", ["A"], 10)])
        self.assertTrue(cut.describe(cuts[1], cuts, boxes)[2].startswith("ébauche: A"))

    def test_pinwheel_layout_is_refused(self):
        # Aucune coupe droite ne traverse ce calepinage en moulinet.
        boxes = {"A": (0, 0, 20, 10), "B": (22, 0, 32, 20), "C": (12, 22, 32, 32), "D": (0, 12, 10, 32),
                 "E": (12, 12, 20, 20)}
        with self.assertRaises(ValueError):
            cut.plan_cuts(boxes)

    def test_pieces_closer_than_the_kerf_are_refused(self):
        with self.assertRaises(ValueError):
            cut.plan_cuts({"A": (0, 0, 30, 10), "B": (31, 0, 50, 10)})


class FinishingTests(unittest.TestCase):
    def test_chamfer_and_notch_are_told_apart(self):
        chamfered = clean([("L", [0, 0, 10, 0]), ("L", [10, 0, 12, 2]), ("L", [12, 2, 12, 20]),
                           ("L", [12, 20, 0, 20]), ("L", [0, 20, 0, 0])])
        self.assertEqual(cut.corner_features(chamfered, (0, 0, 12, 20)), [("bas droit", "chanfrein", 2, 2)])
        notched = clean([("L", [4, 0, 20, 0]), ("L", [20, 0, 20, 10]), ("L", [20, 10, 0, 10]),
                         ("L", [0, 10, 0, 3]), ("L", [0, 3, 4, 3]), ("L", [4, 3, 4, 0])])
        self.assertEqual(cut.corner_features(notched, (0, 0, 20, 10)), [("bas gauche", "encoche", 4, 3)])

    def test_rounded_slot_gets_its_corner_drills(self):
        parts = c2d.read_parts(MANUEL)
        boxes = cut.piece_boxes(parts)
        lines = cut.finishing_lines(parts, "ARRIERE", boxes["ARRIERE"])
        self.assertTrue(any(line.startswith("Fente 12,5 × 6,7") for line in lines))
        self.assertTrue(any("4 × Ø3,5" in line for line in lines))
        self.assertTrue(any(line.startswith("Fente microSD (optionnel)") for line in lines))

    def test_holes_are_measured_from_the_piece_corner(self):
        parts = {"FOND": rect(10, 20, 60, 50) + [("C", [15, 25, 0.8])]}
        lines = cut.finishing_lines(parts, "FOND", cut.piece_boxes(parts)["FOND"])
        self.assertEqual(lines, ["1 × Ø1,6: (5 ; 5)"])

    def test_manual_notches_of_the_card_guide(self):
        parts = c2d.read_parts(MANUEL)
        lines = cut.finishing_lines(parts, "BANDE_GUIDE", cut.piece_boxes(parts)["BANDE_GUIDE"])
        self.assertIn("4,3 de large × 10,5 de haut aux coins bas gauche et bas droit", lines[0])


class PdfTests(unittest.TestCase):
    def test_plan_is_two_a4_portrait_pages(self):
        pages = cut.build_pages(c2d.read_parts(MANUEL), "essai")
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "x.pdf"
            c2p.write_pdf(pages, out)
            data = out.read_bytes()
        self.assertIn(b"/Count 2", data)
        self.assertEqual(data.count(b"/MediaBox [0 0 595.28 841.89]"), 2)


if __name__ == "__main__":
    unittest.main()
