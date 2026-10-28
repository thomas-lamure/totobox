# Journal des modifications

Format inspiré de [Keep a Changelog](https://keepachangelog.com/fr/1.1.0/). Les versions suivent le [versionnage sémantique](https://semver.org/lang/fr/).

## Non publié

## 1.0.0

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
