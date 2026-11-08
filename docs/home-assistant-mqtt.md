# Relier la TotoBox à Home Assistant, pas à pas

Ce guide part d'une installation **Home Assistant OS** (HAOS) de base, sans rien de configuré pour MQTT. Il faut compter une quinzaine de minutes. À la fin, la boîte apparaît dans Home Assistant, chaque carte posée peut déclencher une automatisation, et Home Assistant peut régler le volume ou arrêter l'histoire.

La boîte n'a jamais besoin de Home Assistant pour lire une histoire: si quelque chose ne marche pas ici, l'enfant ne s'en aperçoit pas.

## Ce qu'il faut

- Home Assistant OS installé et accessible depuis un navigateur (en général `http://homeassistant.local:8123`), avec un compte **administrateur**.
- La TotoBox connectée au **même Wi-Fi** que Home Assistant (Réglages de la boîte > Réseau > Wi-Fi de la maison), avec un firmware qui propose la section « Home Assistant » dans ses réglages.
- Le téléphone ou l'ordinateur sur ce même réseau, pour ouvrir les deux pages.

## Le principe en deux phrases

MQTT est une sorte de messagerie locale: les appareils y déposent des messages et s'abonnent à ceux qui les intéressent. Le **broker** est le facteur (ici le module Mosquitto, installé dans Home Assistant); la TotoBox et Home Assistant s'y connectent tous les deux, et Home Assistant crée tout seul l'appareil TotoBox grâce aux messages de « découverte » que la boîte y dépose.

Trois choses sont donc à mettre en place: un compte pour la boîte, le broker, et l'intégration MQTT qui relie Home Assistant au broker.

## 1. Créer un compte pour la boîte

Le broker de Home Assistant accepte les comptes utilisateurs de Home Assistant. On en crée un, réservé à la boîte, sans droits d'administration.

1. Afficher les réglages avancés: cliquer sur son nom en bas à gauche (profil), puis activer **Mode avancé**. Sans lui, l'onglet des utilisateurs n'apparaît pas.
2. Aller dans **Paramètres > Personnes**, onglet **Utilisateurs**, puis **Ajouter un utilisateur**.
3. Remplir:
   - Nom d'affichage: `TotoBox`;
   - Nom d'utilisateur: `totobox` (pas `homeassistant` ni `addons`, réservés par le broker);
   - un mot de passe, à noter: il servira à l'étape 5;
   - activer **Connexion uniquement depuis le réseau local**;
   - laisser **Administrateur** désactivé.
4. **Créer**.

Plusieurs boîtes peuvent partager ce compte.

## 2. Installer le broker Mosquitto

1. Aller dans **Paramètres > Modules complémentaires** (appelés « Applications » dans les versions récentes), puis **Boutique des modules complémentaires** (bouton en bas à droite).
2. Chercher **Mosquitto broker** (module officiel), l'ouvrir, puis **Installer**. L'installation prend une à deux minutes.
3. Une fois installé, activer **Démarrer au démarrage** et **Chien de garde** (Watchdog), puis cliquer sur **Démarrer**.
4. Vérifier dans l'onglet **Journal** du module que le démarrage s'est bien passé (pas de ligne `Error` à la fin).

Rien d'autre à régler dans le module: les options par défaut conviennent. Le broker écoute sur le port **1883** du réseau local. Ne pas ouvrir ce port vers Internet dans la box.

## 3. Ajouter l'intégration MQTT

En général, Home Assistant la propose de lui-même juste après le démarrage de Mosquitto:

1. Aller dans **Paramètres > Appareils et services**.
2. En haut, dans **Découvert**, une carte **MQTT** apparaît: **Ajouter** (ou **Configurer**), puis **Valider**.

Si rien n'apparaît après une minute: **Ajouter une intégration**, chercher **MQTT**, puis choisir l'option qui utilise le module Mosquitto (et non la saisie manuelle d'un broker).

L'intégration **MQTT** figure maintenant dans la liste des intégrations, sans appareil pour l'instant.

## 4. Trouver l'adresse de Home Assistant

La boîte essaie `homeassistant.local` par défaut, ce qui marche sur la plupart des réseaux. Pour être tranquille, mieux vaut noter aussi l'adresse IP:

- **Paramètres > Système > Réseau**: l'adresse **IPv4** de la carte réseau, par exemple `192.168.1.10`.

Conseil: dans l'interface de la box Internet, réserver cette adresse à Home Assistant (« bail DHCP statique » ou « adresse fixe »), pour qu'elle ne change pas après une coupure de courant.

## 5. Régler la TotoBox

1. Ouvrir la page de la boîte (`http://totobox.local`, ou le nom choisi), puis **Réglages**, onglet **Réseau** (déverrouiller d'abord si le verrouillage est actif).
2. Dans la section **Home Assistant**, activer **Relier la boîte à Home Assistant**.
3. **Adresse de Home Assistant**: laisser `homeassistant.local`, ou mettre l'adresse IP notée à l'étape 4. Coller l'adresse de la page Home Assistant (`http://192.168.1.10:8123`) marche aussi: la boîte en garde seulement l'adresse.
4. **Utilisateur MQTT** et **Mot de passe MQTT**: ceux du compte créé à l'étape 1.
5. **Enregistrer le réseau**.

Après quelques secondes, rouvrir les réglages: sous les champs, la page doit afficher « Connectée à Home Assistant. ». Sinon, voir [Dépannage](#dépannage).

## 6. Vérifier dans Home Assistant

**Paramètres > Appareils et services > MQTT**: un appareil portant le nom réseau de la boîte (`totobox` par défaut) apparaît. Il contient:

| Entité | Rôle |
|---|---|
| État | Prête, En lecture, En pause, Arrêtée (`stopped`), Carte inconnue, Erreur |
| Histoire | nom de l'histoire en cours, avec la carte, la piste et la position en attributs |
| Volume | curseur, limité aux bornes réglées dans la boîte |
| Arrêter | bouton qui coupe l'histoire en cours |
| Carte SD, Réglages, Signal Wi-Fi | diagnostic (le signal Wi-Fi est désactivé par défaut) |

Essai rapide: poser une carte sur la boîte, l'état passe à « En lecture » et l'histoire s'affiche. Bouger le curseur du volume: les LEDs de la boîte montrent la jauge.

Quand la boîte dort (5 minutes sans histoire), ses entités passent **indisponibles**: c'est normal. Elles reviennent dès qu'une carte ou la molette la réveille.

## 7. Donner un nom aux cartes

Chaque carte posée sur la boîte est envoyée à Home Assistant comme une **étiquette** (tag NFC). Les cartes maîtresses du verrouillage ne le sont jamais.

1. Poser une carte sur la boîte.
2. Aller dans **Paramètres > Zones, étiquettes et personnes**, onglet **Étiquettes**: la carte y figure, avec son identifiant (`04A1B2C3`…) et l'heure de sa dernière lecture.
3. Cliquer dessus et lui donner un nom parlant (« Les trois petits cochons »).

Recommencer pour chaque carte qui doit déclencher quelque chose.

## 8. Première automatisation

Exemple: allumer la veilleuse de la chambre en orange doux quand l'histoire du soir est posée.

1. Depuis l'étiquette (étape 7), cliquer sur **Créer une automatisation**: le déclencheur « Étiquette scannée » est déjà rempli.
2. **Ajouter une action** > **Lumière** > **Allumer**, choisir la veilleuse, régler la luminosité (20 %) et la couleur.
3. **Enregistrer**, nommer l'automatisation.

La même automatisation en YAML (Modifier en YAML), à adapter:

```yaml
alias: Veilleuse pour l'histoire du soir
triggers:
  - trigger: tag
    tag_id: 04A1B2C3
actions:
  - action: light.turn_on
    target:
      entity_id: light.veilleuse_chambre
    data:
      brightness_pct: 20
      color_name: orange
```

D'autres idées:

- **Arrêter l'histoire à 20 h 30**: déclencheur « Heure », action « Bouton: Appuyer » sur l'entité **Arrêter** de la boîte. La boîte garde le signet et ne relance rien tant que la carte reste posée; la retirer puis la reposer reprend l'histoire.
- **Baisser le volume le soir**: action « Nombre: Définir la valeur » sur l'entité **Volume**. La valeur est toujours ramenée dans les bornes de la boîte: Home Assistant ne peut pas monter plus fort que ce que les parents ont autorisé.
- **Éteindre la veilleuse quand la carte est retirée**: déclencheur « Appareil », choisir la boîte, puis le déclencheur de retrait de carte.

Les noms exacts des entités (`button.totobox_arreter`, `number.totobox_volume`…) se lisent dans la fiche de l'appareil, onglet de chaque entité, réglages (roue dentée).

## Dépannage

Le message affiché sous les champs, dans les réglages de la boîte, indique où chercher:

| Message de la boîte | Cause probable | Que faire |
|---|---|---|
| Utilisateur ou mot de passe MQTT refusé par le broker. | utilisateur ou mot de passe faux | ressaisir l'utilisateur et le mot de passe de l'étape 1; vérifier le journal du module Mosquitto |
| Aucun broker MQTT ne répond à cette adresse. | Mosquitto arrêté ou pas installé, mauvaise adresse | étape 2 (le module est-il démarré?), étape 4 (adresse IP) |
| Adresse introuvable sur le réseau. | `homeassistant.local` non résolu sur ce réseau | mettre l'adresse IP de l'étape 4 |
| La boîte n'est pas connectée au Wi-Fi de la maison. | boîte en point d'accès de secours | régler le Wi-Fi de la maison dans la boîte |
| Adresse illisible. | faute de frappe dans l'adresse | corriger: nom, IP, ou adresse de la page Home Assistant |

Autres cas:

- **« Connectée » mais aucun appareil dans Home Assistant**: l'intégration MQTT n'est pas ajoutée (étape 3), ou la découverte y a été désactivée (Paramètres > Appareils et services > MQTT > Configurer > Options: « Activer la découverte » doit être coché).
- **Une carte n'apparaît pas dans les étiquettes**: c'est une carte maîtresse (jamais envoyée), ou la boîte n'était pas encore connectée (une carte qui réveille la boîte n'est envoyée que si la connexion arrive dans la minute).
- **Voir ce que la boîte envoie**: Paramètres > Appareils et services > MQTT > **Configurer**, section **Écouter un sujet**, saisir `totobox/#` puis **Commencer l'écoute**, et poser une carte.

## Délier la boîte

Désactiver **Relier la boîte à Home Assistant** dans les réglages de la boîte (onglet Réseau), puis **Enregistrer le réseau**: la boîte retire elle-même son appareil de Home Assistant. Après une remise à zéro de la boîte, l'appareil reste « indisponible » dans Home Assistant: le supprimer depuis sa fiche (menu ⋮ > Supprimer).

Pour aller plus loin (sujets MQTT, contenu des messages, choix techniques): [home-assistant.md](home-assistant.md).
