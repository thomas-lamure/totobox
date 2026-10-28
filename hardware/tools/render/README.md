# Atelier de rendu

Rendus du boîtier (version à créneaux) à partir du modèle 3D d'Onshape, dans le navigateur avec [three.js](https://threejs.org/) (chargé depuis un CDN) : contreplaqué de bouleau et chants brunis par le laser générés par le programme, électronique aux couleurs du modèle, LEDs allumées, cartes en bois avec des illustrations d'exemple (les cartes ne sont pas modélisées dans Onshape). Les illustrations du README en sont tirées, retouchées ensuite par IA (image à image, géométrie conservée).

## Exporter le modèle

Depuis le document Onshape public [Totobox castle joint](https://cad.onshape.com/documents/0a7387541967f8e833f620d0) : clic droit sur l'onglet « Part Studio 1 » > « Export… », format **glTF**, puis enregistrer le fichier sous `hardware/tools/render/model/totobox-castle.gltf`. Ce fichier (≈ 1,7 Mo) n'est pas versionné : le réexporter après chaque modification du boîtier.

Le rendu retrouve les pièces par leur nom dans Onshape (`Box Top`, `LED 1`…, `Haut-parleur (corps)`, `Encodeur (bouton)`) : un renommage dans Onshape est à reporter dans `index.html`.

## Rendre

```bash
make render      # node hardware/tools/render/server.mjs, puis ouvrir http://localhost:8090
```

Les boutons affichent les vues (`hero` : boîte fermée avec une carte dans la fente; `cards` : boîte et cartes posées devant; `inside` : boîte sans son dessus), « Enregistrer » écrit la vue affichée dans `hardware/tools/render/out/rendu-<vue>.jpg` (1600 × 1000, non versionné). Un autre modèle se charge avec `?model=chemin.gltf`.

Les vues (position de la caméra, éléments visibles) sont dans `VIEWS`, les matières dans `plywood()`, `birchTexture()` et la boucle de chargement du modèle, les cartes dans `woodCard()`.

## Retouche par IA

Pour un rendu plus photographique, passer l'image dans un outil d'IA « image à image » avec une faible force de transformation (0,30 à 0,45, et ControlNet Canny ou Depth si l'outil le permet), puis vérifier que les détails sont restés exacts : 8 LEDs dans une seule fente sur la façade, créneaux aux coins et le long du dessus, molette sur le côté droit seulement, grille du haut-parleur sur le dessus, fente à cartes à gauche. La légende du README précise qu'il s'agit d'une illustration générée par IA.
