# Journal des modifications

Format inspiré de [Keep a Changelog](https://keepachangelog.com/fr/1.1.0/). Les versions suivent le [versionnage sémantique](https://semver.org/lang/fr/).

## Non publié

### Page web

- Fiche d'une carte: un appui sur l'emoji ouvre une grille de 32 emojis à choisir d'un toucher, sans passer par le clavier; « Autre » garde la saisie libre.

## 1.5.3 - 2026-11-08

### Page web

- Ordre des pistes: une piste glissée près du haut ou du bas de la fenêtre fait défiler la liste, pour l'emmener loin dans un long dossier.

## 1.5.2 - 2026-11-08

### Page web

- Fenêtres « Ordre des pistes », « Déplacer » et choix d'un dossier (nouvelle carte, fiche d'une carte): une longue liste défile; elle était tronquée à la hauteur de la fenêtre, et les derniers éléments restaient inaccessibles.

## 1.5.1 - 2026-11-08

### Firmware

- Apparence des cartes: `/api/config` renvoie maintenant l'emoji et la couleur. Ils étaient enregistrés mais absents de la réponse, si bien que la page revenait à l'apparence par défaut.

## 1.5.0 - 2026-11-08

### Page web

- Lecture en cours: barre de progression et durée de la piste (`activeDurationMs` dans `/api/status`).
- Cartes: un emoji et une couleur par carte (fiche de la carte > Apparence), affichés sur la tuile; enregistrés avec la carte, y compris dans la sauvegarde sur la carte SD. Une recherche apparaît à partir de neuf cartes.
- Réglages en trois onglets, chacun enregistré seul: Écoute (volume autorisé, verrouillage), Réseau (Wi-Fi, point d'accès, Home Assistant), Boîte (mises à jour, informations, sauvegarde, remise à zéro). Le bouton dit ce qu'il enregistre, et les interrupteurs des mises à jour s'enregistrent dès qu'on les change: un seul « Enregistrer » pour neuf sections ne disait pas ce qui était pris en compte.
- Fichiers: « Sélectionner » pour déplacer, télécharger ou mettre à la corbeille plusieurs éléments d'un coup.
- Fichiers: « Ordre des pistes » range les pistes d'un dossier (glisser ou flèches) et les renomme `01-`, `02-`…, en gardant le style de numéros déjà présent (`01_`, `1 - `…). Les cartes suivent leur piste, et un signet sur ce dossier reste sur la même piste.
- Envoi de fichiers: une file montre l'avancement de chaque fichier, avec un bouton « Annuler »; un fichier refusé n'arrête plus les suivants.
- Corbeille en dossiers, comme l'écran Fichiers: on ouvre un dossier supprimé pour n'en restaurer qu'un fichier, et « Sélectionner » restaure ou supprime définitivement plusieurs éléments d'un coup. Restaurer un dossier qui ne sert qu'au rangement remet son contenu en place au lieu de créer « stories (1) ».
- Fiche d'une carte en cours de lecture: la position en cours au lieu du signet enregistré, périmé tant que la carte est posée.
- Espace de la carte SD: l'accueil et les réglages affichent l'espace utilisé (« 18 Mo utilisés sur 32 Go »); l'espace libre arrondi donnait « 32 Go libres sur 32 Go » sur une carte presque vide.

### Firmware

- Un envoi de fichier interrompu (annulé depuis la page, connexion perdue) efface le fichier à moitié écrit au lieu de le laisser sur la carte SD.

## 1.4.1 - 2026-11-08

### Firmware

- La jauge du volume sur les LEDs s'efface 1 s après le dernier changement (5 s auparavant).
- Déverrouillage par carte maîtresse: la bande s'allume en vert puis « s'ouvre en deux », les LEDs s'éteignant du centre vers les bouts (moins d'une seconde), au lieu de 5 s de bande blanche.

### Développement

- Table des routes `/api` ([src/api_routes.cpp](src/api_routes.cpp)): refus quand la boîte est verrouillée, pendant une lecture ou un téléchargement, appliqués au même endroit pour toutes les routes au lieu d'être répétés en tête de chacune. Testée sur ordinateur (verrouillée, la boîte n'expose que la lecture en cours et le volume), et comparée à docs/api.md et au faux serveur par `make test-scripts`.
- Protection des routes: le corps attendu (aucun, JSON, ou multipart pour l'envoi de fichiers) est décidé par `request_guard::expectedBody()` d'après la méthode et le chemin, et testé route par route; les routes ne choisissent plus elles-mêmes le contrôle (cause de l'erreur 415 corrigée en 1.4.0).

## 1.4.0 - 2026-11-08

### Firmware

- Sans carte posée, la recherche de carte est suspendue pendant qu'on tourne la molette (reprise 400 ms après le dernier cran): le champ radio du lecteur NFC, capté par des fils d'encodeur trop proches de l'antenne, faisait monter le volume au maximum. Le remède reste d'éloigner les fils (voir docs/problemes-connus.md).
- Deux cartes posées ensemble: la boîte arrête l'histoire (signet gardé) et clignote en rouge, la page affiche « Deux cartes posées ». Depuis 1.0.1, le lecteur rendant les deux cartes en alternance, la lecture changeait d'histoire à chaque recherche et le son hachait. L'histoire de la carte qui reste reprend environ 1,5 s après le retrait de l'autre.

### Page web

- Corbeille: chaque fichier ou dossier supprimé est listé avec son dossier d'origine, et se restaure seul. Elle n'affichait que le premier niveau: un fichier supprimé d'un sous-dossier apparaissait comme ce sous-dossier, et « Restaurer » le recréait en double (« music-1 »). La boîte retire de la corbeille les dossiers vidés par une restauration, et la page n'affiche plus un dossier sans fichier.
- Envoi de fichiers: la boîte répondait « 415 json_required » à la fin de l'envoi (affiché comme une erreur) alors que les fichiers étaient bien enregistrés.
- Téléchargement d'un fichier ou d'un dossier de la carte SD (menu « ⋯ » > Télécharger, route `GET /api/download`). Un dossier part en archive ZIP faite au fil de l'eau par la boîte, avec ses sous-dossiers; Chrome et Edge autorisés pour la page (`chrome://flags`) peuvent à la place le recopier dans un dossier choisi de l'ordinateur. La boîte envoie par morceaux et reste réactive; une carte posée lance l'histoire et interrompt le téléchargement.
- Égalisation du volume des MP3 à l'envoi (case cochée par défaut): la page mesure le niveau perçu de chaque fichier (BS.1770) et corrige ceux qui s'écartent de plus de 1,5 dB de −16 LUFS, sans réencoder (gain des trames, par pas de 1,5 dB, montée limitée par l'écrêtage).

### Boîtier

- Câblage: les fils de l'encodeur passent loin de l'antenne NFC ([hardware/electronics.md](hardware/electronics.md)). Nouvelle page [docs/problemes-connus.md](docs/problemes-connus.md): symptômes déjà rencontrés, cause et remède.

## 1.3.0 - 2026-11-04

### Firmware

- Home Assistant (désactivé par défaut): la boîte s'annonce par MQTT avec la découverte automatique (état, histoire en cours, volume borné aux limites de la boîte, bouton « Arrêter », carte SD, verrouillage, signal Wi-Fi). Chaque carte posée devient une étiquette Home Assistant, le retrait d'une carte un déclencheur; les cartes maîtresses ne sont jamais envoyées. Un arrêt depuis Home Assistant garde le signet et ne relance pas l'histoire tant que la carte reste posée. Client MQTT dans une tâche du cœur 0, sans effet sur la lecture; une histoire ne dépend jamais de Home Assistant.

### Page web

- Réglages « Home Assistant »: activation, adresse (`homeassistant.local` par défaut, adresse de la page Home Assistant acceptée), utilisateur et mot de passe MQTT (envoyé chiffré), état de la liaison. Une lecture arrêtée depuis Home Assistant s'affiche « Arrêtée ».

### Développement

- Variante sans connexion vers l'extérieur: `make firmware OFFLINE=1` (environnement `totobox-s3-offline`, `TOTOBOX_OFFLINE`) exclut de la compilation la mise à jour en ligne (GitHub, NTP) et Home Assistant. La page en cache les réglages (`features` dans `/api/status`). Faux serveur: `make mock OFFLINE=1`. Les Releases la fournissent aussi (`totobox-X.Y.Z-hors-ligne-complet.bin` et `-hors-ligne-firmware.bin`).
- Atelier de rendu du boîtier (`hardware/tools/render/`, `make render`): rendus three.js du modèle Onshape, base des illustrations du README.

### Boîtier

- DXF: calque GRAVURE avec la croix de centrage de chaque trou et de chaque arrondi et le nom de chaque pièce, gravés sur la face intérieure.
- Version à créneaux: les trous du fond, à percer à la main, sont marqués d'une croix sur le calque GRAVURE et dans le gabarit PDF (avec leur foret), sans être découpés.
- Version à créneaux: couvre-LEDs à bords droits (plus de biseau à poncer), placé par un tenon à chaque bout dans une mortaise inclinée de la cloison et du côté droit (Feature Onshape `led_cover.fs`).
- Plan de coupe à la scie de la version manuelle (`hardware/laser/totobox_manuel_plan_de_coupe.pdf`, généré par `make cad`), pour découper sans gabarit imprimé: ordre des traits et leurs cotes (bandes horizontales, puis traits verticaux, puis recoupes), finitions et coordonnées des perçages.

## 1.2.1 - 2026-10-28

### Firmware

- Mise à jour en ligne: la page enregistrée dans LittleFS est relue (taille et empreinte) avant d'installer le firmware. Une écriture incomplète passait inaperçue et laissait une page tronquée, bloquée sur « Connexion… » (vu en passant à 1.2.0; réparer avec `make uploadfs`). La place libre est vérifiée avant l'écriture et journalisée.
- Console plus lisible: plus d'erreur du framework pour un fichier absent pendant une mise à jour ou au démarrage, et seules les erreurs du framework sont affichées (ses messages se mélangeaient aux lignes de la boîte).

## 1.2.0 - 2026-10-28

### Firmware

- Mise à jour automatique (réglage `autoUpdate`, désactivé par défaut): une fois par jour, la boîte cherche une nouvelle version sur GitHub et l'installe, connectée au Wi-Fi de la maison et après une minute sans histoire ni carte posée, réveil compris. Heure par NTP, date de la dernière recherche gardée en NVS.

### Page web

- Réglages « Être informé des mises à jour de la TotoBox » (désactivé: la page ne contacte plus GitHub) et « Procéder automatiquement aux mises à jour lorsque possible », dans les réglages et dans une étape de l'assistant, juste avant le Wi-Fi.
- Une mise à jour lancée par la boîte (ou depuis un autre téléphone) ouvre la fenêtre de mise à jour avec son avancement, comme une mise à jour lancée à la main.

## 1.1.0 - 2026-10-28

### Firmware

- Mise à jour en ligne (`POST /api/update`): la boîte télécharge une version publiée sur GitHub (HTTPS, autorités racines de Mozilla), vérifie l'empreinte de la page et du firmware, les installe (partition OTA inactive) et redémarre, en gardant histoires, cartes et réglages. Seules les versions plus récentes du dépôt officiel sont acceptées.

### Page web

- À l'ouverture, puis une fois par jour, la page cherche une nouvelle version sur GitHub; bandeau « Nouvelle version de la boîte », nouveautés et installation en un clic avec l'avancement, aussi depuis les réglages.

### Développement

- Les Releases fournissent la page seule (`totobox-X.Y.Z-page.html.gz`) et, dans leurs notes, les empreintes lues par la page pour la mise à jour en ligne.

- Publication automatisée: `make release VERSION=X.Y.Z` (version, CHANGELOG, commit, étiquette), puis la Release GitHub est créée à l'arrivée de l'étiquette, avec les notes du CHANGELOG et les fichiers à télécharger: image complète à flasher sans PlatformIO, firmware et page web pour une mise à jour, fichiers de découpe.

## 1.0.1 - 2026-10-28

Corrections et tests; aucun changement de l'API ni des réglages.

### Firmware

- Règles des chemins de la bibliothèque (`library_paths`) et décisions de reprise de lecture (`playback_rules`) extraites dans des modules testés sur ordinateur (29 nouveaux cas).
- Le dossier `/content` lui-même ne peut plus être envoyé à la corbeille.
- Suivi des cartes (`card_flow`), volume (`volume_rules`), LEDs (`led_layout`) et reconnexion Wi-Fi (`retry_delay`) extraits et testés sur ordinateur (24 nouveaux cas).
- Carte remplacée par une autre en moins de 250 ms: l'histoire en cours est arrêtée et son signet enregistré avant de lire la nouvelle carte. Avant, le signet était perdu, et avec une carte maîtresse l'histoire continuait sans carte posée.

### Page web

- Fonctions pures de la page dans `web/lib.js`, testées par `make test-web` (`node --test`, 10 cas): échappement HTML, formats, noms réseau et de dossiers, retrait ID3, cartes qui suivent un fichier déplacé.

## 1.0.0 - 2026-10-28

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
