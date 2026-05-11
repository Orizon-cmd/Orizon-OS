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
- installateur disque guide avec langue, clavier, GPT, ESP FAT32, mode
  dual-boot ESP non destructif, verification du boot UEFI, selection explicite
  du disque cible et reparation de l'ESP
- layout clavier persistant `fr-azerty` ou `us-qwerty` applique au boot
- pilotes materiel elargis: clavier USB HID plus propre, stockage AHCI/NVMe,
  Ethernet Intel e1000/e1000e, Realtek RTL8139, VirtIO-net pour Proxmox/QEMU,
  et detection stage-0 du Wi-Fi Intel CNVi
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
- service `ssh` experimental: listener TCP/22, banniere SSH Orizon, paquet
  `KEXINIT`, X25519, signature hote RSA de developpement, `ECDH_REPLY`,
  `NEWKEYS`, premiere lecture/reponse chiffree `SERVICE_REQUEST` /
  `SERVICE_ACCEPT`, authentification password explicite pour `orizon`, canal
  `session`, `pty-req`, `shell`, `exec`, mini-shell distant de diagnostic,
  configuration `/system/ssh.conf`, journal `/logs/ssh.log` et diagnostics
  `ssh status`
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
peut preparer Orizon OS sur le disque cible. Le flux affiche les disques
detectes (`disk0`, `disk1`, etc.) avec type, taille et modele.

Le mode recommande pour une machine qui contient deja Windows/Linux est
`dual-boot-esp`: il detecte la GPT existante, trouve l'ESP FAT32, puis ecrit
uniquement `/EFI/Orizon/BOOTX64.EFI`, `/EFI/Orizon/kernel.elf`,
`/EFI/Orizon/limine.conf` et `/EFI/Orizon/INSTALL.TXT`. La confirmation est
`DUALBOOT disk0`. Aucune partition existante n'est reformatee et Orizon ne se
marque pas comme installe complet, donc `update` et `pkg install/remove`
restent bloques tant qu'il n'y a pas de vraie partition data Orizon.

Le mode `guided-full-disk` reste le mode complet et destructif. Il demande une
confirmation sous la forme `ERASE disk0`, ecrit une GPT, formate une ESP FAT32,
copie `BOOTX64.EFI`, `kernel.elf` et `limine.conf`, puis conserve une partition
data Orizon pour `/workspace` et les racines `/home`, `/system`, `/packages`
et `/logs`.
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

Pour verifier les fichiers side-by-side du mode dual boot:

```text
dualboot-check
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

## Acces SSH Orizon

Le service SSH se demarre explicitement depuis la console:

```text
net dhcp
ssh password <mot-de-passe>
ssh start
ssh status
ssh audit
ssh auth
ssh auth max <essais>
ssh auth lockout <secondes>
ssh auth default
ssh hostkey
ssh hostkey reload
ssh hostkey reset
ssh algorithms
ssh reload
ssh lockout clear
logs ssh
```

La commande configure IPv4 si besoin, ouvre TCP/22, ecrit la configuration
dans `/system/ssh.conf`, envoie la banniere `SSH-2.0-OrizonSSH_0.1`, negocie
`curve25519-sha256` avec `rsa-sha2-256`, signe `ECDH_REPLY`, derive les cles
AES-128-CTR/HMAC-SHA256, echange `NEWKEYS`, puis repond au premier
`SERVICE_REQUEST` chiffre par `SERVICE_ACCEPT`. L'authentification password est
desactivee tant que `ssh password <mot-de-passe>` n'a pas ete lance depuis la
console; ensuite OpenSSH peut se connecter avec `ssh orizon@<ip-orizon>`.
Le canal `session` accepte deja `pty-req`, `shell` et `exec` avec un mini-shell
de diagnostic (`help`, `ls`, `cd`, `cat`, `head`, `touch`, `mkdir`, `rm`,
`write`, `append`, `logs`, `net`, `route`, `dns`, `ps`, `pkg`, `storage`,
`free`, `timer`, `audit`, `sync`, `status`, `auth`, `hostkey`, `whoami`,
`uname`, `pwd`, `uptime`, `exit`). Les commandes admin `ssh auth`, `ssh
lockout`, `ssh password` et `ssh hostkey reload/reset` fonctionnent aussi en
commande distante directe. Le service remet l'ecoute TCP en etat apres une
session fermee, garde une protection anti-bruteforce dans `/system/ssh.conf`,
et expose `audit` / `ssh audit` pour verifier sessions, auth, commandes,
derniers evenements et fermetures de canal. Les longues sorties SSH sont
segmentees en plusieurs paquets pour eviter les coupures sur `logs ssh` ou
`cat`; les commandes `logs ssh` et `logs boot` montrent la fin du journal quand
il devient long. `ssh hostkey` affiche l'identite hote persistante stockee
dans `/system/ssh_host_rsa.key`.

Details: [docs/orizon/SSH.md](docs/orizon/SSH.md).

La premiere version cible le cas le plus utile pour le labo et les machines
UEFI simples: un disque AHCI/SATA ou NVMe 512-byte LBA. Le mode dual boot
actuel est un prepareur ESP non destructif; il faudra encore ajouter une entree
UEFI NVRAM/BCD automatique et une partition data Orizon dediee pour obtenir un
dual boot installe avec mises a jour completes.

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
wifi
```

Le premier portable cible documente est le Lenovo 500w Yoga Gen 4. Son clavier
interne passe par PS/2, son SSD par NVMe, et son pave tactile ELAN/Wacom passe
par I2C-HID. Orizon contient maintenant une premiere sonde Intel LPSS/I2C-HID
pour ce chemin, avant le parseur multitouch complet. Le Wi-Fi Intel CNVi est
detecte par `wifi status`; `wifi firmware`, `wifi apm`, `wifi boot arm`,
`wifi alive`, `wifi queues arm`, `wifi context arm`, `wifi scheduler arm`,
`wifi rx poll`, `wifi command arm`, `wifi nvm arm`, `wifi nvm-info arm`
et `wifi bringup`
couvrent maintenant la chaine de
diagnostic
firmware: presence du blob Intel, reveil APM du controleur, release CPU
firmware, transfert FH DMA garde, attente du signal firmware alive, puis
preparation des rings commande/RX/TX cote hote, du context-info firmware,
des anneaux message MTR/MCR, de la premiere trame de commande scheduler, du
polling RX de reponse firmware, du doorbell commande explicite et d'une
premiere lecture NVM cache/capacites radio firmware. `wifi bringup` lance
la sequence complete et indique la premiere etape bloquante. `wifi command`
affiche aussi les snapshots avant/apres, les mots bruts RX/completion et les
valeurs TFD/byte-count pour diagnostiquer un blocage sur vrai materiel.
`wifi scan` prepare maintenant un plan de scan passif et `wifi scan arm` tente
la premiere requete UMAC scan minimale. `wifi scan poll` lit ensuite les
notifications firmware UMAC de debut, iteration et fin de scan pour verifier
que la carte parcourt bien les canaux, avec un premier tableau `result[...]`
indiquant canal, bande, statut probe et duree d'ecoute par canal. Le poll RX
surveille aussi les premieres trames beacon/probe-response pour remplir une
table `ap[...]` avec SSID, BSSID, canal, securite detectee et source de
detection. Si aucun AP ne sort encore, `wifi scan poll` affiche maintenant un
bloc `mpdu-debug` avec les octets bruts et offsets candidats pour corriger le
parseur sur vrai materiel.
`wifi connect <ssid> [password]` peut ensuite selectionner un AP scanne et
preparer les trames 802.11 open-system authentication + association request,
avec template RSN WPA2-PSK si un mot de passe est fourni. Pour WPA2, Orizon
derive aussi la PMK par PBKDF2-HMAC-SHA1 sans afficher la cle; `wifi crypto`
verifie les vecteurs SHA-1/PBKDF2, AES key unwrap et AES-CCM integres. Les
passphrases 8-63 caracteres et les PSK hexadecimales 64 caracteres sont
acceptees. Le chemin RX reconnait deja
les reponses authentication/association correspondant au plan de connexion et
stocke leurs status codes. `wifi tx [auth|assoc|m2|m4|data|all]`
prepare maintenant les trames de gestion dans les buffers DMA TX et affiche le
doorbell prevu sans l'ecrire. Le chemin RX detecte aussi les trames
EAPOL-Key WPA2, capture l'ANonce, derive un PTK de diagnostic, prepare une
reponse M2 inspectable avec `wifi wpa`, puis prepare aussi M3/GTK/M4 quand les
trames EAPOL suivantes arrivent. `wifi tx m2` et `wifi tx m4` peuvent placer ces
reponses WPA en DMA. `wifi txcmd [auth|assoc|m2|m4|data]` construit aussi une
enveloppe Intel `TX_CMD` v10 de diagnostic dans un buffer separe, puis peut
l'envoyer avec `arm` si le contexte, RX et le binding STA sont prets.
`wifi bind` prepare maintenant les enveloppes diagnostiques `MAC_CONFIG`,
`LINK_CONFIG` et `STA_CONFIG` avec un `sta-id` AP local; apres cela,
`wifi bind arm` peut les envoyer une par une dans la queue commande avec ACK
firmware strict avant de passer a la suivante. Quand les trois ACKs sont vus,
`wifi txcmd` indique `bound=acked` dans son rapport, et `wifi txcmd <cible> arm`
peut envoyer le `TX_CMD` avec le meme garde-fou: contexte arme, RX pret,
binding ACKe, puis reponse firmware strictement associee a la sequence TX.
L'etat `wifi status` et le rapport `wifi rx poll` separent maintenant l'ACK
firmware de la reponse AP: Orizon ne marque une association comme confirmee que
si authentication TX, association TX, binding STA, authentication response et
association response sont tous acceptes. `wifi join <ssid> [password]` enchaine
maintenant automatiquement bringup, scan, connexion, binding, auth/assoc, puis
la sequence WPA2 M1/M2/M3/M4 avec progression courte. Pour WPA2, le chemin data
reste volontairement bloque tant que les cles de chiffrement ne sont pas
installees. `wifi key pairwise [arm]`
prepare la commande Intel `SEC_KEY_CMD` du groupe data-path pour installer la
cle paire CCMP derivee du PTK; elle ne s'envoie qu'apres association confirmee,
binding STA ACKe et `wifi txcmd m2 arm` ACKe. Apres M3, Orizon dechiffre le key
data avec AES key unwrap, extrait la GTK, puis `wifi key gtk [arm]` prepare et
peut envoyer la cle groupe CCMP. Le chemin WPA attend ensuite `wifi txcmd m4 arm`
avant de marquer la data path comme prete. `wifi data` construit alors une
premiere trame data protegee CCMP de diagnostic, et `wifi tx data` /
`wifi txcmd data arm` peuvent la faire passer par le meme chemin TX garde. La
pile IPv4 sait maintenant choisir ce lien Wi-Fi quand WPA2 est guarded-ready:
`net status` affiche `link=wifi`, puis `net dhcp`, ARP, IPv4 et les essais
GitHub passent par des trames Ethernet encapsulees en CCMP. Il reste a valider
ce chemin sur le Lenovo avec un vrai AP et a durcir les traces de diagnostic
quand un AP refuse ou chiffre differemment une trame protegee.

Pour importer localement le firmware Intel depuis le Linux du Lenovo sans le
committer dans Git:

```powershell
python scripts/orizon/import_intel_wifi_firmware.py
python scripts/orizon/orizon_update.py --mode zimaos-vm
```

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
