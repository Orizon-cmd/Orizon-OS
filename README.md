# Orizon OS

Orizon OS est maintenant un projet autonome et personnel. Ce depot est la
source d'autorite du systeme: il n'y a plus de synchronisation prevue avec un
autre OS ou un depot amont externe.

## Direction actuelle

Le point d'entree actif est `orizon-os-x86_64`, recentre en base minimale pour
le developpement noyau:

- demarrage stable en VM et sur cible `x86_64` UEFI
- interface framebuffer simple avec splash `Orizon OS`
- une seule console centrale avec historique persistant et autocompletion simple
- racines data persistantes `/workspace`, `/home`, `/system`, `/packages` et
  `/logs` quand une zone donnees Orizon est disponible
- installateur disque guide avec langue, clavier, GPT, ESP FAT32, verification
  du boot UEFI, selection explicite du disque cible et reparation de l'ESP
- layout clavier persistant `fr-azerty` ou `us-qwerty` applique au boot
- pilotes materiel elargis: clavier USB HID plus propre, stockage AHCI/NVMe,
  Ethernet Intel e1000/e1000e, Realtek RTL8139 et VirtIO-net pour Proxmox/QEMU
- commande `update` interne, disponible seulement apres installation disque,
  qui telecharge le manifeste GitHub, verifie les artefacts SHA-256 et reecrit
  les fichiers de boot installes
- mini gestionnaire de paquets `pkg` avec format texte `.opkg`, SHA-256 du
  payload, installation de fichiers et script post-install minimal
- depot officiel de paquets GitHub `Orizon-Packages`, lu par `update` pour
  installer des composants separes du kernel
- console avec scrollback, support molette souris PS/2, `edit` ameliore et
  navigation historique `Up/Down`
- diagnostics `sysinfo`, `hw`, `mounts` et `report` pour voir CPU, memoire,
  stockage, racines data, reseau, USB/PS2, installation, update et principaux
  peripheriques PCI
- inspection stockage avec `disks`, `storage detail` et selection du disque
  actif via `storage select <n>`
- journal noyau en memoire avec `dmesg`, lecture des journaux via `logs` et
  rapport compact `report`; apres installation, le boot log est conserve dans
  `/logs/boot.log`
- timer noyau PIT 100 Hz, uptime reel, boucle idle `hlt` pour eviter le CPU a 100%
- debut de table processus/scheduler visible avec `ps`
- timer LAPIC/APIC sur machines UEFI modernes, avec fallback PIT puis polling
  de diagnostic si aucune IRQ timer ne parvient au shell

Ce qui est volontairement absent du profil actif:

- gestionnaire de fichiers integre
- bureau de demonstration
- jeux et applications integrees non essentielles
- flux de mise a jour externe

## Installation Disque

L'installation sur disque se lance depuis la console:

```text
install
```

L'assistant demande la langue, le clavier, le mode disque et le hostname, puis
peut installer Orizon OS sur le disque cible. Le flux affiche les disques
detectes (`disk0`, `disk1`, etc.) avec type, taille et modele, puis demande une
confirmation destructive sous la forme `ERASE disk0`. Le mode
`guided-full-disk` ecrit une GPT, formate une ESP FAT32, copie `BOOTX64.EFI`, `kernel.elf` et
`limine.conf`, puis conserve une partition data Orizon pour `/workspace` et
les racines `/home`, `/system`, `/packages` et `/logs`.
Avant l'ecriture disque, `/workspace` est synchronise pour garder les dossiers
et fichiers crees pendant le live boot.

Apres une installation reussie, Orizon OS marque le disque comme installe,
affiche une consigne de retrait/ejection de l'ISO ou de la cle USB, puis lance
un shutdown. Au boot suivant, la commande `install` est bloquee pour proteger
le disque et les donnees.

L'installateur verifie maintenant le boot installe avant de marquer le disque
comme pret: MBR protecteur, GPT, ESP FAT32, label, `EFI/BOOT/BOOTX64.EFI`,
`boot/kernel.elf` et les configurations Limine. Pour refaire ce diagnostic:

```text
boot-check
```

Si le disque a deja une installation Orizon mais que l'ESP est abimee, la
commande suivante reecrit uniquement les fichiers de boot et preserve la
partition data:

```text
repair-boot
```

## Reseau Bridge Et Proxmox

Orizon OS peut utiliser une carte reseau VM en NAT ou en bridge. Pour Proxmox,
la configuration recommandee est:

```text
Bridge: vmbr0
Model: VirtIO (paravirtualized)
DHCP: active sur le LAN
```

Le bridge ne remplace pas DHCP: il branche simplement la VM sur le meme reseau
que l'hote. Si le reseau local ne distribue pas d'adresse, Orizon peut utiliser
une IP statique persistante dans `/system/network.conf`.
Dans Orizon, `net` affiche le pilote detecte, `net dhcp` teste l'obtention
d'une adresse IPv4 sans lancer une mise a jour, et `net auto` tente DHCP puis
la configuration statique si DHCP echoue.

Exemple IP statique:

```text
net config ip 192.168.1.50 gateway 192.168.1.1 dns 192.168.1.1
net auto
ping 8.8.8.8
dns raw.githubusercontent.com
route
logs network
```

La configuration est sauvegardee dans `/system/network.conf` et le journal
reseau dans `/logs/network.log`, donc une machine Proxmox en bridge sans NAT
peut rester connectee a GitHub si son LAN autorise la passerelle et le DNS.
En cas de machine Proxmox configuree en VirtIO moderne-only, choisir le modele
`Intel E1000` reste un fallback compatible.

Details: [docs/orizon/NETWORK.md](docs/orizon/NETWORK.md).

La premiere version cible le cas le plus utile pour le labo et les machines
UEFI simples: un disque AHCI/SATA ou NVMe 512-byte LBA, une ESP de 1 MiB a
512 MiB, et une partition data Orizon a partir de 512 MiB. Les installations
multi-disques, dual-boot et boot-count automatique arriveront ensuite.

Pour revoir le plan:

```text
install-status
```

Pour verifier le layout clavier actif:

```text
keyboard
```

Pour inspecter ou changer le disque actif avant diagnostic/reparation:

```text
disks
storage detail
storage select 1
```

Details: [docs/orizon/INSTALL.md](docs/orizon/INSTALL.md).

## Boot Sur Vrai Materiel

Si le splash `Orizon OS` apparait sur un PC portable mais que la console ne
s'ouvre jamais, le kernel est bien lance. Orizon tente maintenant le timer
LAPIC/APIC via ACPI MADT, puis retombe sur PIT, puis sur polling de diagnostic
si aucune IRQ timer n'arrive au shell.

Apres boot, verifier:

```text
sysinfo
report
hw
pci
input
```

Le premier portable cible documente est le Lenovo 500w Yoga Gen 4. Son clavier
interne passe par PS/2, son SSD par NVMe, et son pave tactile ELAN/Wacom passe
par I2C-HID. Orizon contient maintenant une premiere sonde Intel LPSS/I2C-HID
pour ce chemin, avant le parseur multitouch complet.

Details:
[docs/orizon/HARDWARE_BOOT.md](docs/orizon/HARDWARE_BOOT.md) et
[docs/orizon/LAPTOP_HARDWARE.md](docs/orizon/LAPTOP_HARDWARE.md).

## Paquets Orizon

Orizon OS contient maintenant une premiere base de gestionnaire de paquets.
Le format est volontairement simple: un fichier texte `.opkg` contient `name`,
`version`, un `sha256` du payload, des blocs `file` a installer, puis un
bloc `post-install` minimal.

Commandes disponibles:

```text
pkg list
pkg status
pkg info orizon-hello
pkg sample
pkg hash /workspace/packages/orizon-hello.opkg
pkg install /workspace/packages/orizon-hello.opkg
pkg remove orizon-hello
```

`pkg install` et `pkg remove` sont reserves a un OS installe sur disque. Les
paquets installes sont stockes dans `/workspace/.orizon/pkgdb`, puis rejoues
au boot pour restaurer les fichiers systeme en RAM comme `/system/share/...`.
`pkg info <name>` affiche les metadonnees et fichiers possedes par un paquet.

Le depot officiel de paquets est:

```text
https://github.com/Orizon-cmd/Orizon-Packages
```

Details: [docs/orizon/PACKAGES.md](docs/orizon/PACKAGES.md).

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
`e1000/e1000e`, `RTL8139` ou `VirtIO-net`, configuration IPv4 DHCP avec fallback
IP statique, DNS, TCP, TLS vers GitHub, telechargement du manifeste public,
telechargement des artefacts par requetes HTTP `Range`, verification
SHA-256, puis reecriture de l'ESP installee avec le nouveau `kernel.elf`,
`BOOTX64.EFI` et `limine.conf`, puis lecture de l'index public
`Orizon-Packages` pour installer ou mettre a jour les paquets `.opkg`. La
partition data Orizon et `/workspace` sont preserves.

Pendant l'operation, la console affiche les etapes en continu: etat courant,
manifest recu, progression par pourcentage sur chaque artefact, verification
SHA-256 et ecriture de l'ESP. L'ecran ne reste donc plus silencieux jusqu'a la
fin de la transaction. Les timings par etape sont aussi sauvegardes dans
`/workspace/.orizon/update.log`.

Avant de remplacer le payload principal, Orizon garde le kernel et le loader
actuellement demarres dans un slot rollback sur l'ESP:

```text
/boot/KROLLBK.ELF
/EFI/BOOT/BOOTX64.ROL
```

Le menu Limine contient ensuite une entree `Orizon OS Rollback`. Si une mise a
jour boote mal, cette entree permet de redemarrer sur l'ancien payload. Une
fois dans ce slot, la commande suivante restaure le payload demarre comme slot
principal:

```text
rollback
```

Pour consulter les metadonnees du rollback:

```text
rollback-status
```

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
/workspace/.orizon/package-index
/workspace/.orizon/last-update
/workspace/.orizon/pkgdb
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
Les modes de build rafraichissent aussi `updates/x86_64/` pour que la commande
`update` dans Orizon recoive le meme kernel que l'ISO publiee.

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
