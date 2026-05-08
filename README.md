# Orizon OS

Orizon OS est maintenant un projet autonome et personnel. Ce depot est la
source d'autorite du systeme: il n'y a plus de synchronisation prevue avec un
ancien OS ou un depot amont externe.

## Direction actuelle

Le point d'entree actif est `orizon-os-x86_64`, recentre en base minimale pour
le developpement noyau:

- demarrage stable en VM et sur cible `x86_64` UEFI
- interface framebuffer simple avec splash `Orizon OS`
- une seule console centrale pour travailler proprement
- espace `/workspace` persistant quand une zone donnees Orizon est disponible
- mise a jour sans reinstallation complete de l'espace de travail
- commande `update` interne avec transaction full-upgrade et probe Ethernet
- timer noyau PIT 100 Hz, uptime reel, boucle idle `hlt` pour eviter le CPU a 100%
- debut de table processus/scheduler visible avec `ps`

Ce qui est volontairement absent du profil actif:

- gestionnaire de fichiers integre
- bureau de demonstration
- jeux et applications integrees non essentielles
- flux de mise a jour herite d'un ancien projet

## Update Dans Orizon OS

Dans la console Orizon OS:

```text
update
```

La commande lance maintenant une transaction de mise a jour interne, facon
`apt full-upgrade`: preparation de la base packages, probe Ethernet, et journal
local dans `/workspace/.orizon/update.log`. Elle ne lance pas de programme
externe.

Le driver Ethernet Intel `e1000/e1000e` est initialise au demarrage pour la VM
et les cartes compatibles. Le telechargement GitHub complet depuis l'OS demande
encore les couches reseau suivantes dans le noyau: ARP, IPv4, DHCP, DNS, TCP et
HTTPS/TLS.

## Noyau Et Performance

La VM ne doit plus tourner en boucle active permanente. Le noyau utilise un
timer PIT a 100 Hz pour l'uptime et rentre en idle avec `hlt` entre les ticks
et les evenements clavier/souris. Les commandes utiles pour verifier:

```text
uptime
free
ps
neofetch
```

## Mise A Jour Par Internet

ZimaOS est seulement le labo VM actuel. Orizon OS doit rester portable vers
d'autres machines `x86_64` UEFI.

Le depot public GitHub est la source officielle:

```text
https://github.com/Orizon-cmd/Orizon-OS
```

Pour recuperer la derniere ISO publiee depuis Internet, sans compiler:

```powershell
python scripts/orizon/orizon_update.py --mode github-iso
```

Sur une nouvelle machine, le demarrage le plus simple est:

```powershell
git clone https://github.com/Orizon-cmd/Orizon-OS.git
cd Orizon-OS
python scripts/orizon/orizon_update.py --mode github-iso
```

Pour reconstruire depuis le dernier code GitHub sur la machine courante:

```powershell
python scripts/orizon/orizon_update.py --from-github --mode local-iso
```

Ces commandes rafraichissent `Orizon-OS.iso` a la racine. Le mode `github-iso`
est le plus simple pour une machine qui veut juste recevoir une mise a jour; le
mode `--from-github --mode local-iso` sert aux machines qui ont la toolchain et
doivent reconstruire.

Backends disponibles:

- `github-iso`: telecharge l'ISO publique depuis GitHub
- `local-iso`: build local portable, pour toute machine avec clang/lld/xorriso
- `zimaos-iso`: build via Docker sur le serveur ZimaOS, puis recupere l'ISO
- `zimaos-vm`: build via ZimaOS, deploie sur la VM `orizon-dev`, puis recupere l'ISO

## Boucle De Travail VM

Le cycle le plus rapide aujourd'hui passe encore par le serveur ZimaOS:

```powershell
python scripts/orizon/orizon_update.py --from-github --mode zimaos-vm
powershell -File scripts/orizon/open_orizon_vnc.ps1
```

La premiere commande reconstruit `orizon-os-x86_64`, deploie le resultat sur
`orizon-dev`, preserve la partition donnees Orizon, puis met a jour
`Orizon-OS.iso`. La seconde ouvre la console VNC avec TigerVNC.

## Arborescence Utile

- `orizon-os-x86_64/` : noyau et image de demarrage `x86_64`
- `docs/orizon/` : notes de projet et labo ZimaOS
- `scripts/orizon/` : update portable, build, deploiement VM, acces VNC et SSH
- `config/hosts/*.local.*` : secrets locaux ignores par Git

## Base Active `x86_64`

Le socle actif vise un noyau propre, stable et facile a faire evoluer. Il
privilegie la lisibilite, le controle du boot, et une surface reduite pour
reintroduire les fonctionnalites plus tard, uniquement si elles servent la
vision d'Orizon OS.

Pour les details de build locaux, voir
[orizon-os-x86_64/README.md](orizon-os-x86_64/README.md).
