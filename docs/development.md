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

La console n'affiche que les lignes de la boîte (`logLine`, préfixées par le service) et les erreurs du framework: `CORE_DEBUG_LEVEL=1` dans [platformio.ini](../platformio.ini), les messages d'information et avertissements du framework, écrits sans verrou, se mélangeaient aux autres lignes. Le monter à 3 ou 4 pour déboguer le framework (Wi-Fi, TLS…).

La console affiche en première ligne la version du firmware et le commit compilé (`TotoBox 1.0.0 (0e098db)`, `-dirty` si des fichiers étaient modifiés), aussi visibles dans Réglages > « Firmware » et dans `/api/status`. La version suit le [versionnage sémantique](https://semver.org/lang/fr/) et [CHANGELOG.md](../CHANGELOG.md).

## Publier une version

Au fil des modifications, chaque changement visible ajoute une ligne à la section « Non publié » de [CHANGELOG.md](../CHANGELOG.md). Pour publier:

1. choisir le numéro: correctif (1.0.**1**: corrections, tests, remaniement sans effet visible), mineure (1.**1**.0: nouvelle fonction, nouvelle route ou nouveau champ compatibles), majeure (**2**.0.0: API, format des réglages ou des cartes, câblage incompatibles);
2. sur `main`, dépôt propre:

   ```bash
   make release VERSION=1.1.0     # python3 scripts/release.py prepare 1.1.0 (--dry-run pour voir sans rien changer)
   git push --follow-tags
   ```

   [scripts/release.py](../scripts/release.py) vérifie que le numéro est plus grand que l'actuel et que « Non publié » n'est pas vide, met le numéro dans [include/version.h](../include/version.h), renomme la section en `## 1.1.0 - date` (et en rouvre une vide), commite « Release 1.1.0 » et étiquette `v1.1.0`;
3. l'étiquette déclenche [.github/workflows/release.yml](../.github/workflows/release.yml): vérification de la version, tests, compilation, puis Release GitHub avec les notes du CHANGELOG et les fichiers de `make dist`:
   - `totobox-X.Y.Z-complet.bin`: image unique à écrire en 0x0 (première installation sans PlatformIO; efface les réglages et les cartes, pas la carte SD ni la partition d'usine), produite par la cible PlatformIO `merged` ([scripts/build_merged.py](../scripts/build_merged.py));
   - `totobox-X.Y.Z-firmware.bin` (0x10000) et `totobox-X.Y.Z-page-web.bin` (0x3D0000): mise à jour en gardant les réglages;
   - `totobox-X.Y.Z-page.html.gz`: la page seule, pour la mise à jour en ligne; les notes de version se terminent par un commentaire invisible (`scripts/release.py update-block`) qui donne à la page les empreintes à faire vérifier par la boîte (voir [api.md](api.md#mise-à-jour-en-ligne));
   - `totobox-X.Y.Z-decoupe.zip`: DXF et gabarits PDF du boîtier; `SHA256SUMS.txt`.

Une version publiée ainsi est proposée aux boîtes par leur page (bandeau « Nouvelle version de la boîte »), qui s'installe en ligne. Les versions antérieures à la mise à jour en ligne (1.0.x) s'installent une fois par USB.

### Autorités racines

La boîte vérifie le certificat de GitHub avec les autorités racines de Mozilla, intégrées au firmware ([src/certs/x509_crt_bundle.bin](../src/certs/x509_crt_bundle.bin), MPL-2.0). À régénérer de temps en temps (une autorité retirée ou ajoutée par Mozilla, un changement d'autorité chez GitHub), puis publier une version:

```bash
curl -o cacert.pem https://curl.se/ca/cacert.pem
python3 scripts/gen_ca_bundle.py cacert.pem src/certs/x509_crt_bundle.bin
```

## Page web

Les sources sont dans [web/](../web): `index.html` (structure), `style.css`, `lib.js` (fonctions pures, testées par `make test-web`), `app.js` (le reste de la page) et `vendor/nacl.min.js` (tweetnacl-js, repris tel quel). Une fonction sans DOM ni réseau va dans `lib.js`, avec son test dans [scripts/test_web_lib.mjs](../scripts/test_web_lib.mjs). À la construction de l'image LittleFS, [scripts/build_webfs.py](../scripts/build_webfs.py) les assemble en un seul fichier ([scripts/webpage.py](../scripts/webpage.py): feuille de style et scripts intégrés), le compresse en gzip (~175 Ko → ~46 Ko) et y inscrit sa version (empreinte de la page): la boîte ne sert qu'un fichier, sans dépendance à Internet.

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
make test-scripts  # scripts du dépôt: assemblage de la page, publication des versions (unittest)
make test-web      # fonctions pures de la page, web/lib.js (node --test)
make test-cad      # outils du boîtier (unittest)
make check-cad     # DXF et PDF versionnés à jour avec leurs contours
make dist          # fichiers d'une Release dans dist/ (voir « Publier une version »)
```

Les modules sans matériel (nettoyage ID3, verrouillage, sérialisation de la configuration et du catalogue, noms réseau, données d'usine, sauvegarde des cartes, protection des requêtes, décodage de l'encodeur, chemins de la bibliothèque, décisions de reprise de lecture, suivi des cartes et signets, volume et jauge, ordre des LEDs, délai de reconnexion Wi-Fi) sont compilés pour l'ordinateur: [test/support/](../test/support) fournit des versions simulées d'Arduino (`String`), de la NVS (`Preferences`), de l'aléa et du SHA-256 de mbedtls. Une nouvelle logique sans matériel doit y être testée: la placer dans un petit module sans dépendance matérielle (comme [src/library_paths.h](../src/library_paths.h) ou [src/playback_rules.h](../src/playback_rules.h), extraits de `web_admin.cpp` et `audio_service.cpp`), l'ajouter à `build_src_filter` dans [platformio.ini](../platformio.ini) et compléter `test/support/` si elle utilise une API Arduino qui n'y est pas encore.

Le reste (audio, NFC, LEDs, réseau, veille) se valide sur la boîte. Scénarios minimum avant une contribution qui y touche: carte connue, carte inconnue, retrait de carte, volume (molette et page), verrouillage et déverrouillage, retrait et réinsertion de la carte SD.

## Arborescence

```text
.
├── src/                  # firmware (services, voir docs/architecture.md)
├── include/
│   ├── totobox_config.h  # broches, délais, constantes
│   └── version.h         # version du firmware
├── web/                  # interface web: index.html, style.css, lib.js, app.js, vendor/ (assemblés pour LittleFS)
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
│   └── tools/            # générateurs DXF, PDF et modèle de la carte (Python), et leurs tests; render/: rendus three.js
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
