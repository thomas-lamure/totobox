# Production en série

Pour préparer plusieurs boîtes avec chacune son étiquette. Une boîte unique n'en a pas besoin: sans données d'usine, elle tire au hasard le mot de passe de son point d'accès au premier démarrage et l'affiche dans la console série.

## Mot de passe d'usine

Chaque boîte reçoit un mot de passe de point d'accès imprimé sur son étiquette, écrit dans la partition `mfg` ([partitions.csv](../partitions.csv), format décrit dans [src/factory_data.h](../src/factory_data.h)). La boîte ne l'écrit jamais: effacer la configuration ramène au mot de passe de l'étiquette. Un mot de passe choisi dans les réglages reste prioritaire.

## Provisionnement

Pour chaque boîte branchée en USB, après `pio run -t upload` (qui écrit aussi la table de partitions) et `pio run -t uploadfs`:

```bash
python3 scripts/provision.py --port /dev/cu.usbmodem1101
```

Le script ([scripts/provision.py](../scripts/provision.py), esptool fourni par PlatformIO):

1. lit l'adresse MAC et en déduit le nom `TotoBox-XXXX`;
2. tire le mot de passe, écrit la partition puis la relit pour vérifier;
3. ajoute une ligne à `production/labels.csv`: MAC, nom, mot de passe et contenu du QR code Wi-Fi (`WIFI:T:WPA;S:…;P:…;;`, lisible par l'appareil photo d'un téléphone).

Une boîte déjà présente dans ce fichier garde son mot de passe (`--new-password` pour en tirer un autre). Sans boîte branchée, `--mac 24:6f:28:ab:12:34 --image mfg.bin` produit seulement l'image.

`production/` contient les mots de passe: il n'est **jamais versionné** (voir `.gitignore`). Le conserver en lieu sûr.

Ne pas effacer toute la flash (`erase_flash`) d'une boîte provisionnée: relancer alors le script, qui réécrit le même mot de passe.
