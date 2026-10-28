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
| Rouge clignotant | erreur: carte SD absente ou illisible, lecteur NFC muet, ou contenu de la carte impossible à lire |
| Bande qui se remplit de cyan | envoi de fichiers en cours (une carte posée est lue à la fin) |
| Jauge blanche | volume, de la limite basse (vide) à la limite haute (pleine) |
| Bande entière blanche | une carte maîtresse vient de déverrouiller les réglages |

## Première mise en route

1. Insérer une carte microSD formatée en **FAT32** (voir plus bas), brancher la boîte en USB-C.
2. Avec le téléphone, se connecter au point d'accès `TotoBox-XXXX`, avec le mot de passe de l'étiquette. Sans étiquette, le mot de passe est affiché dans la console série (voir [development.md](development.md)). La page s'ouvre d'elle-même (portail captif).
3. Un assistant guide les étapes, chacune peut être passée (« Configurer plus tard »):
   - la carte SD (détectée à chaud si elle manque);
   - le nom de la boîte (« Chambre de Léo » donne `http://chambre-de-leo.local`);
   - les cartes maîtresses et le code de secours;
   - la restauration des cartes, si une sauvegarde est présente sur la carte SD;
   - en dernier, le Wi-Fi de la maison.

Une boîte déjà utilisée (Wi-Fi, verrouillage ou cartes configurés) ne revoit pas l'assistant.

## Interface web

Adresse: `http://totobox.local` (ou `http://<nom>.local` si la boîte a été renommée), depuis le Wi-Fi de la maison ou le point d'accès de la boîte.

- **Accueil**: lecture en cours, volume, tuiles des cartes avec leur signet.
- **Nouvelle carte**: une carte inconnue posée affiche le bandeau « Nouvelle carte détectée » et un assistant en 3 étapes (carte, contenu, nom). La lecture démarre si la carte est encore posée.
- **Contenu d'une carte**: un dossier entier (lu dans l'ordre alphabétique) ou un seul fichier, choisi dans un sélecteur avec recherche.
- **Fichiers**: navigation par dossiers, envoi, renommage et déplacement (les cartes concernées suivent), corbeille restaurable. Sur ordinateur, glisser-déposer de fichiers et de dossiers (les sous-dossiers sont recréés, les fichiers non audio ignorés), et une ligne glissée sur un dossier y est déplacée. Les tags ID3 et pochettes des MP3 sont retirés à l'envoi.
- **Réglages**: limites de volume, Wi-Fi de la maison (avec « Oublier ce réseau »), point d'accès de secours, verrouillage, sauvegarde des cartes, remise à zéro.

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

La carte peut être retirée à chaud: la lecture s'arrête en gardant le signet, et reprend à la réinsertion si la carte NFC est toujours posée.

## Sauvegarde des cartes

Réglages > « Sauvegarde des cartes »: les associations (nom, contenu, signet) sont écrites dans `/totobox-cartes.json` à la racine de la carte SD, hors de la bibliothèque. Une seule sauvegarde: la nouvelle remplace l'ancienne, et une liste vide ne remplace jamais une sauvegarde existante. « Restaurer » remplace les associations actuelles par celles du fichier.

Les cartes maîtresses et le code de secours ne sont pas sauvegardés.

## Remise à zéro

Les cartes associées sont d'abord sauvegardées sur la carte SD, puis la boîte efface toute sa configuration (réglages, Wi-Fi, verrouillage, cartes et signets) et redémarre en point d'accès `TotoBox-XXXX` avec le mot de passe de l'étiquette, déverrouillée. Les fichiers de la carte SD sont conservés.

- **Depuis l'interface**: Réglages > « Réinitialiser la boîte… », taper `EFFACER`. Seulement déverrouillée et à l'arrêt. Si la sauvegarde échoue, rien n'est effacé.
- **En atelier, boîte démontée** (code et cartes maîtresses perdus): relier GPIO47 à la masse au démarrage. La bande se remplit de rouge pendant 5 s (retirer le pont annule), puis clignote en vert une fois la configuration effacée: retirer le pont, la boîte redémarre. Volontairement inaccessible sans démontage. Voir [hardware/electronics.md](../hardware/electronics.md).
