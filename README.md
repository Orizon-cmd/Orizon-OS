# Orizon OS

Orizon OS est maintenant un projet autonome et personnel. Ce dépôt est la
source d’autorité du système: il n’y a plus de synchronisation prévue avec un
ancien OS ou un dépôt amont externe.

## Direction actuelle

Le point d’entrée actif est `orizon-os-x86_64`, recentré en base minimale pour
le développement noyau:

- démarrage stable en VM et sur cible `x86_64` UEFI
- interface framebuffer simple avec splash `Orizon OS`
- une seule console centrale pour travailler proprement
- espace `/workspace` persistant en VM, avec `/system` et `/tmp` en base minimale
- mise à jour de la VM sans réinstallation complète

Ce qui est volontairement absent du profil actif:

- gestionnaire de fichiers intégré
- bureau de démonstration
- jeux et applications intégrées non essentielles
- flux de mise à jour hérité d’un ancien projet

## Boucle de travail VM

Le cycle le plus rapide aujourd’hui passe par le serveur ZimaOS:

```powershell
python scripts/orizon/build_x86_64_on_zimaos.py --deploy-vm
powershell -File scripts/orizon/open_orizon_vnc.ps1
```

La première commande reconstruit `orizon-os-x86_64` dans Docker sur ZimaOS,
déploie le résultat sur `orizon-dev`, puis redémarre la VM si nécessaire. La
seconde ouvre la console VNC avec TigerVNC en profil rapide.

## Arborescence utile

- `orizon-os-x86_64/` : noyau et image de démarrage `x86_64`
- `docs/orizon/` : notes de projet et labo ZimaOS
- `scripts/orizon/` : build, déploiement VM, accès VNC et SSH
- `config/hosts/*.local.*` : secrets locaux ignorés par Git

## Base active `x86_64`

Le socle actif vise un noyau propre, stable et facile à faire évoluer. Il
privilégie la lisibilité, le contrôle du boot, et une surface réduite pour
réintroduire les fonctionnalités plus tard, uniquement si elles servent la
vision d’Orizon OS.

Pour les détails de build locaux, voir
[orizon-os-x86_64/README.md](orizon-os-x86_64/README.md).
