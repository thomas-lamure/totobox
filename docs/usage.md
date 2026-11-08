# Guide d'utilisation

Pour les parents qui installent et règlent la boîte. L'enfant, lui, n'a besoin que des cartes et de la molette.

## Au quotidien

- **Poser une carte** lance son histoire ou sa musique. **La retirer** met en pause immédiatement et enregistre le signet (piste et position): la reposer reprend au même endroit. En fin d'histoire, le signet repart du début.
- **Molette**: volume, entre les limites réglées par les parents (5 % par cran). **Appui**: réveille la boîte.
- **Veille** après 5 min sans interaction ni lecture. Poser une carte ou appuyer sur la molette réveille la boîte et lance l'histoire.

Le signet est précis à l'octet près en MP3 (même à débit variable), WAV, M4A, FLAC et AAC; OGG et Opus repartent du début de la piste.

## LEDs

| Ce qu'on voit | Signification |
|---|---|
| Chenillard bleu aller-retour | prête, en attente d'une carte |
| Chenillard vert | lecture |
| Rouge atténué | carte inconnue: à associer depuis l'interface web |
| Rouge clignotant | erreur: carte SD absente ou illisible, lecteur NFC muet, contenu de la carte impossible à lire, ou deux cartes posées ensemble (rien ne joue; l'histoire de la carte qui reste reprend environ 1,5 s après le retrait de l'autre) |
| Bande qui se remplit de cyan | envoi de fichiers en cours (une carte posée est lue à la fin) |
| Jauge blanche | volume, de la limite basse (vide) à la limite haute (pleine) |
| Barre verte qui s'ouvre en deux (s'éteint du centre vers les bouts) | une carte maîtresse vient de déverrouiller les réglages |

## Première mise en route

1. Insérer une carte microSD formatée en **FAT32** (voir plus bas), brancher la boîte en USB-C.
2. Avec le téléphone, se connecter au point d'accès `TotoBox-XXXX`, avec le mot de passe de l'étiquette. Sans étiquette, le mot de passe est affiché dans la console série (voir [development.md](development.md)). La page s'ouvre d'elle-même (portail captif).
3. Un assistant guide les étapes, chacune peut être passée (« Configurer plus tard »):
   - la carte SD (détectée à chaud si elle manque);
   - le nom de la boîte (« Chambre de Léo » donne `http://chambre-de-leo.local`);
   - les cartes maîtresses et le code de secours;
   - la restauration des cartes, si une sauvegarde est présente sur la carte SD;
   - les mises à jour (être informé des nouvelles versions, les installer automatiquement);
   - en dernier, le Wi-Fi de la maison.

Une boîte déjà utilisée (Wi-Fi, verrouillage ou cartes configurés) ne revoit pas l'assistant.

## Interface web

Adresse: `http://totobox.local` (ou `http://<nom>.local` si la boîte a été renommée), depuis le Wi-Fi de la maison ou le point d'accès de la boîte.

- **Accueil**: lecture en cours (avec sa progression dans la piste), volume, tuiles des cartes avec leur signet. Un appui sur une carte ouvre sa fiche: nom, apparence (un emoji et une couleur pour la reconnaître), contenu lu, signet. À partir de neuf cartes, un champ de recherche apparaît au-dessus des tuiles.
- **Nouvelle carte**: une carte inconnue posée affiche le bandeau « Nouvelle carte détectée » et un assistant en 3 étapes (carte, contenu, nom). La lecture démarre si la carte est encore posée.
- **Contenu d'une carte**: un dossier entier (lu dans l'ordre alphabétique) ou un seul fichier, choisi dans un sélecteur avec recherche.
- **Fichiers**: navigation par dossiers, envoi, téléchargement d'un fichier ou d'un dossier (menu « ⋯ » > Télécharger, voir [Télécharger un dossier](#télécharger-un-dossier)), renommage et déplacement (les cartes concernées suivent), corbeille restaurable. Sur ordinateur, glisser-déposer de fichiers et de dossiers (les sous-dossiers sont recréés, les fichiers non audio ignorés), et une ligne glissée sur un dossier y est déplacée. Les tags ID3 et pochettes des MP3 sont retirés à l'envoi. Voir [Volume des MP3](#volume-des-mp3) pour l'égalisation du volume. Un téléchargement s'arrête si une carte lance une histoire: l'histoire passe avant.
- **Réglages** (roue dentée), en trois onglets, chacun enregistré seul: **Écoute** (limites de volume, verrouillage), **Réseau** (Wi-Fi de la maison avec « Oublier ce réseau », nom de la boîte, point d'accès de secours, Home Assistant), **Boîte** (mises à jour, informations, sauvegarde des cartes, remise à zéro). « Enregistrer » ne concerne que l'onglet affiché; les boutons du verrouillage, de la sauvegarde et de la remise à zéro, et les interrupteurs des mises à jour, agissent tout de suite.

Pendant la lecture, la boîte refuse l'accès aux fichiers (pour ne pas faire sauter le son): la page l'indique.

Le navigateur intégré qui s'ouvre sur le portail captif d'Android ne sait pas choisir de fichiers: la page le détecte et propose l'adresse à ouvrir dans Chrome ou Safari.

## Verrouillage des réglages

- Une ou plusieurs **cartes maîtresses** (une par parent, 8 au maximum) et un **code de secours** à 6 chiffres commun.
- Verrouillée, la page n'affiche que la lecture en cours et le volume, et la boîte refuse elle-même toutes les autres actions.
- Poser une carte maîtresse ou saisir le code déverrouille, jusqu'à l'appui sur le cadenas ou au redémarrage. 5 codes faux bloquent les essais pendant 60 s.

## Réseau

- **Wi-Fi de la maison** si configuré; sinon, ou s'il ne répond pas en 15 s, **point d'accès de secours** `TotoBox-XXXX` avec portail captif.
- Le nom du point d'accès et son mot de passe se changent dans les réglages. Tant que le mot de passe n'a pas été personnalisé, celui de l'étiquette reste valable (y compris après une remise à zéro).
- Changer de Wi-Fi ne redémarre pas la boîte: depuis le point d'accès, elle essaie le nouveau réseau sans couper le point d'accès, et la page affiche le résultat (connectée avec sa nouvelle adresse, mot de passe incorrect, réseau introuvable).
- Plusieurs boîtes dans la maison: leur donner des noms différents dans les réglages.
- Si l'adresse `.local` cesse de répondre (certaines box ne relaient plus le nom au bout de quelques minutes), la page bascule d'elle-même sur l'adresse IP de la boîte.

## Carte microSD

La carte doit être formatée en **FAT32** (ou FAT16). L'exFAT, format d'usine des cartes de 64 Go et plus, n'est pas lu. Une carte illisible est signalée dans la page avec la marche à suivre, et peut être formatée par la boîte (« Formater la carte… », taper `FORMATER`: efface tout).

La bibliothèque est dans `/content`. Une carte NFC pointe vers un fichier ou un dossier; un dossier est lu dans l'ordre alphabétique, d'où des noms préfixés:

```text
/content/
├── music/
│   └── comptines/
│       ├── 01-au-clair-de-la-lune.mp3
│       └── 02-meunier-tu-dors.mp3
└── stories/
    └── petit-prince/
        ├── 01-introduction.mp3
        ├── 02-chapitre-01.mp3
        └── 03-chapitre-02.mp3
```

Pour préparer des MP3 légers sans pochette depuis un ordinateur:

```bash
ffmpeg -i entree.mp3 -map 0:a -c:a libmp3lame -b:a 128k -ar 44100 -map_metadata -1 -id3v2_version 0 -write_id3v1 0 -fflags +bitexact sortie.mp3
```

### Télécharger un dossier

Menu « ⋯ » d'un dossier > Télécharger: la boîte envoie le dossier et ses sous-dossiers en une archive ZIP (`comptines.zip`), à décompresser sur l'ordinateur ou le téléphone. Seuls les fichiers audio y sont, sans compression (les MP3 le sont déjà), 2000 fichiers et 4 Go au plus.

Avec Chrome ou Edge sur ordinateur, le menu peut à la place proposer « Télécharger dans un dossier de l'ordinateur… »: on choisit un dossier, et la page y recopie les fichiers un par un en recréant l'arborescence de la carte (le dossier téléchargé et ses sous-dossiers, sur trois niveaux comme l'écran Fichiers). Ces navigateurs ne l'autorisent que sur une page sécurisée: pour la page de la boîte, ouvrir `chrome://flags/#unsafely-treat-insecure-origin-as-secure`, y ajouter l'adresse de la boîte (`http://totobox.local`), activer, puis relancer le navigateur. Sans ce réglage, c'est l'archive ZIP.

Une carte posée pendant un téléchargement lance l'histoire et interrompt le téléchargement.

### Volume des MP3

Case « Égaliser le volume des MP3 » (cochée par défaut, sous « Ajouter » et dans l'assistant de carte): avant l'envoi, la page mesure le niveau perçu de chaque MP3 (en LUFS, comme les plateformes de streaming) et corrige un fichier trop faible ou trop fort, pour que toutes les histoires sonnent pareil au même cran de molette. La correction se fait sans réencoder ni perdre de qualité (comme mp3gain): seul le gain inscrit dans chaque trame change, par pas de 1,5 dB.

- Cible: −16 LUFS. Un fichier à moins de 1,5 dB de la cible n'est pas touché.
- Montée de 12 dB au plus, limitée pour ne pas écrêter plus de 0,01 % des échantillons (inaudible).
- Le message de fin d'envoi indique les corrections appliquées (« Volume égalisé pour 2 fichiers : +6 dB, +4,5 dB »).
- L'analyse lit tout le fichier: environ 2 s pour 20 minutes d'histoire sur ordinateur, plus sur téléphone. Les autres formats (WAV, M4A…) partent tels quels.

Le choix de la case est retenu par le navigateur. Pour des MP3 déjà sur la carte SD, les renvoyer depuis la page, ou les corriger sur ordinateur avec mp3gain (`mp3gain -r -d 2 *.mp3`: 89 + 2 = 91 dB, soit environ −16 LUFS).

La carte peut être retirée à chaud: la lecture s'arrête en gardant le signet, et reprend à la réinsertion si la carte NFC est toujours posée.

## Sauvegarde des cartes

Réglages > Boîte > « Sauvegarde des cartes »: les associations (nom, contenu, signet) sont écrites dans `/totobox-cartes.json` à la racine de la carte SD, hors de la bibliothèque. Une seule sauvegarde: la nouvelle remplace l'ancienne, et une liste vide ne remplace jamais une sauvegarde existante. « Restaurer » remplace les associations actuelles par celles du fichier.

Les cartes maîtresses et le code de secours ne sont pas sauvegardés.

## Home Assistant

Réglages > Réseau > « Home Assistant » (désactivé par défaut): la boîte apparaît dans Home Assistant comme un appareil, par MQTT avec la découverte automatique. Détails techniques: [home-assistant.md](home-assistant.md).

Pas à pas pour une première installation: [home-assistant-mqtt.md](home-assistant-mqtt.md).

1. Dans Home Assistant, installer le broker MQTT (Paramètres > Modules complémentaires > « Mosquitto broker »), créer un utilisateur pour la boîte (Paramètres > Personnes > Utilisateurs), puis ajouter l'intégration MQTT si Home Assistant ne la propose pas de lui-même.
2. Dans la page de la boîte, activer « Relier la boîte à Home Assistant », vérifier l'adresse (`homeassistant.local` par défaut; l'adresse de la page Home Assistant, comme `http://192.168.1.10:8123`, convient aussi; `mqtt://adresse:port` pour un broker ailleurs ou sur un autre port), saisir l'utilisateur et son mot de passe, enregistrer. L'état de la liaison s'affiche sous les champs.

Dans Home Assistant, l'appareil (nom réseau de la boîte) montre l'état, l'histoire en cours (carte, piste, position), le volume (curseur limité aux bornes de la boîte), un bouton « Arrêter », et en diagnostic la carte SD, le verrouillage et le signal Wi-Fi. Chaque carte posée apparaît dans Paramètres > Étiquettes, où lui donner un nom et créer une automatisation; le retrait d'une carte est un déclencheur de l'appareil. Les cartes maîtresses ne sont jamais envoyées.

- « Arrêter » coupe l'histoire et garde le signet; tant que la carte reste posée, la boîte ne relance rien (LEDs jaunes, « Arrêtée » dans la page): retirer puis reposer la carte reprend l'histoire.
- Le volume et l'arrêt restent pilotables quand les réglages sont verrouillés, comme depuis la page.
- La boîte en veille est « indisponible ». Une carte qui la réveille lance l'histoire tout de suite; Home Assistant est prévenu quelques secondes plus tard, une fois le Wi-Fi connecté.
- Sans Home Assistant (broker arrêté, mot de passe refusé, Wi-Fi coupé), la boîte fonctionne normalement; seule la page affiche l'erreur.
- Désactiver la liaison retire l'appareil de Home Assistant. Après une remise à zéro, il y reste « indisponible »: le supprimer depuis Home Assistant.
- Connexion non chiffrée (MQTT sur le réseau local, port 1883); `mqtts://` n'est pas encore pris en charge.

## Mise à jour

Deux réglages (Réglages > Boîte > Mises à jour, enregistrés dès qu'on les change, et une étape de l'assistant de première connexion):

- **Être informé des mises à jour de la TotoBox** (activé par défaut): à son ouverture, puis une fois par jour tant qu'elle reste ouverte, la page regarde sur GitHub si une nouvelle version est publiée (le téléphone doit avoir accès à Internet; rien n'est envoyé, la page lit seulement la liste des versions). Un bandeau l'annonce (« Plus tard » le masque jusqu'à la version suivante); la mise à jour reste aussi proposée dans les réglages. Désactivé, la page ne contacte plus GitHub.
- **Procéder automatiquement aux mises à jour lorsque possible** (désactivé par défaut): une fois par jour, la boîte cherche elle-même une nouvelle version sur GitHub et l'installe, sans page ouverte. Elle attend pour cela d'être connectée au Wi-Fi de la maison, de connaître l'heure (NTP), et une minute sans histoire ni carte posée, réveil compris: un enfant qui réveille la boîte pour écouter n'est jamais gêné. Une carte posée pendant la recherche reporte l'installation. La date de la dernière recherche est affichée dans les réglages. Une page ouverte pendant une mise à jour automatique en affiche l'avancement, comme pour une mise à jour lancée à la main.

« Voir » affiche les nouveautés, puis « Installer »: la boîte télécharge elle-même la nouvelle version, vérifie son empreinte, l'installe et redémarre (environ une minute, LEDs cyan pendant le téléchargement). Les histoires, les cartes, les signets et les réglages sont conservés.

Conditions: réglages déverrouillés, aucune histoire en cours, et la boîte connectée au Wi-Fi de la maison (en point d'accès seul, elle n'a pas accès à Internet). En cas d'échec (coupure réseau, fichier abîmé), rien n'est changé: la boîte garde sa version et peut réessayer. Une version qui ne peut pas s'installer depuis la page s'installe par USB (voir le lien vers GitHub dans la fenêtre).

## Remise à zéro

Les cartes associées sont d'abord sauvegardées sur la carte SD, puis la boîte efface toute sa configuration (réglages, Wi-Fi, verrouillage, cartes et signets) et redémarre en point d'accès `TotoBox-XXXX` avec le mot de passe de l'étiquette, déverrouillée. Les fichiers de la carte SD sont conservés.

- **Depuis l'interface**: Réglages > Boîte > « Réinitialiser la boîte… », taper `EFFACER`. Seulement déverrouillée et à l'arrêt. Si la sauvegarde échoue, rien n'est effacé.
- **En atelier, boîte démontée** (code et cartes maîtresses perdus): relier GPIO47 à la masse au démarrage. La bande se remplit de rouge pendant 5 s (retirer le pont annule), puis clignote en vert une fois la configuration effacée: retirer le pont, la boîte redémarre. Volontairement inaccessible sans démontage. Voir [hardware/electronics.md](../hardware/electronics.md).
