# Journal des modifications

Format inspiré de [Keep a Changelog](https://keepachangelog.com/fr/1.1.0/). Les versions suivent le [versionnage sémantique](https://semver.org/lang/fr/).

## Non publié

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
