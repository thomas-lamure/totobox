# Problèmes connus

Symptômes déjà rencontrés sur une boîte, leur cause et quoi faire. Les pièges propres au code (bibliothèques, ESP32) sont dans [AGENTS.md](../AGENTS.md).

## Matériel et montage

### La molette monte le volume au maximum quand aucune carte n'est posée

- **Symptôme**: sans carte posée, tourner la molette dans un sens ou dans l'autre monte le volume jusqu'au maximum; avec une carte dans la fente, tout est normal. Dans la console, chaque geste compte des dizaines de crans (`[knob] +27 crans`), presque toujours positifs.
- **Cause**: les fils de l'encodeur passent sur l'antenne NFC. Sans carte, le lecteur garde son champ radio allumé pendant toute la recherche (toutes les 100 ms), qui induit de faux fronts dans les fils CLK et DT pendant qu'ils changent d'état. Une carte posée répond tout de suite et le champ se coupe presque aussitôt.
- **Remède**: faire passer les fils de l'encodeur loin de l'antenne, le long de la paroi, et les fixer pour qu'ils n'y reviennent pas en refermant la boîte (voir [hardware/electronics.md](../hardware/electronics.md#encodeur-ky-040-retenu)). Depuis 1.3.1, la boîte suspend aussi la recherche de carte pendant qu'on tourne la molette sans carte posée, ce qui masque le défaut sans le corriger.

## Carte microSD

### Carte SD « illisible »

- **Cause**: seules les cartes en FAT32 (ou FAT16) sont lues; les cartes de 64 Go et plus sont livrées en exFAT.
- **Remède**: formater en FAT32, depuis la page de la boîte (« Formater ») ou un ordinateur (voir [usage.md](usage.md#carte-microsd)).

## Réseau

### `totobox.local` ne répond plus après quelques minutes

- **Cause**: certaines box cessent de relayer les annonces mDNS; l'adresse IP, elle, répond toujours.
- **Remède**: aucun à faire en général: la boîte se réannonce toutes les minutes, et la page ouverte par le nom bascule d'elle-même sur l'adresse IP. Sinon, ouvrir la page par son adresse IP (affichée dans Réglages > Boîte).

### Envoi de fichiers impossible depuis la page ouverte par le portail captif

- **Cause**: le navigateur intégré des portails captifs (Android) n'ouvre pas de sélecteur de fichiers.
- **Remède**: la page le détecte et propose de l'ouvrir dans Chrome ou Safari.

## Mises à jour

### Une version installée en ligne démarre mais ne fonctionne plus

- **Cause**: le framework (Arduino-ESP32 2.0.x) n'active pas le retour automatique à l'ancien firmware.
- **Remède**: réinstaller par USB (`totobox-X.Y.Z-firmware.bin` en 0x10000, voir les notes de la Release).

## Home Assistant

### Connexion chiffrée au broker impossible

- **Cause**: `mqtts://` (TLS) n'est pas encore pris en charge.
- **Remède**: broker en MQTT simple sur le réseau local (port 1883), voir [home-assistant-mqtt.md](home-assistant-mqtt.md).
