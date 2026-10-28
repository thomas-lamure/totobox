# TotoBox Agent Guide

## Mission

Le dépôt contient le firmware d'une boîte à histoires enfant basée sur ESP32-S3. Toute modification doit préserver trois priorités: sécurité d'usage, comportement déterministe hors-ligne et lisibilité des états via les LEDs.

## Contraintes produit

- V1 est autonome et lit depuis la carte SD.
- Le retrait de carte NFC doit arrêter ou mettre en pause immédiatement.
- Le bookmark est obligatoire pour les livres audio.
- Les limites de volume sont bornées par logiciel et persistées en NVS.
- La V2 réseau ne doit pas casser la V1 locale.
- Un enfant ne doit jamais avoir besoin de l'interface web pour écouter une histoire.
- Quand les réglages sont verrouillés, la boîte ne doit exposer que la lecture en cours et le volume; le verrouillage est appliqué côté firmware, pas seulement dans la page.

## Conventions de travail

- Garder une architecture par services; éviter de repousser toute la logique dans main.cpp.
- Préférer des boucles non bloquantes et des délais courts.
- Toute évolution NFC ou audio doit conserver les pins matériels de [include/totobox_config.h](include/totobox_config.h).
- Toute nouvelle persistance doit passer par ConfigStore, et rester dans la NVS: la remise à zéro (`factory_reset`) l'efface en entier. La sauvegarde des cartes sur la SD (`card_backup`, `/totobox-cartes.json`) réutilise le format JSON de `ConfigStore::writeBindings/readBindings`: tout nouveau champ de `CardBinding` y passe automatiquement. Seule exception: la partition `mfg` (données d'usine), écrite par `scripts/provision.py` sur la ligne de production et seulement lue par la boîte ([src/factory_data.h](src/factory_data.h)).
- Les endpoints web doivent rester simples, JSON, et utilisables depuis un téléphone.
- Ne pas introduire de secret Wi-Fi en dur dans le dépôt.
- Logs série: toujours `logLine(tag, format, ...)` ([src/logger.h](src/logger.h)), jamais `Serial.print` directement (deux tâches écrivent en parallèle).
- Code et commentaires en français, comme le reste du dépôt.
- Versions: tout changement visible ajoute une ligne à « Non publié » dans [CHANGELOG.md](CHANGELOG.md). Une version se publie avec `make release VERSION=X.Y.Z` puis `git push --follow-tags` (numérotation et étapes: [docs/development.md](docs/development.md#publier-une-version)); ne jamais modifier `kFirmwareVersion` à la main.
- Licences (format REUSE, [REUSE.toml](REUSE.toml)): code sous GPL-3.0-or-later, `hardware/`, `docs/` et les `.md` sous CC BY-SA 4.0. Tout nouveau fichier de code commence par l'en-tête SPDX de la GPL, en commentaire, copié d'un fichier voisin; un code repris d'ailleurs garde sa licence, déclarée dans `REUSE.toml` et `LICENSES/`.
- Documentation: utilisation dans [docs/usage.md](docs/usage.md), API dans [docs/api.md](docs/api.md), boîtier dans [hardware/](hardware/README.md). Une fonction visible par l'utilisateur ou une route modifiée met à jour ces fichiers et [CHANGELOG.md](CHANGELOG.md).

## Architecture d'exécution (à respecter)

- Le décodage audio tourne dans une tâche FreeRTOS dédiée sur le cœur 1, priorité 3 ([src/audio_service.cpp](src/audio_service.cpp)), au-dessus de `loop()` (même cœur, priorité 1) qui ne doit jamais appeler `Audio::loop()`. Ne pas la remettre sur le cœur 0: la pile Wi-Fi y prend la main par rafales et coupe le son pendant une connexion.
- Toutes les méthodes publiques d'`AudioService` prennent un mutex récursif partagé avec la tâche audio. Toute nouvelle méthode qui touche `audio_` ou la playlist doit faire de même.
- `WebAdmin::loop()` tourne aussi pendant la lecture. Les routes qui accèdent à la carte SD (bibliothèque, upload, renommage, corbeille…) doivent appeler `rejectIfBusy()` et répondre `503 busy_playing` pendant la lecture. Seules les routes légères (statut, réglages, volume, associations, verrouillage) restent servies.
- Tout chemin ou nom reçu par une route web passe par `library_paths` (normalisation dans `/content`, noms nettoyés, corbeille) avant d'accéder à la carte SD; les décisions de reprise de lecture sont dans `playback_rules`, le suivi des cartes (posée, retirée, remplacée, signets, retour de la carte SD) dans `card_flow`.
- Toute route `/api` s'enregistre avec `onApi()` (jamais `server_.on()` directement): `request_guard` refuse les requêtes adressées à un autre nom que la boîte (rebinding DNS, 403 `forbidden_host`) et, pour un POST, celles venues d'une autre origine (CSRF, 403 `forbidden_origin`) ou dont le corps n'est pas `application/json` (415 `json_required`). Une route hors `onApi()` doit appeler `rejectIfForeign()` elle-même (voir l'envoi de fichiers).
- Les routes de gestion doivent appeler `rejectIfLocked()` (403 `locked`) avant tout le reste.
- Éviter les écritures NVS en rafale: une écriture flash fige le cache des deux cœurs. Le volume est enregistré 1,5 s après le dernier changement (`kVolumeSaveDelayMs`).

## Pièges connus

- ESP32-audioI2S 2.3.0: le 3e paramètre de `connecttoFS()` est une position en OCTETS, pas des secondes. Le signet enregistre donc l'octet en cours (`getFilePos() - inBufferFilled()`) et la reprise le passe à `connecttoFS()`, que la lib recale sur une trame (MP3, WAV, M4A, FLAC, AAC). `setAudioPlayPosition(sec)` ne sert que de secours pour les anciens signets sans octet: il convertit les secondes en octets avec le débit moyen (faux en MP3 à débit variable, refusé en M4A). Après une reprise par octet, l'horloge de la lib repart de 0: `AudioService` compte la position depuis le signet.
- La lib ne décode une trame MP3 (~26 ms) qu'un appel à `Audio::loop()` sur trois: tout blocage de quelques dizaines de ms fait sauter le son, d'où la tâche dédiée.
- Molette: pendant la lecture, la tâche audio (même cœur, priorité plus haute) espace les tours de `loop()` de plusieurs ms; une lecture de l'encodeur en scrutation y perdait des crans. CLK et DT sont donc lus par interruption (`VolumeKnob::handleEdge()`, en IRAM, registre GPIO lu directement), `loop()` ne relève que les crans. Toute nouvelle entrée rapide (impulsions, rotation) doit suivre le même principe. Volume: `kVolumeStepPercent` (5 %) par cran, la lib audio n'ayant que 22 niveaux.
- La bande LED est montée à l'envers: utiliser `physicalIndex()` et `kStripReversed` ([src/led_controller.cpp](src/led_controller.cpp)) pour toute nouvelle animation orientée.
- Envoyer toute la bande LED en une trame RMT (`LedController::show()`); un `neopixelWrite()` par LED ne pilote que la première.
- Le navigateur intégré des portails captifs (WebView Android) n'ouvre pas de sélecteur de fichiers: l'UI le détecte et renvoie vers Chrome/Safari.
- L'UI web est servie en HTTP, et `crypto.subtle` n'existe pas dans une page HTTP. Les secrets (mots de passe Wi-Fi, code de secours) partent chiffrés: `apiWithSecrets()` dans la page (tweetnacl embarqué, `nacl.box` vers `status.boxKey` avec le défi `status.challenge`), `rejectIfSealedInvalid()` côté boîte ([src/secret_channel.h](src/secret_channel.h), libsodium). Tout nouveau champ secret passe par ces deux fonctions. Protège d'une écoute passive seulement, pas d'un MITM.
- Veille profonde: le réveil repasse par `setup()`. Toute nouvelle source d'activité doit appeler `PowerManager::noteActivity()` (ou mettre à jour `runtime.lastActivityMs`), et toute broche qui ne doit pas flotter pendant le sommeil va dans `kHeldPins` (`power_manager.cpp`).
- PN532: une recherche `InListPassiveTarget` avec un nombre d'essais infini (MxRtyPassiveActivation = 0xFF, valeur par défaut) ne s'arrête jamais sans carte, et le PN532 ignore alors toute nouvelle commande de détection (sans ACK ni erreur). `NFCService` règle donc un nombre d'essais fini (`kNfcSearchRetries`) et sait interrompre une commande en cours (trame ACK envoyée par l'hôte).
- Lib Adafruit PN532: `setPassiveActivationRetries()` et `SAMConfig()` ne lisent pas (ou trop tôt) leur réponse, ce qui désynchronise l'échange: vider ensuite la réponse (`drainPending()`). `readDetectedPassiveTargetID()` copie autant d'octets que la trame l'annonce: tampon de 256 octets.
- Veille: l'ACK d'`InAutoPoll` n'est jamais lu correctement, mais la commande est bien exécutée (validé sur la carte: une carte posée réveille la boîte). Le PN532 ignore alors les commandes radio suivantes tout en répondant aux autres. `armWakeOnCard()` arme donc le réveil dès que l'IRQ est haute, sans exiger d'ACK.
- Wi-Fi: le framework Arduino ne retente qu'une fois par coupure et abandonne sur AUTH_FAIL, que la box renvoie souvent à la première tentative. `NetworkService::tickStation()` gère donc lui-même les reconnexions (10 s puis jusqu'à 60 s entre deux essais).
- mDNS: sur certaines box, `<nom>.local` cesse d'être résolu ~2 min après l'ouverture de la page (durée de vie de 120 s, puis requêtes multicast non relayées) alors que l'IP répond. La boîte se réannonce toutes les 60 s (`kMdnsAnnounceMs`) pour garder le nom en cache sur les appareils, et la page, ouverte par le nom, bascule d'elle-même sur l'IP connue si ça ne suffit pas (`fallbackToIpIfNameFails()`).
- Carte SD: seul le FAT16/FAT32 est lu (exFAT désactivé dans le framework précompilé, or les cartes de 64 Go et plus sont livrées en exFAT). `SD.begin()` ne distingue pas « absente » de « illisible »: `AudioService::cardResponds()` rejoue sa séquence (`sdcard_init`, `disk_initialize`, `sdcard_type`) avant `SD.begin()` pour le savoir (`SdState`). Non montée, une tâche de surveillance d'`AudioService` (`sdwatch`, même cœur et même priorité que `loop()`) réessaie toutes les 2 s (`kSdRetryMs`): sans carte, une initialisation SPI bloque de 0,3 à plus de 2 s (attentes de la lib SD), ce qui figeait le serveur web quand l'essai se faisait dans `loop()`. Jamais d'accès à une carte non montée dans `loop()` ni sous le verrou audio; `loop()` ne relève que `audio.sdState()`. Illisible, seule la présence est vérifiée (montage retenté toutes les 30 s). Le formatage (`AudioService::formatSd()`, `POST /api/sd/format`) n'est fait que sur demande explicite et jamais sur une carte lisible. Montée, la tâche lit le secteur 0 toutes les 2 s (`SD.readRAW`, ~1 ms) pour détecter un retrait à chaud, et la tâche audio fait de même à chaque fin de piste: la lib arrête la piste quand la carte ne répond plus, ce qui passerait sinon pour la fin de l'histoire et remettrait le signet à zéro. Au retrait, `AudioService::handleSdLost()` arrête la lecture en gardant la dernière position valide, relevée toutes les 250 ms (après le retrait, la lib tourne encore quelques secondes et `getFilePos()` renvoie -1) (`consumeSdLostPlayback()`, enregistrée dans le signet par `loop()`), et la lecture reprend à la réinsertion si la carte NFC est toujours posée. Le démontage (`SD.end()`) est fait au montage suivant, dans la tâche de surveillance.
- Mise à jour en ligne: GitHub n'envoie pas d'en-têtes CORS sur les fichiers des Releases (seulement sur l'API): la page trouve la version, la boîte télécharge (`UpdateService`, tâche du cœur 0, 12 Ko de pile pour TLS). Ne jamais écrire la page en place ni changer de partition de démarrage avant que les deux fichiers soient reçus et vérifiés (SHA-256 des notes de version); `update_rules` n'accepte que les Releases de `kUpdateRepo` (`totobox_config.h`, à changer dans un fork) et une version plus récente. Mode automatique (`autoUpdate`): la boîte cherche elle-même une fois par jour (`lastUpdateCheck` en NVS, heure NTP: pas d'horloge à pile), seulement connectée au Wi-Fi de la maison et après une minute sans lecture ni carte posée, réveil compris (`update_rules::shouldAutoCheck`); une carte posée pendant la recherche reporte l'installation (`UpdateService::setHold`). Le framework (Arduino-ESP32 2.0.x) n'active pas le retour automatique à l'ancien firmware: un firmware qui démarre mais ne fonctionne plus se répare par USB. Certificats: `src/certs/x509_crt_bundle.bin` (`scripts/gen_ca_bundle.py`).
- Les broches de réveil (GPIO11, GPIO21) restent en mode RTC après le sommeil: `PowerManager::begin()` les rend au GPIO numérique, à appeler avant d'initialiser l'encodeur et le NFC.

## Hypothèses matérielles retenues

- Encodeur KY-040: CLK GPIO9, DT GPIO10, SW GPIO11.
- GPIO47: pont de remise à zéro d'atelier (relié à la masse au démarrage). Elle contourne le verrouillage: elle doit rester inaccessible sans démontage (ni molette, ni bouton externe).
- GPIO 45, 46, 48 restent réservés pour extensions futures (45 et 46 sont des broches de strapping).
- Aucun capteur batterie n'est présent en V1; ne pas simuler une jauge réelle sans matériel dédié.
- CPU à 240 MHz ([platformio.ini](platformio.ini)) pour la marge de décodage.

## Ordre de priorité pour les prochaines tâches

1. Implémenter le pont Home Assistant pour la V2.
2. Étendre les tests sur ordinateur à toute nouvelle logique sans matériel: firmware (`pio test -e native`, dans un petit module sans dépendance matérielle comme `card_flow` ou `playback_rules`), page web (`web/lib.js`, `make test-web`). `main.cpp` et les services n'exécutent que les décisions de ces modules.

## Validation attendue

- Compiler avec PlatformIO après toute modification C++ (`make firmware`), et construire l'image LittleFS après toute modification de `web/` (`make webfs`). Les versions de la plateforme et des bibliothèques sont figées dans `platformio.ini`: ne les monter qu'avec un test sur la boîte.
- Lancer les tests sur ordinateur après toute modification de `id3_stripper`, `access_lock`, `config_store`, `network_names`, `factory_data`, `card_backup`, `request_guard`, `encoder_decoder`, `library_paths`, `playback_rules`, `card_flow`, `update_rules`, `volume_rules`, `led_layout`, `retry_delay` ou `app_types.h` (`pio test -e native`), et de `web/lib.js` (`make test-web`). `test/support/` remplace Arduino (`String`), la NVS, l'aléa et mbedtls: les compléter si un module testé utilise une nouvelle API.
- Après toute modification de `hardware/tools/` ou des contours de `hardware/laser/`: `make test-cad`, puis `make cad` pour régénérer les DXF et les PDF des deux versions (la CI vérifie qu'ils sont à jour). Après une modification des cotes de la carte: `python3 hardware/tools/hermitx_board.py`.
- Vérifier les endpoints HTTP modifiés, y compris en état verrouillé et pendant la lecture.
- Tester au minimum les scénarios: carte connue, carte inconnue, retrait de carte, changement de volume (molette et web), verrouillage/déverrouillage.
- L'UI se teste sans la boîte avec le faux serveur `make mock` ([scripts/mock-server.mjs](scripts/mock-server.mjs), page de simulation `/mock`: cartes, carte SD, lecture). Une route ajoutée ou modifiée dans `web_admin.cpp` l'est aussi dans le faux serveur et dans [docs/api.md](docs/api.md).
