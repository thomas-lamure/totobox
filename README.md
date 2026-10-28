<p align="center"><img src="docs/logo/totobox-logo.svg" alt="TotoBox" width="200"></p>

# TotoBox

[![CI](https://github.com/thomas-lamure/totobox/actions/workflows/ci.yml/badge.svg)](https://github.com/thomas-lamure/totobox/actions/workflows/ci.yml)
[![Code: GPL-3.0-or-later](https://img.shields.io/badge/code-GPL--3.0--or--later-blue)](LICENSES/GPL-3.0-or-later.txt)
[![Matériel et docs: CC BY-SA 4.0](https://img.shields.io/badge/mat%C3%A9riel%20%26%20docs-CC%20BY--SA%204.0-lightgrey)](LICENSES/CC-BY-SA-4.0.txt)

*[English version](README.en.md)*

Boîte à histoires et à musiques pour enfant, à fabriquer soi-même: l'enfant glisse une carte dans la fente de la boîte, l'histoire démarre; il la retire, elle s'arrête et reprendra au même endroit. Pas d'écran, pas de compte, pas de cloud: tout est lu depuis une carte microSD, et des LEDs disent ce que fait la boîte.

Le dépôt contient le firmware (ESP32-S3, carte HermitX CardReader), l'interface web de réglage pour les parents, et le boîtier en contreplaqué (modèles Onshape, plans de découpe laser ou gabarits à imprimer).

## Fonctionnalités

- **Lecture par carte NFC**: un fichier ou un dossier entier par carte, signet par carte (piste et position exacte), arrêt immédiat au retrait.
- **Molette** pour le volume, bornée par des limites réglées par les parents; **LEDs** pour l'état (prête, lecture, carte inconnue, erreur, volume).
- **Interface web pour le téléphone**: associer les cartes, envoyer et ranger les fichiers, régler le Wi-Fi et les limites. Assistant de première connexion, portail captif quand la boîte crée son propre point d'accès.
- **Verrouillage parental**: cartes maîtresses et code de secours, appliqué par la boîte elle-même.
- **Autonome et hors ligne**: le Wi-Fi ne sert qu'aux réglages; veille profonde au bout de 5 min, réveil en posant une carte.
- **Robuste**: carte SD retirée à chaud, carte exFAT détectée et formatable, secrets chiffrés entre la page et la boîte, protection contre les requêtes d'autres sites.

## Fabriquer une TotoBox

1. **Matériel**: une carte HermitX CardReader (fournie avec antenne NFC et haut-parleur), un encodeur KY-040, une carte microSD, des tags NFC, du contreplaqué de 3 mm, des entretoises et vis M2. Liste complète: [hardware/README.md](hardware/README.md#nomenclature).
2. **Boîtier**: découpe laser ([version à créneaux](hardware/README.md#deux-versions-du-boîtier)) ou à la scie avec un gabarit imprimé à l'échelle 1:1 ([version faite à la main](hardware/README.md#deux-versions-du-boîtier)). Fichiers dans [hardware/laser/](hardware/laser).
3. **Câblage**: seul l'encodeur est à brancher, voir [hardware/electronics.md](hardware/electronics.md).
4. **Firmware**: avec [PlatformIO](https://platformio.org/), carte branchée en USB-C:

   ```bash
   pio run -t upload      # firmware
   pio run -t uploadfs    # interface web
   ```

5. **Mise en route**: carte microSD en FAT32, puis se connecter au point d'accès `TotoBox-XXXX` avec le téléphone. Voir le [guide d'utilisation](docs/usage.md).

## Documentation

| Pour… | Lire |
|---|---|
| utiliser et régler la boîte | [docs/usage.md](docs/usage.md) |
| fabriquer le boîtier, câbler | [hardware/README.md](hardware/README.md), [hardware/electronics.md](hardware/electronics.md) |
| modifier le modèle 3D | [hardware/onshape/README.md](hardware/onshape/README.md) |
| compiler, tester, contribuer | [docs/development.md](docs/development.md), [CONTRIBUTING.md](CONTRIBUTING.md) |
| comprendre le firmware | [docs/architecture.md](docs/architecture.md), [AGENTS.md](AGENTS.md) (règles et pièges connus) |
| piloter la boîte par script | [docs/api.md](docs/api.md) |
| préparer des boîtes en série | [docs/production.md](docs/production.md) |

## État du projet et suite

La V1 (lecture locale, NFC, LEDs, molette, interface web, verrouillage) est complète et validée sur la boîte. Prévu ensuite:

- V2 connectée: cartes qui pilotent Home Assistant ou un Chromecast, sans casser la lecture locale;
- pas de jauge de batterie: la V1 est alimentée en USB-C, sans capteur.

Les évolutions sont listées dans [CHANGELOG.md](CHANGELOG.md).

## Licences

- **Code** (firmware, interface web, scripts, tests): [GPL-3.0-or-later](LICENSES/GPL-3.0-or-later.txt). Le firmware embarque [ESP32-audioI2S](https://github.com/schreibfaul1/ESP32-audioI2S), sous GPL-3.0.
- **Matériel** (boîtier, modèles Onshape, plans de découpe, câblage), **documentation** et **logo**: [CC BY-SA 4.0](LICENSES/CC-BY-SA-4.0.txt).
- La page web embarque [tweetnacl-js](https://github.com/dchest/tweetnacl-js) (domaine public, [Unlicense](LICENSES/Unlicense.txt)).

Le détail par fichier est dans [REUSE.toml](REUSE.toml) ([REUSE](https://reuse.software)).

Bibliothèques utilisées: [ArduinoJson](https://arduinojson.org/) (MIT), [Adafruit PN532](https://github.com/adafruit/Adafruit-PN532) (BSD), [Arduino-ESP32](https://github.com/espressif/arduino-esp32) (LGPL-2.1, avec ESP-IDF et libsodium).
