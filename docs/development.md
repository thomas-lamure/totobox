# Développement

## Outils

- [PlatformIO](https://platformio.org/) (extension VS Code « PlatformIO IDE » ou `pip install platformio`) pour le firmware.
- Python 3, bibliothèque standard seulement, pour les scripts et les outils du boîtier.
- Node.js 18 ou plus (facultatif), sans dépendance, pour le faux serveur de la page web.
- `make` (facultatif): le [Makefile](../Makefile) regroupe les commandes ci-dessous, les mêmes que la CI.

## Compiler et téléverser

```bash
make firmware      # pio run
make upload        # pio run -t upload
make uploadfs      # pio run -t uploadfs: page web, à refaire après chaque modification de web/
make monitor       # pio device monitor: console série (115200 bauds)
```

La console affiche en première ligne la version du firmware et le commit compilé (`TotoBox 1.0.0 (0e098db)`, `-dirty` si des fichiers étaient modifiés), aussi visibles dans Réglages > « Firmware » et dans `/api/status`. La version suit [CHANGELOG.md](../CHANGELOG.md): la monter dans [include/version.h](../include/version.h) à chaque publication.

## Page web

Les sources sont dans [web/](../web): `index.html` (structure), `style.css`, `app.js` et `vendor/nacl.min.js` (tweetnacl-js, repris tel quel). À la construction de l'image LittleFS, [scripts/build_webfs.py](../scripts/build_webfs.py) les assemble en un seul fichier ([scripts/webpage.py](../scripts/webpage.py): feuille de style et scripts intégrés), le compresse en gzip (~175 Ko → ~46 Ko) et y inscrit sa version (empreinte de la page): la boîte ne sert qu'un fichier, sans dépendance à Internet.

Pour travailler sur la page sans la boîte:

```bash
make mock          # node scripts/mock-server.mjs: http://localhost:8080
```

Le faux serveur ([scripts/mock-server.mjs](../scripts/mock-server.mjs)) sert les sources de `web/` telles quelles (recharger la page suffit après une modification) et imite l'API de la boîte en mémoire: bibliothèque d'exemple, cartes, verrouillage avec secrets chiffrés, envoi de fichiers, corbeille, sauvegarde. La page http://localhost:8080/mock simule la boîte: poser une carte connue, inconnue ou maîtresse, la retirer, carte SD prête, illisible ou absente. Il suit [src/web_admin.cpp](../src/web_admin.cpp): une route modifiée l'est aussi dans le faux serveur et dans [api.md](api.md).

Au premier démarrage d'une boîte sans données d'usine, le mot de passe du point d'accès est tiré au hasard et affiché dans la console série tant qu'il n'a pas été personnalisé (appuyer sur le bouton RESET après avoir ouvert la console).

## Tests

```bash
make test          # tout
make test-native   # pio test -e native: modules du firmware sans matériel (Unity)
make test-scripts  # scripts du dépôt: assemblage de la page (unittest)
make test-cad      # outils du boîtier (unittest)
make check-cad     # DXF et PDF versionnés à jour avec leurs contours
```

Les modules sans matériel (nettoyage ID3, verrouillage, sérialisation de la configuration et du catalogue, noms réseau, données d'usine, sauvegarde des cartes, protection des requêtes, décodage de l'encodeur) sont compilés pour l'ordinateur: [test/support/](../test/support) fournit des versions simulées d'Arduino (`String`), de la NVS (`Preferences`), de l'aléa et du SHA-256 de mbedtls. Une nouvelle logique sans matériel doit y être testée; un module qui y entre est ajouté à `build_src_filter` dans [platformio.ini](../platformio.ini).

Le reste (audio, NFC, LEDs, réseau, veille) se valide sur la boîte. Scénarios minimum avant une contribution qui y touche: carte connue, carte inconnue, retrait de carte, volume (molette et page), verrouillage et déverrouillage, retrait et réinsertion de la carte SD.

## Arborescence

```text
.
├── src/                  # firmware (services, voir docs/architecture.md)
├── include/
│   ├── totobox_config.h  # broches, délais, constantes
│   └── version.h         # version du firmware
├── web/                  # interface web: index.html, style.css, app.js, vendor/ (assemblés pour LittleFS)
├── test/                 # tests Unity sur ordinateur (pio test -e native)
│   └── support/          # Arduino, NVS, aléa et SHA-256 simulés
├── scripts/
│   ├── build_version.py  # commit compilé, pour la version du firmware (appelé par PlatformIO)
│   ├── build_webfs.py    # image LittleFS: page assemblée (webpage.py), compressée et versionnée
│   ├── mock-server.mjs   # faux serveur de la boîte pour développer la page (make mock)
│   └── provision.py      # production: mot de passe d'usine et étiquettes
├── hardware/             # boîtier et câblage (CC BY-SA 4.0)
│   ├── onshape/          # Features Onshape (FeatureScript) des composants et du boîtier
│   ├── laser/            # contours, DXF de découpe et gabarits PDF A4
│   └── tools/            # générateurs DXF, PDF et modèle de la carte (Python), et leurs tests
├── docs/                 # documentation (utilisation, API, architecture, production) et logo
├── LICENSES/             # textes des licences (REUSE.toml dit laquelle s'applique à quel fichier)
├── partitions.csv        # table de partitions (dont la partition d'usine mfg)
├── platformio.ini
├── Makefile
└── AGENTS.md             # règles de code et pièges connus (pour les humains comme pour les agents IA)
```

## Dépendances

Les versions sont figées dans [platformio.ini](../platformio.ini): plateforme `espressif32` 7.1.3, ArduinoJson 7.4.3, Adafruit PN532 1.3.4, ESP32-audioI2S 2.3.0. Le firmware dépend de détails précis d'ESP32-audioI2S (position de reprise en octets, voir [AGENTS.md](../AGENTS.md)): monter une version demande de retester la lecture, la reprise et le retrait de carte SD sur la boîte.

## Règles de code

Elles sont dans [AGENTS.md](../AGENTS.md): architecture par services, boucles non bloquantes, tâche audio dédiée et son verrou, routes web protégées (`onApi()`, `rejectIfLocked()`, `rejectIfBusy()`), persistance par `ConfigStore`, logs par `logLine()`, et la liste des pièges matériels et de bibliothèques déjà rencontrés. À lire avant de modifier le firmware.

Code, commentaires, interface et documentation sont en français.
