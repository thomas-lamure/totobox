# API embarquée

La page web ([web/](../web)) ne parle à la boîte que par cette API JSON locale. Elle reste simple et utilisable depuis un script (`curl`). Le faux serveur de développement ([scripts/mock-server.mjs](../scripts/mock-server.mjs), voir [development.md](development.md#page-web)) l'imite sans matériel.

Erreurs communes: `403 locked` (réglages verrouillés), `503 busy_playing` (accès à la carte SD refusé pendant la lecture), `403 forbidden_host`, `403 forbidden_origin` et `415 json_required` (voir « Protection contre les autres sites »).

| Route | Verrouillée | En lecture | Rôle |
|---|---|---|---|
| `GET /` | oui | oui | page web |
| `GET /api/status` | oui | oui | état, lecture en cours, volume, verrouillage, SD |
| `POST /api/volume` | oui | oui | `{volume}` borné aux limites |
| `POST /api/unlock` | oui | oui | `{pin}`; 5 erreurs = 60 s de blocage |
| `POST /api/lock` | oui | oui | reverrouille |
| `GET /api/config` | non | oui | réglages, bindings, cartes maîtresses |
| `POST /api/config` | non | oui | réglages (mot de passe absent = inchangé), `hostname`; validés, réseau appliqué sans redémarrage |
| `POST /api/bindings` | non | oui | remplace la liste des cartes |
| `POST /api/lock/setup` | non | oui | `{enabled:true, cardUid, cardLabel, pin}` ou `{enabled:false}` |
| `POST /api/lock/pin` | non | oui | `{pin}` |
| `POST /api/lock/cards` | non | oui | `{action:"add"/"remove", cardUid, cardLabel}` |
| `GET /api/library` | non | non | arborescence de `/content`, espace SD |
| `POST /api/upload` | non | non | multipart, tags ID3 retirés à l'écriture |
| `POST /api/folder` | non | non | crée un dossier |
| `POST /api/rename` | non | non | renomme fichier ou dossier |
| `POST /api/move` | non | non | `{path, targetDir}`: déplace fichier ou dossier, renvoie `movedPath` |
| `POST /api/delete` | non | non | déplace vers `/content/.trash` |
| `GET /api/trash` | non | non | contenu de la corbeille |
| `POST /api/restore` | non | non | restaure depuis la corbeille |
| `POST /api/trash/purge` | non | non | vide la corbeille |
| `GET /api/cards/backup` | non | non | sauvegarde présente sur la SD: `exists`, `count`, `savedAt` |
| `POST /api/cards/backup` | non | non | `{savedAt}`: sauvegarde les cartes dans `/totobox-cartes.json` (remplace la précédente) |
| `POST /api/cards/restore` | non | non | remplace les cartes par celles de la sauvegarde (cartes maîtresses ignorées) |
| `POST /api/setup/complete` | non | oui | assistant de première connexion terminé (ou remis à plus tard) |
| `POST /api/factory-reset` | non | non | `{confirm:"EFFACER", savedAt}`: sauvegarde les cartes sur la SD, efface la configuration et redémarre; renvoie `apSsid`, `backupSaved` |
| `POST /api/sd/format` | non | — | `{confirm:"FORMATER"}`: formate en FAT32 une carte illisible (refusé si elle est lisible) |

`GET /api/status` renvoie notamment:

- `firmwareVersion` (version publiée, voir [CHANGELOG.md](../CHANGELOG.md)) et `firmwareBuild` (commit compilé, `-dirty` si modifié);
- `state`, `audioPlaying`, `activeCardUid`, `activeCardLabel`, `activeMediaPath`, `activeTrackIndex`, `activeTrackCount`, `activePositionMs`;
- `currentVolume`, `volumeMin`, `volumeMax`;
- `lockEnabled`, `locked`, `lastUnknownCardUid` (vide si verrouillée);
- `sdState` (`absent`, `unreadable`: présente mais pas en FAT32, `ready`) et l'espace SD;
- `wifiMode`, `wifiIp`, `hostname`, `networkAttempt` (résultat du dernier changement de Wi-Fi);
- `setupDone` (faux: la page ouvre l'assistant de première connexion);
- `uiVersion` (version de la page dans LittleFS: une page plus ancienne propose de se recharger);
- `boxKey` et `challenge` (canal chiffré des secrets, voir plus bas).

Les créations et renommages refusent les noms réservés ou ambigus comme `.`, `..`, `.trash`, `trash` et `content`.

## Protection contre les autres sites

Une page web malveillante ouverte sur un appareil du réseau local ne peut pas piloter la boîte ([src/request_guard.h](../src/request_guard.h)):

- toute route `/api` refuse les requêtes adressées à un autre nom que la boîte (son nom `.local`, son IP ou 192.168.4.1): protection contre le rebinding DNS (`403 forbidden_host`);
- toute modification venue d'une autre origine (en-têtes `Origin`, `Sec-Fetch-Site`) est refusée (`403 forbidden_origin`), de même qu'un corps qui n'est pas en `application/json` (`415 json_required`).

Les scripts restent possibles: `curl -H "Content-Type: application/json" -d '{"volume":40}' http://totobox.local/api/volume`.

## Secrets chiffrés

L'interface est servie en HTTP. Les mots de passe Wi-Fi (maison et point d'accès) et le code de secours ne circulent pas en clair: la page les chiffre avec la clé publique de la boîte (`boxKey`, X25519 + XSalsa20-Poly1305, `nacl.box`; tweetnacl-js embarqué côté page, libsodium côté boîte, [src/secret_channel.h](../src/secret_channel.h)). La paire de clés est tirée au premier démarrage et effacée par la remise à zéro. Un défi aléatoire (`challenge`), renouvelé après chaque utilisation, empêche de rejouer une requête enregistrée.

Cela protège d'une écoute passive du réseau, pas de quelqu'un qui s'interpose et remplace la clé ou la page. Les requêtes en clair restent acceptées (scripts).
