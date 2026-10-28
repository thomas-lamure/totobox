# Sécurité

La boîte est destinée au réseau local d'une maison. Ses protections, et leurs limites, sont décrites dans [docs/api.md](docs/api.md):

- verrouillage des réglages appliqué par le firmware (cartes maîtresses, code haché et salé, limitation des essais);
- refus des requêtes d'autres sites (rebinding DNS, CSRF);
- secrets chiffrés entre la page et la boîte. Cela protège d'une écoute passive seulement: l'interface est en HTTP et ne résiste pas à quelqu'un qui s'interpose sur le réseau;
- mise à jour en ligne limitée aux versions publiées dans le dépôt officiel, plus récentes que celle de la boîte, téléchargées en HTTPS (certificat vérifié) et dont l'empreinte SHA-256 est vérifiée avant installation. Les images ne sont pas signées: la confiance repose sur le compte GitHub qui publie les versions.

## Signaler une faille

Merci de ne pas ouvrir d'issue publique. Utiliser le signalement privé de GitHub (onglet « Security » du dépôt, « Report a vulnerability »), en décrivant la faille, comment la reproduire et la version du firmware concernée.

Il s'agit d'un projet personnel maintenu sur le temps libre: la réponse peut prendre quelques jours.
