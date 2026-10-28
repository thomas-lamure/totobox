# Modèles Onshape

Le boîtier est dessiné dans Onshape (documents publics, liens dans [../README.md](../README.md)). Les composants sont des **Features paramétrables** écrites en FeatureScript, versionnées ici:

- [hermitx_board.fs](hermitx_board.fs): carte HermitX CardReader simplifiée (PCB aux coins arrondis, trous de fixation, trous des barrettes, composants en volumes simples, nommés et colorés). **Fichier généré** par [../tools/hermitx_board.py](../tools/hermitx_board.py): modifier les cotes là-bas.
- [nfc_antenna.fs](nfc_antenna.fs): module d'antenne NFC HermitX.
- [speaker.fs](speaker.fs): haut-parleur rectangulaire.
- [encoder.fs](encoder.fs): encodeur rotatif KY-040 avec écrou, paroi et bouton, placé par la position du centre du trou sur la face intérieure de la paroi et les directions de l'arbre et des broches. La paroi ne doit pas dépasser 5 mm (canon de 7 mm moins écrou de 2 mm). Cotes nominales (fiche vendeur et EC11 usuel), à vérifier au pied à coulisse, en particulier la position de l'axe sur le circuit.
- [standoff.fs](standoff.fs): entretoise hexagonale M2 posée sur le fond, avec le perçage du fond: avant-trou Ø 1,6 pour une entretoise mâle-femelle (sous la carte), ou trou fraisé pour une vis M2 tête fraisée passée par le dessous (fermeture du fond).
- [card_guide.fs](card_guide.fs): bande guide-cartes collée contre la paroi de la fente: plaque la carte contre la cloison (et le tag contre l'antenne), biseau d'entrée en haut, encoches en bas à chaque bout pour les entretoises de fond.
- [sheet.fs](sheet.fs): plaque de contreplaqué (300 × 200 × 3 par défaut) pour le calepinage.
- [castle_joint.fs](castle_joint.fs): assemblage à créneaux entre deux plaques à angle droit (coin, bord ou T): nombre impair de créneaux d'environ la largeur visée, ceux des bouts à la plaque A; compensation de saignée laser et limitation de l'arête (coins où trois plaques se rencontrent).

## Importer une Feature

1. Dans le document, créer un **Feature Studio** (bouton `+` en bas à gauche), **un par fichier `.fs`**.
2. Garder les deux premières lignes créées par Onshape (`FeatureScript …;` et `import(path : "onshape/std/geometry.fs", …);`), puis coller tout le contenu du fichier en dessous.
3. Dans le **Part Studio**, ouvrir le menu des Features personnalisées (icône à droite de la barre d'outils) et ajouter **Carte HermitX**, **Antenne NFC HermitX**, **Haut-parleur TotoBox**, **Encodeur KY-040**, **Entretoise M2 TotoBox**, **Bande guide-cartes TotoBox**…

Onshape n'accepte que l'ASCII dans les annotations (`annotation { ... }`): pas d'accents dans les noms de Feature ni de paramètres.

Une Feature modifiée dans Onshape doit être recopiée ici (et inversement): le dépôt est la référence versionnée.

## Carte HermitX

### Repère

- Origine au coin inférieur gauche de la carte vue de dessus: LEDs en bas, USB-C en haut.
- X vers la droite (99 mm), Y vers le haut (62 mm), Z = 0 sous le PCB, composants au-dessus.

La carte apparaît avec ses pièces nommées (`PCB HermitX`, `USB-C`, `LED 1`…). L'épaisseur du PCB est réglable (1,6 mm par défaut).

### Cotes utiles pour le boîtier

Relevées sur la photo cotée du fabricant (précision ~ ±0,5 mm): **à vérifier au pied à coulisse** avant un boîtier ajusté.

| Élément | Position (mm) | Remarque |
|---|---|---|
| PCB | 99 × 62 × 1,6, coins R4 | |
| Trous de fixation | (4, 4) (95, 4) (4, 58) (95, 58), Ø 3,2 | entraxe 91 × 54, M3 |
| USB-C | X 59,8 → 68,8, dépasse le bord haut de 0,6 | hauteur 3,2: ouverture sur la face arrière |
| microSD | support X 75,2 → 90,8, Y 43 → 60,2 | carte insérée par le bord haut |
| Module ESP32-S3 (WROOM-1) | X 78 → 103,5, Y 21,25 → 39,25 | **dépasse de 4,5 mm du bord droit**: zone d'antenne X 97 → 103,5, sans métal autour |
| Boutons RESET / BOOT | centres (70,9 ; 37,6) et (70,9 ; 21,9) | poussoir jusqu'à 5 mm au-dessus du PCB |
| LEDs WS2812B | Y 7,5, X 9,1 / 20,6 / 32,1 / 43,5 / 54,9 / 66,4 / 77,9 / 89,3 | 5 × 5 mm: fenêtres ou diffuseur |
| Connecteur NFC | X 1,4 → 6, Y 26,9 → 35,2 | câble d'antenne vers le bord gauche |
| Connecteur haut-parleur | X 44,3 → 52,8, Y 54,8 → 60,5 | |
| Barrette IO (IO9…IO48) | 7 broches, Y 20,2, X 43,37 + n × 2,54 | barrette mâle droite; encodeur KY-040 sur IO9/10/11 |
| Barrette +5V / +3V3 / GND | X 57,45, Y 47,3 / 44,76 / 42,22 | barrette mâle droite |
| RX / TX | Y 18,1, X 90,7 / 93,24 | simples trous (pas de barrette) |

Encombrement total en X: 103,5 mm à cause de l'antenne du module, qui dépasse du PCB. Prévoir l'espace dans le boîtier et éviter vis, inserts ou blindage métallique à moins de ~15 mm de l'antenne.

Barrettes mâles modélisées au standard 2,54 mm: socle de 2,5 mm, broches carrées de 0,64 mm qui montent à 8,5 mm au-dessus du PCB et dépassent de 3 mm dessous. Ce sont les éléments les plus hauts de la carte (les poussoirs des boutons montent à 5 mm): prévoir la place des connecteurs femelles (Dupont) et des câbles, ou des fils soudés directement.

La face inférieure n'est pas modélisée: vérifier sur la carte réelle s'il y a des composants ou des soudures traversantes avant de changer la hauteur des entretoises.

### Modifier les cotes

Corriger les valeurs dans [../tools/hermitx_board.py](../tools/hermitx_board.py), régénérer la Feature, puis recoller le fichier dans le Feature Studio:

```bash
python3 hardware/tools/hermitx_board.py
```

Une vue de contrôle superpose les composants à la photo cotée de la carte:

```bash
python3 hardware/tools/hermitx_board.py --overlay photo_carte.png controle.svg
```

## Périphériques

Les cotes par défaut sont mesurées sur les pièces réelles, les détails (positions du trou, de la membrane…) estimés sur photo. Ce sont des **paramètres de la Feature**: les corriger directement dans Onshape après mesure. Pour placer un périphérique, utiliser **Transform** ou un **Mate connector** sur ses pièces.

### Antenne NFC HermitX

| Paramètre | Défaut | Remarque |
|---|---|---|
| Longueur (sens des fils) | 38 mm | mesuré |
| Largeur | 25 mm | mesuré |
| Epaisseur du PCB | 1,5 mm | PCB + adhésif = 2 mm mesurés |
| Epaisseur de l'adhesif | 0,5 mm | mesuré; double face blanc sous le module |
| Distance des soudures au bord | 9,5 mm | mesuré |
| Diametre des fils | 1 mm | posés à plat: épaisseur totale 3 mm au niveau des fils |
| Longueur des fils | 20 mm | représentation, câble réel plus long |

Repère: coin inférieur gauche, Z = 0 sous l'adhésif, fils vers -Y. Le module se colle par son adhésif sous la paroi où l'on pose les cartes: paroi fine pour la portée de lecture, et pas de métal entre l'antenne et la carte.

### Haut-parleur

| Paramètre | Défaut | Remarque |
|---|---|---|
| Longueur hors tout | 35 mm | mesuré |
| Largeur hors tout | 25 mm | mesuré |
| Epaisseur totale (avec joint) | 6,3 mm | mesuré; corps = épaisseur totale - joint |
| Epaisseur du joint adhésif | 0,5 mm | mesuré; corps de 5,8 mm |
| Largeur du bord du joint | 2,2 mm | estimé: ouverture ~ 30,6 × 20,6 mm |
| Longueur des fils | 20 mm | représentation |

Repère: centre du haut-parleur, Z = 0 au dos, face avant (joint adhésif) vers +Z, fils par le petit côté +X. Le joint se colle contre la paroi: percer la grille du boîtier dans l'ouverture du joint, pas au-delà, pour que le son ne fuie pas à l'intérieur.

## Boîtier

### Variable Studio « Cotes TotoBox »

Référencé en entier par le Part Studio. Les variables sont évaluées dans l'ordre de la liste: une variable n'utilise que celles placées avant elle.

| Variable | Valeur | Rôle |
|---|---|---|
| `fente_cartes` | 10 mm | largeur de la fente à cartes (la Sketch 1 du fond utilise `#fente_cartes + 4 mm`; parois gauche, avant et arrière suivent) |
| `antenne_bas_z` | 15 mm | dz de la Transform 1: haut de l'antenne contre le dessus, Z 40 |
| `haut_z` | 43 mm | haut des parois |
| `bande_haut_z`, `bande_epaisseur`, `bande_biseau` | 26,3 mm (2/3 de la hauteur), 3 mm, 80° | bande guide-cartes |
| `paroi`, `fond_epaisseur`, `fond_z` | | épaisseurs; `fond_z` = face intérieure du fond (−4 mm) |
| `interieur_gauche_x` / `_droite_x` / `_avant_y` / `_arriere_y` | | faces intérieures (`interieur_gauche_x` = −(fente + 4 mm)) |
| `entretoise_pcb`, `entretoise_pcb_filetage` | 8 mm, 3 mm | hauteur de la carte électronique |
| `entretoise_fond`, `entretoise_coin` | 10 mm | entretoises de fermeture du fond |
| `encodeur_x` / `_y` / `_z` | Z 22 | position de l'encodeur |

Les esquisses du boîtier gardent leurs cotes saisies: les relier aux variables au fil des modifications.

### Fixations

- **Carte électronique**: 4 entretoises laiton M2 × 8 + 3 mm mâle-femelle vissées dans un avant-trou Ø 1,6 du fond, sous les trous (4, 4), (95, 4), (4, 58), (95, 58); carte vissée dessus en M2, 5 mm de jeu sous les broches qui dépassent du dessous. La Feature « Hauteur de la carte » (juste après la carte dans l'historique) monte toutes ses pièces de `#fond_z + #entretoise_pcb`; le fond, esquissé sur la face inférieure du PCB, est extrudé à `#entretoise_pcb` sous elle (Extrude 1) et reste en place avec les parois. Les découpes cotées sur les composants (USB-C, microSD, fente des LEDs, couvre-LEDs) suivent la carte.
- **Encodeur**: en Z 22 (`encodeur_z`), 3,8 mm au-dessus du blindage du module ESP32, fixé par son écrou dans la paroi droite. Son trou (Sketch 10) n'est tenu que par son diamètre et sa position saisie (Y 31, Z 22): le redéplacer à la main si l'encodeur bouge. Broches vers l'arrière (+Y): avec des cosses Dupont droites, leur boîtier finit à ~2 mm de la paroi arrière, d'où des cosses coudées ou des fils soudés.
- **Fond**: 4 entretoises M2 × 10 femelle-femelle collées dans les coins des parois, vis M2 Torx tête fraisée par le dessous (trou Ø 2,2, fraisure Ø 4,4 à 90°).
- **Fente à cartes**: 10 mm (`fente_cartes`), réduite à 7 mm dans le bas par la bande guide-cartes de 3 mm (`bande_epaisseur`). La bande monte aux 2/3 de la hauteur de la boîte: au-dessus, la fente garde ses 10 mm pour que l'enfant trouve l'entrée; un biseau à 80° (de Z 11,6 à 26,3) rabat progressivement la carte vers la cloison. Encoches de 4,3 × 10,5 mm autour des entretoises de fond. Cartes en bois de 2 × 3 mm: 0,6 mm de jeu (ajuster `bande_epaisseur` selon l'épaisseur réelle du contreplaqué et de la colle). Tag de 20 mm au centre de la carte, en Z 28,8 → 48,8, face à l'antenne (Z 15 → 40): ~7 mm entre tag et antenne quand la carte est droite, ~10 mm si elle penche.

### Calepinage

Assemblage **Calepinage** de chaque document: chaque pièce posée à plat sur une plaque (Part Studio **Plaques**), face intérieure vers +Z, 2 mm de trait de coupe entre les pièces et 2 mm de marge au bord. Le calepinage ne suit pas les modifications de forme: après un changement de dimensions, vérifier que les pièces ne se chevauchent pas et réextraire les contours (voir [../laser/](../laser) et [../README.md](../README.md)).

### Version à créneaux

Copie de la version faite à la main, adaptée à la découpe laser:

- chanfreins des arêtes supprimés (« Chamfer 1 »): une découpe laser est droite;
- dix jonctions « Assemblage a creneaux TotoBox » (Feature Studio 8), créneaux d'environ `#creneau` = 10 mm, compensation `#saignee_compensation` = 0 mm (à régler si le logiciel de découpe ne la fait pas):
  - 4 coins verticaux (façade, côtés, arrière): 5 créneaux sur 50 mm;
  - dessus avec façade, arrière et côté droit, limités à la zone intérieure (X `#cloison_x` → `#interieur_droite_x`, Y `#interieur_avant_y` → `#interieur_arriere_y`);
  - cloison avec le dessus (tenons dans des encoches) et avec façade et arrière (tenons dans des mortaises), limités en Z à `#fond_z` → `#haut_z - #paroi`;
- le fond reste encastré et vissé, sans créneaux; couvre-LEDs et bande guide-cartes restent collés.

Les épaisseurs mesurent 3,01 mm aux créneaux: l'écart de 0,01 mm entre les parois du modèle est comblé, sans effet à la découpe.
