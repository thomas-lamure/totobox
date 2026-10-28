# Matériel et boîtier

Tout ce qu'il faut pour fabriquer une TotoBox: composants, câblage, boîtier en contreplaqué de 3 mm et fichiers de découpe. Sous licence [CC BY-SA 4.0](../LICENSES/CC-BY-SA-4.0.txt) (sauf les scripts de [tools/](tools), sous GPL-3.0-or-later).

| Dossier ou fichier | Contenu |
|---|---|
| [electronics.md](electronics.md) | carte, broches, câblage de l'encodeur (le seul à câbler) |
| [onshape/](onshape) | Features Onshape (FeatureScript) des composants et du boîtier, notes de conception |
| [laser/](laser) | contours, DXF de découpe et gabarits PDF A4 à l'échelle 1:1 |
| [tools/](tools) | générateurs des DXF, des PDF et du modèle de la carte, et leurs tests; [atelier de rendu](tools/render/README.md) des illustrations |

## Nomenclature

Pour une boîte. Les lots couvrent plusieurs boîtes. Liens donnés à titre indicatif.

| Élément | Quantité | Exemple |
|---|---|---|
| Carte HermitX CardReader (ESP32-S3, PN532, MAX98357, microSD, 8 LEDs), livrée avec son antenne NFC (38 × 25 mm) et son haut-parleur (35 × 25 mm), câbles et connecteurs compris | 1 | [Amazon B0FGPDRDQX](https://www.amazon.fr/dp/B0FGPDRDQX) |
| Carte microSD (FAT32: l'exFAT n'est pas lu) | 1 | [Amazon B07R59FHVG](https://www.amazon.fr/dp/B07R59FHVG) |
| Encodeur rotatif KY-040 avec bouton (lot de 5) | 1 | [Amazon B07FJQH1F7](https://www.amazon.fr/dp/B07FJQH1F7) |
| Autocollants NFC (un par carte d'histoire, collé entre les deux épaisseurs de bois) | 1 par carte | [Amazon B0GYZ32L3R](https://www.amazon.fr/dp/B0GYZ32L3R) |
| ou cartes NFC en plastique, au format bancaire, à la place des cartes en bois | 1 par histoire | [Amazon B0GYPR1SF9](https://www.amazon.fr/dp/B0GYPR1SF9) |
| Contreplaqué 3 mm, plaques de 300 × 200 mm (lot de 9) | 1 plaque pour le boîtier, 1 plaque pour 5 cartes en bois | [Amazon B0DQKRPFDD](https://www.amazon.fr/dp/B0DQKRPFDD) |
| Entretoises laiton M2 (kit de 300): M2 × 8 + 3 mm mâle-femelle sous la carte électronique, M2 × 10 mm femelle-femelle collées dans les coins pour le fond | 4 + 4 | [Amazon B08VD1ZCFJ](https://www.amazon.fr/dp/B08VD1ZCFJ) |
| Vis M2 Torx inox tête fraisée (kit M2/M2,5/M3): fixation de la carte sur ses entretoises, et du fond (passage Ø 2,2, fraisure Ø 4,4) | 4 + 4 | [Amazon B0F3J84JXH](https://www.amazon.fr/dp/B0F3J84JXH) |

Non listés: colle à bois, cyanoacrylate (entretoises du fond), patins.

## Deux versions du boîtier

Même boîte de 9 pièces, chacune dans son document Onshape public:

- **[Faite à la main](https://cad.onshape.com/documents/adf09e5f379a22ee68ce163c)**: parois collées bord à bord, coins chanfreinés, pièces découpées à la scie (2 mm de trait de coupe). Fichiers `totobox_manuel_*`.
- **[À créneaux](https://cad.onshape.com/documents/0a7387541967f8e833f620d0)** (« castle joint »), pour la découpe laser: coins, dessus et cloison assemblés par créneaux d'environ 10 mm, collés. Fichiers `totobox_castle_*`.

Le [document d'origine](https://cad.onshape.com/documents/26767c2f4ee2790f86315da3) contient le premier modèle de la carte. Les détails de conception (repères, cotes, Variable Studio, fixations) sont dans [onshape/README.md](onshape/README.md).

Principes communs:

- **Cartes** au format bancaire (85,6 × 54 mm): deux épaisseurs de 3 mm avec le tag NFC collé au centre, posées debout dans une fente de 10 mm; une bande biseautée les plaque contre la cloison, devant l'antenne.
- **Fixations**: carte électronique sur 4 entretoises M2 × 8 + 3 vissées dans le fond; fond démontable, vissé par 4 vis M2 tête fraisée dans des entretoises collées dans les coins; encodeur fixé par son écrou dans la paroi droite.
- **Calepinage**: les 9 pièces tiennent sur une seule plaque de 300 × 200 (2 mm entre les pièces et au bord); les autres plaques donnent 5 cartes chacune (10 demi-cartes de 85,6 × 54).

## Découpe

Dans [laser/](laser), en millimètres, origine au coin de la plaque de 300 × 200, pour chaque version:

- `<version>_calepinage_a4.pdf`: **gabarit à imprimer** à l'échelle 1:1 sur une page A4 paysage, en « taille réelle » (100 %), jamais « ajuster à la page ». Au moins 7 mm de marge: imprimable sur toutes les imprimantes. Chaque trou et chaque arrondi a une croix de centrage, et chaque groupe de trous identiques le diamètre du foret à utiliser (Ø1,6 avant-trous des entretoises, Ø2,2 vis du fond, Ø2,5 grille du haut-parleur, Ø3,5 et Ø3 aux coins des fentes USB-C et microSD, Ø7,5 encodeur), là où le texte tient;
- `<version>_plaque.dxf`: les 9 pièces placées comme dans le calepinage, un calque par pièce (FOND, FACADE, ARRIERE, COTE_GAUCHE, COTE_DROIT, DESSUS, CLOISON, COUVRE_LEDS, BANDE_GUIDE), plus le calque ARRIERE_MICROSD;
- `<version>_<piece>.dxf`: chaque pièce seule, ramenée à l'origine, pour un placement libre dans le logiciel de découpe;
- `<version>_contours.txt`: les contours sources, extraits des documents Onshape (format décrit en tête de fichier).

**Toutes les pièces sont vues de leur face intérieure** (celle tournée vers le centre de la boîte; pour la bande guide-cartes, celle collée contre le dessus): le gabarit papier se colle de ce côté, ses traces de colle restent cachées une fois la boîte montée.

La fente de la carte microSD dans l'arrière est **optionnelle** (elle sert seulement à changer la carte sans ouvrir la boîte): calque ARRIERE_MICROSD dans les DXF (à désactiver pour ne pas la couper), pointillés dans les PDF.

Particularités:

- version à créneaux: le fond est exporté sans ses trous (avant-trous des entretoises et trous fraisés des vis, à percer à la main);
- version manuelle: le fond garde ses perçages, le couvre-LEDs est tracé par sa grande face (25,1 mm) et le biseau à 45° de ses deux bords se fait au ponçage;
- les deux: la bande guide-cartes est découpée droite, son biseau à 80° se fait au ponçage. Pas de compensation de saignée dans les fichiers: la régler dans le logiciel de découpe.

### Régénérer les fichiers

Après une modification des contours (Python 3, bibliothèque standard seulement):

```bash
make cad        # DXF et PDF des deux versions
make test-cad   # tests des générateurs et des contours
```

Le générateur DXF fusionne les extrémités distantes de moins de 0,02 mm (écarts de modélisation aux pieds des créneaux) et vérifie que chaque contour est fermé. La CI vérifie que les fichiers versionnés sont à jour (`make check-cad`).

Après une modification du boîtier dans Onshape, les contours sont à réextraire du Part Studio (face au profil complet de chaque pièce, placée selon l'assemblage « Calepinage »): il n'existe pas encore de script pour cette étape.
