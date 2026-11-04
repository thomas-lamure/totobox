# Home Assistant (conception)

Document de travail pour la première étape de la V2: la boîte se montre dans Home Assistant. Il deviendra la documentation d'utilisation une fois la fonction livrée.

## Objectif

- **État de la boîte** dans Home Assistant: lecture ou attente, histoire en cours, volume, carte SD, disponibilité.
- **Pilotage**: volume (dans les limites de la boîte) et arrêt de la lecture.
- **Cartes posées**: chaque carte posée prévient Home Assistant et y apparaît comme une étiquette (*Paramètres > Étiquettes*), utilisable comme déclencheur d'automatisation.

Hors périmètre de cette étape: cartes « commande » sans histoire (le type `home_assistant` de `PlaybackMode` reste inutilisé), entité `media_player`, lecture sur une autre enceinte, reprise de la lecture à distance.

## Choix: MQTT avec découverte automatique

| Piste | Retenue | Pourquoi |
|---|---|---|
| MQTT + découverte | **oui** | les deux sens avec un seul mécanisme; entités créées par Home Assistant sans code Python; lecteur d'étiquettes natif (composant `tag`); « indisponible » géré par le broker (LWT) pendant la veille |
| Webhook | non | un seul sens; JSON brut à filtrer par modèle dans chaque automatisation, pas d'étiquettes |
| API REST + jeton | non | le jeton donne tous les droits sur Home Assistant, un seul sens |
| Intégration Python (HACS) | non | second dépôt à maintenir; seul intérêt, une vraie entité `media_player` |
| API ESPHome, WebSocket | non | trop lourds pour ce qu'ils apportent |

Côté maison, il faut un broker MQTT (module **Mosquitto broker** de Home Assistant, avec un utilisateur dédié à la boîte) et l'intégration MQTT, que Home Assistant propose d'elle-même une fois le module installé.

## Appareil et entités

Identifiant stable de la boîte: `totobox_<mac>` (adresse MAC Wi-Fi en hexadécimal minuscule), indépendant du nom réseau que l'utilisateur peut changer. Nom de l'appareil: le nom réseau (`hostname`). Fabricant « TotoBox », modèle « HermitX CardReader », version du firmware, lien vers la page de la boîte (`configuration_url`).

Sujets sous `totobox/<id>/`:

| Sujet | Sens | Conservé | Contenu |
|---|---|---|---|
| `availability` | boîte → HA | oui | `online`; `offline` en dernière volonté (LWT) et avant la veille |
| `state` | boîte → HA | oui | JSON, ci-dessous |
| `tag` | boîte → HA | non | UID de la carte posée |
| `card_removed` | boîte → HA | non | UID de la carte retirée |
| `volume/set` | HA → boîte | non | volume en %, entier |
| `stop` | HA → boîte | non | `PRESS` |

État, publié à chaque changement et toutes les 15 s pendant la lecture (position):

```json
{
  "state": "playing",
  "story": "Les trois petits cochons",
  "card": "04A1B2C3",
  "track": 2,
  "tracks": 5,
  "position": 312,
  "volume": 55,
  "volume_min": 20,
  "volume_max": 85,
  "sd": "ready",
  "locked": true,
  "rssi": -61
}
```

`state` reprend `deviceStateToString()` (`ready`, `playing`, `paused`, `unknown_card`, `error`), plus `stopped` pour une lecture arrêtée depuis Home Assistant (voir plus bas).

Entités annoncées (découverte sous `homeassistant/<composant>/<id>/<objet>/config`, messages conservés):

| Entité | Composant | Remarques |
|---|---|---|
| État | `sensor` (enum) | `value_template` sur `state` |
| Histoire en cours | `sensor` | attributs: carte, piste, nombre de pistes, position |
| Volume | `number` | curseur, pas de 5, `min`/`max` = limites de la boîte; découverte republiée quand les limites changent |
| Arrêter | `button` | |
| Carte posée | `tag` | l'étiquette Home Assistant a pour identifiant l'UID de la carte |
| Carte retirée | `device_automation` | déclencheur `card_removed` |
| Carte SD | `sensor`, diagnostic | `absent`, `unreadable`, `ready` |
| Verrouillage | `binary_sensor`, diagnostic | lecture seule: le verrouillage ne se commande pas depuis Home Assistant |
| Signal Wi-Fi | `sensor`, diagnostic | dBm, désactivé par défaut |

La découverte « par appareil » (un seul message `homeassistant/device/<id>/config`, Home Assistant 2024.11 et plus) serait plus compacte; à retenir seulement si elle accepte `tag` et `device_automation`, sinon une découverte par entité.

## Règles de comportement

- **Hors ligne d'abord**: une histoire ne dépend jamais de Home Assistant. Broker absent, mot de passe faux ou Wi-Fi coupé: la boîte joue normalement, l'erreur n'apparaît que dans la page web (pas sur les LEDs, qu'un enfant regarde).
- **Cartes**: les cartes connues et inconnues sont publiées; **jamais les cartes maîtresses** (leur UID suffit à en copier une, et il n'a rien à faire sur le broker).
- **Volume**: borné par `volume_rules` aux limites de la boîte, comme la molette et la page (`currentVolume`, enregistrement différé `kVolumeSaveDelayMs`, jauge LED affichée). Une valeur hors limites est ramenée dans les limites, l'état publié corrige le curseur.
- **Arrêter**: la lecture s'arrête et le signet est enregistré, comme au retrait de la carte. La carte restant posée, la boîte ne relance pas l'histoire: il faut retirer et reposer la carte (règle dans `card_flow`, état `stopped`, LEDs comme en pause). Sans lecture en cours, la commande ne fait rien.
- **Verrouillage**: volume et arrêt restent accessibles verrouillé (ce sont la lecture en cours et le volume, déjà exposés). Les réglages MQTT, eux, sont des réglages: refusés verrouillé, comme les autres.
- **Mise à jour en cours**: commandes ignorées, état publié jusqu'au redémarrage (`offline` avant `ESP.restart()`).
- **Veille**: une commande de Home Assistant compte comme une activité (comme une requête web). Avant de dormir, la boîte publie `offline`, se déconnecte proprement (300 ms au plus) et s'endort. Une commande envoyée pendant la veille est perdue (non conservée par le broker): sans lecture, il n'y a rien à arrêter.
- **Réveil par une carte**: l'histoire part tout de suite de la carte SD; l'étiquette est publiée dès que le Wi-Fi et MQTT sont connectés, si la carte a été posée il y a moins de 60 s (au-delà, l'automatisation n'a plus de sens).
- **Réseau**: seulement connectée au Wi-Fi de la maison, jamais en point d'accès seul.

## Exécution

- **Service** `HomeAssistantService` (`src/ha_service.*`), appelé par `loop()`: reçoit l'état (`RuntimeState`, `DeviceConfig`, histoire), publie ce qui a changé, rend les commandes reçues; `main.cpp` les exécute avec les fonctions existantes (volume, arrêt via `card_flow`).
- **Décisions** dans un module sans matériel `ha_rules` (testé sur ordinateur): sujets, identifiant, messages de découverte, JSON d'état, lecture des commandes, bornage du volume, quand publier (changement, 15 s pendant la lecture), délai de l'étiquette au réveil, exclusion des cartes maîtresses.
- **Client MQTT**: [PubSubClient](https://github.com/knolleary/pubsubclient) (MIT, version figée dans `platformio.ini`), dans une tâche dédiée **épinglée au cœur 0**, priorité 1, comme `UpdateService`: une connexion, une attente réseau ou une reconnexion ne prend jamais de temps au cœur 1 (audio, `loop()`). Le client d'ESP-IDF (`mqtt_client.h`) est écarté: dans le framework précompilé, sa tâche n'est épinglée à aucun cœur (`CONFIG_MQTT_TASK_CORE_SELECTION_ENABLED` désactivé).
  - PubSubClient n'est pas prévu pour plusieurs tâches: seule la tâche MQTT l'utilise. `loop()` lui passe un instantané de l'état (sous mutex) et des événements à publier (file FreeRTOS: carte posée, retirée); elle lui rend les commandes reçues par une autre file. `loop()` n'attend jamais le réseau.
  - Tampon de 1 Ko (`setBufferSize`) pour les messages de découverte; reconnexion espacée de 10 s à 60 s (`retry_delay`, comme le Wi-Fi).
  - Un nom en `.local` est résolu par la boîte (`MDNS.queryHost`, dans la tâche MQTT), la résolution DNS du framework ne passant pas par mDNS.
- **Point à vérifier sur la boîte**: mémoire (pile de la tâche et tas), et aucune coupure du son pendant une connexion ou une reconnexion.

## Réglages et API

Persistance par `ConfigStore` (NVS, effacée par la remise à zéro; pas dans la sauvegarde des cartes):

| Champ | Défaut | Remarques |
|---|---|---|
| `haEnabled` | `false` | |
| `haAddress` | `homeassistant.local` | adresse de Home Assistant (ou du broker), modifiable si celle par défaut ne convient pas |
| `mqttUser` | vide | |
| `mqttPassword` | vide | secret: envoyé par `apiWithSecrets()`, vérifié par `rejectIfSealedInvalid()`, jamais renvoyé (`mqttPasswordSet`) |

L'adresse accepte ce que l'utilisateur a sous la main, lu par `ha_rules` (testé sur ordinateur):

| Saisie | Serveur | Port MQTT |
|---|---|---|
| `homeassistant.local`, `192.168.1.10`, `ha.maison.lan` | tel quel | 1883 |
| `http://192.168.1.10:8123/`, `https://ha.maison.lan` (adresse de la page Home Assistant) | nom ou IP | 1883 (le port web est ignoré) |
| `mqtt://192.168.1.20:1884`, `192.168.1.20:1884` (broker ailleurs ou sur un autre port) | nom ou IP | celui indiqué |

Une adresse vide ou illisible est refusée par la route (`400 invalid_ha_address`), `mqtts://` aussi tant que TLS n'est pas pris en charge (`400 ha_tls_unsupported`).

- `GET /api/config` et `POST /api/config` portent ces champs (verrouillage déjà appliqué par la route).
- `GET /api/status` ajoute `stoppedRemotely` et, si la boîte n'est pas verrouillée, `homeAssistant`: `{"enabled": true, "state": "off" | "connecting" | "connected" | "error", "error": "invalid_address" | "no_wifi" | "dns" | "unreachable" | "auth"}`.
- Page: section « Home Assistant » des réglages (interrupteur, adresse préremplie avec `homeassistant.local`, utilisateur, mot de passe, état de la connexion, aide pour installer Mosquitto). Pas dans l'assistant de première connexion: fonction avancée.
- Faux serveur (`scripts/mock-server.mjs`) et [api.md](api.md) mis à jour en même temps.

## Sécurité

- MQTT sans TLS sur le réseau local: le mot de passe passe en clair entre la boîte et le broker (comme pour tout appareil MQTT domestique). Utilisateur dédié recommandé.
- TLS (`mqtts://`, port 8883) dans un second temps, après mesure de la mémoire sur la boîte (une connexion TLS garde 30 à 40 Ko de tas, en plus de celle de la mise à jour en ligne). Deux modes de vérification prévus: certificat d'une autorité publique (Let's Encrypt via DuckDNS ou un domaine à soi), vérifié par le bundle de la boîte, la connexion se faisant alors par ce nom; certificat auto-signé d'un Mosquitto local, dont l'empreinte est retenue à la première connexion et confirmée dans la page.
- Toute personne qui peut écrire sur le broker peut arrêter la lecture ou changer le volume, toujours dans les limites de la boîte: rien de plus que ce que fait la molette.

## Étapes

1. `ha_rules` et ses tests (adresse, sujets, découverte, état, commandes): fait ([src/ha_rules.h](../src/ha_rules.h)).
2. `card_flow`: arrêt à distance (`onRemoteStop`, `RuntimeState::stoppedRemotely`, état « en pause » publié `stopped`, pas de relance tant que la carte reste posée, reprise après la carte SD annulée) et tests: fait.
3. Champs de configuration (`ConfigStore`, tests), routes `/api/config` et `/api/status` (avec `stoppedRemotely`, pour que la page affiche « Arrêtée » plutôt que « En pause »), faux serveur (page `/mock`: arrêt et volume depuis Home Assistant; mot de passe « faux » refusé), docs: fait.
4. `HomeAssistantService` (client, file d'événements, publication, veille, réveil), branchement dans `main.cpp`: fait, à essayer sur la boîte.
5. Page: section des réglages, état de la connexion: fait.
6. Essais sur la boîte avec Home Assistant: découverte, étiquettes, volume (bornes), arrêt, verrouillé, pendant la lecture (pas de coupure du son), veille et réveil par une carte, broker arrêté, mauvais mot de passe.
