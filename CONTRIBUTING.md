# Contribuer

Merci de l'intérêt pour TotoBox! Questions, retours de fabrication, photos de boîtes, corrections et nouvelles fonctions sont les bienvenus, en français ou en anglais.

## Signaler un problème

Ouvrir une *issue* en précisant:

- la version du firmware (Réglages, ligne « Firmware », ou première ligne de la console série), et la version du boîtier si c'est un problème matériel (à créneaux ou faite à la main);
- ce qui était attendu et ce qui s'est passé, avec les LEDs observées;
- si possible, la sortie de la console série (`pio device monitor`): chaque ligne est préfixée par le service concerné (`audio`, `nfc`, `net`…).

Ne pas joindre de mot de passe Wi-Fi ni le contenu de `production/`.

Pour une faille de sécurité, voir [SECURITY.md](SECURITY.md) plutôt qu'une issue publique.

## Proposer une modification

1. Lire [docs/development.md](docs/development.md) (outils, tests) et [AGENTS.md](AGENTS.md) (règles de code et pièges connus): la plupart des règles viennent de problèmes réels rencontrés sur la boîte.
2. Garder une modification par sujet, la plus petite possible qui fonctionne de bout en bout.
3. Vérifier avant d'ouvrir la *pull request*:
   - `make firmware` et, si `web/` a changé, `make webfs`;
   - `make test` (tests sur ordinateur);
   - pour une modification du boîtier: `make cad`, et les DXF et PDF régénérés sont inclus;
   - sur la boîte, pour tout ce qui touche au matériel: les scénarios de [docs/development.md](docs/development.md#tests).
4. Décrire dans la *pull request* ce qui a été testé sur la boîte et ce qui ne l'a pas été.
5. Mettre à jour la documentation concernée et ajouter une ligne à [CHANGELOG.md](CHANGELOG.md) (section « Non publié »).

La CI (GitHub Actions) compile le firmware et la page, lance les tests, vérifie que les fichiers de découpe sont à jour et que chaque fichier a une licence (REUSE).

## Conventions

- Code, commentaires, interface, messages et documentation **en français**, comme le reste du dépôt.
- Logs série par `logLine(tag, format, …)`, jamais `Serial.print`.
- Pas de secret (mot de passe Wi-Fi, clé) dans le dépôt.
- Nouveau fichier: il est couvert par [REUSE.toml](REUSE.toml) selon son dossier. Un fichier de code commence par l'en-tête `SPDX-License-Identifier: GPL-3.0-or-later`; un fichier repris d'ailleurs garde sa licence d'origine, à déclarer dans `REUSE.toml` et `LICENSES/`.

## Licence des contributions

En proposant une contribution, vous acceptez qu'elle soit publiée sous la licence du dossier concerné: GPL-3.0-or-later pour le code, CC BY-SA 4.0 pour le matériel et la documentation (voir le [README](README.md#licences)).
