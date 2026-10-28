# SPDX-License-Identifier: GPL-3.0-or-later
# Raccourcis des commandes du projet (les mêmes que la CI, .github/workflows/ci.yml).
# PlatformIO (pio) pour le firmware, Python 3 (bibliothèque standard) pour les outils du boîtier.

PYTHON ?= python3
LASER := hardware/laser
TOOLS := hardware/tools

.PHONY: help firmware webfs upload uploadfs monitor mock test test-native test-scripts test-cad cad check-cad

help:
	@echo "make firmware    compile le firmware (pio run)"
	@echo "make webfs       construit l'image LittleFS de la page web (pio run -t buildfs)"
	@echo "make upload      téléverse le firmware, make uploadfs la page web, make monitor ouvre la console"
	@echo "make mock        faux serveur de la boîte: page web sans matériel (http://localhost:8080)"
	@echo "make test        tous les tests sur ordinateur (firmware, scripts, outils du boîtier)"
	@echo "make cad         régénère les DXF et les gabarits PDF depuis les contours"
	@echo "make check-cad   vérifie que les DXF et PDF versionnés sont à jour"

firmware:
	pio run

webfs:
	pio run -t buildfs

upload:
	pio run -t upload

uploadfs:
	pio run -t uploadfs

monitor:
	pio device monitor

mock:
	node scripts/mock-server.mjs

test: test-native test-scripts test-cad

test-native:
	pio test -e native

test-scripts:
	$(PYTHON) -m unittest discover -s scripts -p "test_*.py"

test-cad:
	$(PYTHON) -m unittest discover -s $(TOOLS) -p "test_*.py"

cad:
	$(PYTHON) $(TOOLS)/contours_to_dxf.py $(LASER)/totobox_castle_contours.txt
	$(PYTHON) $(TOOLS)/contours_to_pdf.py $(LASER)/totobox_castle_contours.txt "version à créneaux"
	$(PYTHON) $(TOOLS)/contours_to_dxf.py $(LASER)/totobox_manuel_contours.txt
	$(PYTHON) $(TOOLS)/contours_to_pdf.py $(LASER)/totobox_manuel_contours.txt "version fabriquée à la main"

check-cad: cad
	git diff --exit-code -- $(LASER)
