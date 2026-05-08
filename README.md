# Orizon OS

Orizon OS est maintenant un projet autonome et personnel. Ce depot est la
source d'autorite du systeme: il n'y a plus de synchronisation prevue avec un
autre OS ou un depot amont externe.

## Direction actuelle

Le point d'entree actif est `orizon-os-x86_64`, recentre en base minimale pour
le developpement noyau:

- demarrage stable en VM et sur cible `x86_64` UEFI
- interface framebuffer simple avec splash `Orizon OS`
- une seule console centrale pour travailler proprement
- espace `/workspace` persistant quand une zone donnees Orizon est disponible
- installateur disque guide avec langue, clavier, GPT, ESP FAT32 et boot UEFI
- layout clavier persistant `fr-azerty` ou `us-qwerty` applique au boot
- commande `update` interne, disponible seulement apres installation disque,
  qui telecharge le manifeste GitHub, verifie les artefacts SHA-256 et reecrit
  les fichiers de boot installes
- console avec scrollback et support molette souris PS/2
- timer noyau PIT 100 Hz, uptime reel, boucle idle `hlt` pour eviter le CPU a 100%
- debut de table processus/scheduler visible avec `ps`

Ce qui est volontairement absent du profil actif:

- gestionnaire de fichiers integre
- bureau de demonstration
- jeux et applications integrees non essentielles
- flux de mise a jour externe

## Installation Disque

Le prochain axe prioritaire est l'installation sur disque. Depuis la console:

```text
install
```

L'assistant demande la langue, le clavier, le mode disque et le hostname, puis
peut installer Orizon OS sur le disque cible. Le mode `guided-full-disk` ecrit
une GPT, formate une ESP FAT32, copie `BOOTX64.EFI`, `kernel.elf` et
`limine.conf`, puis conserve une partition data Orizon pour `/workspace`.
Avant l'ecriture disque, `/workspace` est synchronise pour garder les dossiers
et fichiers crees pendant le live boot.

Apres une installation reussie, Orizon OS marque le disque comme installe,
affiche une consigne de retrait/ejection de l'ISO ou de la cle USB, puis lance
un shutdown. Au boot suivant, la commande `install` est bloquee pour proteger
le disque et les donnees.

La premiere version cible le cas le plus utile pour le labo et les machines
UEFI simples: un disque AHCI/SATA, une ESP de 1 MiB a 512 MiB, et une partition
data Orizon a partir de 512 MiB. Les installations multi-disques, dual-boot et
rollback A/B arriveront ensuite.

Pour revoir le plan:

```text
install-status
```

Pour verifier le layout clavier actif:

```text
keyboard
```

Details: [docs/orizon/INSTALL.md](docs/orizon/INSTALL.md).

## Update Dans Orizon OS

La commande `update` est volontairement reservee a un Orizon OS installe sur
disque. En live-boot, elle n'apparait pas dans `help` et refuse de demarrer si
quelqu'un la tape quand meme, parce qu'un ISO demarre en lecture seule ne peut
pas se modifier lui-meme proprement.

Dans la console Orizon OS:

```text
update
```

La commande lance une transaction interne, facon `apt full-upgrade`, sans
programme externe: preparation de la base packages, probe Ethernet Intel
`e1000/e1000e`, DHCP, DNS, TCP, TLS vers GitHub, telechargement du manifeste
public, telechargement des artefacts par requetes HTTP `Range`, verification
SHA-256, puis reecriture de l'ESP installee avec le nouveau `kernel.elf`,
`BOOTX64.EFI` et `limine.conf`. La partition data Orizon et `/workspace` sont
preserves.

Le depot public GitHub est la source officielle:

```text
https://github.com/Orizon-cmd/Orizon-OS
```

Le manifeste lu par le noyau se trouve ici:

```text
updates/x86_64/manifest.txt
```

La transaction ecrit ses etats et journaux ici:

```text
/workspace/.orizon/update.log
/workspace/.orizon/update-state
/workspace/.orizon/update-manifest
/workspace/.orizon/last-update
/system/installed
```

Apres une mise a jour reussie, il suffit de redemarrer pour booter sur le
payload installe rafraichi.

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
