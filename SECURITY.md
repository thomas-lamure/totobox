# Sécurité

La boîte est destinée au réseau local d'une maison. Ses protections, et leurs limites, sont décrites dans [docs/api.md](docs/api.md):

- verrouillage des réglages appliqué par le firmware (cartes maîtresses, code haché et salé, limitation des essais);
- refus des requêtes d'autres sites (rebinding DNS, CSRF);
- secrets chiffrés entre la page et la boîte. Cela protège d'une écoute passive seulement: l'interface est en HTTP et ne résiste pas à quelqu'un qui s'interpose sur le réseau.

## Signaler une faille

Merci de ne pas ouvrir d'issue publique. Utiliser le signalement privé de GitHub (onglet « Security » du dépôt, « Report a vulnerability »), en décrivant la faille, comment la reproduire et la version du firmware concernée.

Il s'agit d'un projet personnel maintenu sur le temps libre: la réponse peut prendre quelques jours.
