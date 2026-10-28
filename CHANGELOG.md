# Journal des modifications

Format inspiré de [Keep a Changelog](https://keepachangelog.com/fr/1.1.0/). Les versions suivent le [versionnage sémantique](https://semver.org/lang/fr/).

## Non publié

## 1.2.1 - 2026-10-28

### Firmware

- Mise à jour en ligne: la page enregistrée dans LittleFS est relue (taille et empreinte) avant d'installer le firmware. Une écriture incomplète passait inaperçue et laissait une page tronquée, bloquée sur « Connexion… » (vu en passant à 1.2.0; réparer avec `make uploadfs`). La place libre est vérifiée avant l'écriture et journalisée.
- Console plus lisible: plus d'erreur du framework pour un fichier absent pendant une mise à jour ou au démarrage, et seules les erreurs du framework sont affichées (ses messages se mélangeaient aux lignes de la boîte).

## 1.2.0 - 2026-10-28

### Firmware

- Mise à jour automatique (réglage `autoUpdate`, désactivé par défaut): une fois par jour, la boîte cherche une nouvelle version sur GitHub et l'installe, connectée au Wi-Fi de la maison et après une minute sans histoire ni carte posée, réveil compris. Heure par NTP, date de la dernière recherche gardée en NVS.

### Page web

- Réglages « Être informé des mises à jour de la TotoBox » (désactivé: la page ne contacte plus GitHub) et « Procéder automatiquement aux mises à jour lorsque possible », dans les réglages et dans une étape de l'assistant, juste avant le Wi-Fi.
- Une mise à jour lancée par la boîte (ou depuis un autre téléphone) ouvre la fenêtre de mise à jour avec son avancement, comme une mise à jour lancée à la main.

## 1.1.0 - 2026-10-28

### Firmware

- Mise à jour en ligne (`POST /api/update`): la boîte télécharge une version publiée sur GitHub (HTTPS, autorités racines de Mozilla), vérifie l'empreinte de la page et du firmware, les installe (partition OTA inactive) et redémarre, en gardant histoires, cartes et réglages. Seules les versions plus récentes du dépôt officiel sont acceptées.

### Page web

- À l'ouverture, puis une fois par jour, la page cherche une nouvelle version sur GitHub; bandeau « Nouvelle version de la boîte », nouveautés et installation en un clic avec l'avancement, aussi depuis les réglages.

### Développement

- Les Releases fournissent la page seule (`totobox-X.Y.Z-page.html.gz`) et, dans leurs notes, les empreintes lues par la page pour la mise à jour en ligne.

- Publication automatisée: `make release VERSION=X.Y.Z` (version, CHANGELOG, commit, étiquette), puis la Release GitHub est créée à l'arrivée de l'étiquette, avec les notes du CHANGELOG et les fichiers à télécharger: image complète à flasher sans PlatformIO, firmware et page web pour une mise à jour, fichiers de découpe.

## 1.0.1 - 2026-10-28

Corrections et tests; aucun changement de l'API ni des réglages.

### Firmware

- Règles des chemins de la bibliothèque (`library_paths`) et décisions de reprise de lecture (`playback_rules`) extraites dans des modules testés sur ordinateur (29 nouveaux cas).
- Le dossier `/content` lui-même ne peut plus être envoyé à la corbeille.
- Suivi des cartes (`card_flow`), volume (`volume_rules`), LEDs (`led_layout`) et reconnexion Wi-Fi (`retry_delay`) extraits et testés sur ordinateur (24 nouveaux cas).
- Carte remplacée par une autre en moins de 250 ms: l'histoire en cours est arrêtée et son signet enregistré avant de lire la nouvelle carte. Avant, le signet était perdu, et avec une carte maîtresse l'histoire continuait sans carte posée.

### Page web

- Fonctions pures de la page dans `web/lib.js`, testées par `make test-web` (`node --test`, 10 cas): échappement HTML, formats, noms réseau et de dossiers, retrait ID3, cartes qui suivent un fichier déplacé.

## 1.0.0 - 2026-10-28

Première publication.

### Firmware

- Lecture depuis la carte microSD déclenchée par carte NFC (PN532): fichier seul ou dossier, signet par carte à l'octet près, arrêt immédiat au retrait.
- Volume à la molette (KY-040, lue par interruption) et depuis la page, borné par des limites enregistrées.
- LEDs d'état (prête, lecture, carte inconnue, erreur, envoi de fichiers, jauge de volume).
- Interface web pour téléphone: assistant de première connexion, association des cartes, gestion des fichiers et corbeille, réglages Wi-Fi et point d'accès, sauvegarde et restauration des cartes sur la carte SD, remise à zéro.
- Verrouillage parental par cartes maîtresses et code de secours, appliqué côté firmware.
- Wi-Fi non bloquant, point d'accès de secours avec portail captif, nom mDNS réglable, changement de réseau sans redémarrage.
- Veille profonde et réveil par carte NFC ou molette.
- Carte SD surveillée en tâche de fond: absente, illisible (exFAT, formatable depuis la page), retrait à chaud avec reprise à la réinsertion.
- Protection contre les autres sites (rebinding DNS, CSRF) et secrets chiffrés entre la page et la boîte.
- Partition d'usine et script de provisionnement pour la production en série.
- Version du firmware et commit compilé dans la console, les réglages et `/api/status`.

### Développement

- Page web en sources séparées (`web/`), assemblée en un fichier pour la boîte; faux serveur de la boîte (`make mock`) pour la développer sans matériel.
- Tests sur ordinateur (firmware, scripts, outils du boîtier), CI GitHub Actions, licences au format REUSE.

### Boîtier

- Deux versions en contreplaqué de 3 mm: faite à la main (gabarit A4 à l'échelle 1:1) et à créneaux pour la découpe laser (DXF).
- Features Onshape paramétrables des composants (carte HermitX, antenne, haut-parleur, encodeur, entretoises, bande guide-cartes, assemblage à créneaux).
