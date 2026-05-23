# Orizon SSH

Orizon contient maintenant une base serveur SSH integree au kernel. Elle valide
le reseau entrant, la negociation crypto et le debut de transport chiffre avant
d'activer un vrai shell distant.

Etat exact et limites: [STATUS.md](STATUS.md). Le SSH est une interface admin
de diagnostic pour VM/ZimaOS et captures materiel futures; il ne remplace pas
encore une separation POSIX user/admin complete.

## Commandes

```text
net dhcp
ssh password <mot-de-passe>
ssh start
ssh status
ssh audit
ssh sessions
ssh auth
ssh auth max <essais>
ssh auth lockout <secondes>
ssh auth default
ssh hostkey
security
ssh hostkey reload
ssh hostkey reset
ssh algorithms
ssh reload
ssh lockout clear
ssh password off
ssh poll
ssh stop
logs ssh
logs security
report save
install-plan
system status
system repair
rescue
hostname
hostname set orizon-vm
firstboot done
selftest ssh
bootguard
bootguard confirm
bootguard recover
rollback
```

`ssh password <mot-de-passe>` active l'authentification explicite pour
l'utilisateur `orizon` et stocke le SHA-256 dans `/system/ssh.conf`.
`ssh start` configure IPv4 si necessaire, ouvre TCP/22, charge la configuration
et journalise dans `/logs/ssh.log`.
`ssh auth` affiche la politique active, `ssh reload` recharge `/system/ssh.conf`,
`ssh lockout clear` retire un verrouillage temporaire, et `ssh password off`
desactive l'authentification par mot de passe.
`ssh auth max <essais>` et `ssh auth lockout <secondes>` changent la politique
anti-bruteforce puis la sauvegardent; `ssh auth default` remet `3` essais et
`30` secondes de verrouillage.
`ssh audit` et `ssh sessions` affichent le meme rapport d'audit depuis la
console locale que les commandes distantes `audit` et `ssh sessions`.
`ssh hostkey` affiche l'identite hote, `ssh hostkey reload` recharge
`/system/ssh_host_rsa.key`, et `ssh hostkey reset` regenere une cle RSA locale
persistante pour l'installation courante. La cle de bootstrap compilee ne sert
plus que de secours si la generation ou la persistence echoue.
Apres connexion OpenSSH, les commandes admin utiles peuvent aussi etre lancees
directement avec `ssh orizon@<ip> "ssh auth max 4"`, `ssh orizon@<ip> "ssh
lockout clear"` ou `ssh orizon@<ip> "ssh hostkey reload"`.
Les diagnostics reseau non destructifs sont aussi exposes avec `ssh
orizon@<ip> "net check"` et `ssh orizon@<ip> "net tls"`. Les commandes qui
peuvent couper la session active (`net dhcp`, `net auto`, `net renew`,
`net reset`, modifications `net config`) restent volontairement reservees a la
console locale.

## Etat actuel

- TCP entrant: actif sur le port 22.
- ARP/IPv4: utilise la stack `netstack` existante.
- Banniere: `SSH-2.0-OrizonSSH_0.1`.
- KEXINIT: Orizon envoie son paquet `SSH_MSG_KEXINIT`, parse celui du client
  et choisit `curve25519-sha256`, `rsa-sha2-256`, `aes128-ctr` et
  `hmac-sha2-256` quand OpenSSH les propose.
- Diagnostic client: capture la banniere du client, le premier KEX, le premier
  hostkey propose, puis le paquet ECDH suivant.
- X25519: Orizon parse la cle publique du paquet `SSH_MSG_KEX_ECDH_INIT`,
  calcule sa cle publique serveur et le secret partage, puis affiche leurs
  empreintes SHA-256 dans `ssh algorithms`.
- Signature hote: Orizon construit un blob `ssh-rsa`, signe le hash d'echange
  avec `rsa-sha2-256`, puis envoie `SSH_MSG_KEX_ECDH_REPLY`.
- Cle hote: Orizon genere une cle RSA 1024 bits par installation, la persiste
  dans `/system/ssh_host_rsa.key`, la recharge au demarrage du service SSH et
  expose le fingerprint via `ssh hostkey` / `ssh algorithms`.
- Transport chiffre: Orizon derive IV, cles AES-128-CTR et cles HMAC-SHA256,
  echange `SSH_MSG_NEWKEYS`, lit le premier `SERVICE_REQUEST` chiffre et
  repond par `SERVICE_ACCEPT`.
- Authentification: Orizon refuse `none`, annonce `password`, accepte seulement
  l'utilisateur `orizon` si un mot de passe a ete configure depuis la console.
- Durcissement auth: les echecs de mot de passe sont comptes, avec verrouillage
  temporaire configurable dans `/system/ssh.conf` (`max-attempts`,
  `lockout-seconds`).
- Canal session: Orizon accepte `session`, `pty-req`, `shell` et `exec`, expose
  un shell distant de diagnostic avec `help`, `ls`, `cd`, `cat`, `head`, `tail`,
  `touch`, `mkdir`, `rm`, `write`, `append`, `system status`,
  `system repair`, `rescue`, `hostname`, `hostname set <name>`, `logs`, `net`,
  `net check`, `net tcp`, `net tls`, `net diag`, `route`, `dns`, `ping`, `usb`, `wifi`, `ps`,
  `security`, `pkg`, `update`, `update status`, `storage`,
  `storage diag`, `persist status`, `persist slots`, `persist save`,
  `persist restore previous`, `persist repair`,
  `logs storage`, `logs pci`, `disk identify`,
  `disk read-test`, `disk read-test last`, `gpt scan`, `selftest`, `pci`,
  `pci bars`, `hw next`, `report next`, `report save`, `install-plan`,
  `free`, `timer`, `bootguard`, `bootguard confirm`, `bootguard recover`,
  `rollback`, `audit`, `ssh sessions`, `sync`, `reboot`,
  `shutdown`, `status`, `auth`, `hostkey`, `whoami`,
  `uname`, `pwd`, `uptime` et `exit`, puis ferme proprement avec `exit-status`.
  Le mode shell PTY accepte les fins de ligne
  CR/LF d'OpenSSH, echo les caracteres saisis, et peut enchainer plusieurs
  commandes dans une meme session interactive.
- Audit: `audit` / `ssh sessions` affiche le cumul des sessions, auth reussies/echouees,
  commandes `exec`, commandes shell, fermetures de canal, recoveries listener,
  temps idle, derniere commande et les derniers evenements recents; les
  evenements sont aussi journalises dans `/logs/ssh.log` avec le mot de passe
  masque et miroitent dans `/logs/security.log`. Les changements de politique
  auth, lockout et hostkey ajoutent aussi une entree non secrete dans
  `logs security`. Le meme rapport est disponible localement avec `ssh audit`.
- Journaux: `logs ssh`, `logs security`, `logs boot`, `logs storage` et
  `logs pci` affichent les etats utiles sans action destructive; storage/PCI
  sont des snapshots diagnostiques quand aucun vrai fichier journal persistant
  n'existe encore.
- Rapport materiel: `report next` / `hw next` affiche le plan de capture
  materielle future sans rien ecrire. `report save` ecrit
  `/workspace/hardware-report.txt` depuis SSH pour capturer storage, PCI BARs,
  USB, Wi-Fi, reseau, bootguard, update, selftest et les queues de logs avant
  une validation reelle. `cat
  /workspace/hardware-report.txt`, `tail /workspace/hardware-report.txt` et
  `report show` utilisent maintenant un tampon de sortie plus large ou une vue
  de fin de fichier pour lire un rapport complet depuis un vrai client SSH,
  sans passer par l'ecran framebuffer.
- Rapport installateur VM: `install-plan` est non destructif et ecrit
  `/workspace/.orizon/install-report.txt`; lire ensuite avec `cat
  /workspace/.orizon/install-report.txt` ou `logs install` pour verifier le
  mode, le disque cible, la portee d'ecriture et la confirmation requise avant
  toute installation.
- Etat installe/live: `system status` distingue live ISO et boot installe,
  affiche hostname, first-boot, racines persistantes, fichiers initiaux et
  etat init/logs. `system services` montre la petite politique services,
  `system doctor` audite sans ecrire, et `system init` rafraichit
  `/system/boot-state` plus `/logs/init.log`. `system repair` recree seulement
  les defaults manquants et ecrit `/workspace/.orizon/rescue-report.txt`;
  `rescue` affiche la checklist de recuperation sans installation.
  `hostname set <name>` persiste `/system/hostname`, et `firstboot done`
  marque la premiere session installee comme revue.
- Commandes admin distantes: `exec` sait modifier la politique auth avec
  `ssh auth max`, `ssh auth lockout`, `ssh auth default`, changer ou couper le
  mot de passe avec `ssh password`, nettoyer le lockout avec `ssh lockout
  clear`, recharger/reinitialiser la cle hote, editer des fichiers avec
  `write`/`append`/`touch`/`mkdir`/`rm` uniquement dans les racines autorisees
  `/workspace`, `/home`, `/logs` et `/packages`, sans pouvoir ecrire
  `/workspace/.orizon` ni les noms sensibles (`.env`, `.key`, `.pem`, `.ssh`,
  private, secret, token, credential, `id_rsa`, `id_ed25519`), lancer les diagnostics non destructifs
  `security`, `selftest`, `disk identify`, `disk read-test`, `disk read-test last`,
  `gpt scan`, `persist status`, `persist slots`, `persist save`,
  `persist restore previous`, `persist repair`,
  `bootguard confirm`, `rollback`, et sauvegarder avec `sync`. En VM, `reboot` et `shutdown` persistent d'abord les racines Orizon
  puis planifient le redemarrage ou l'extinction.
- Robustesse: le chemin SSH utilise des buffers statiques pour les gros
  paquets, segmente les longues sorties `CHANNEL_DATA` en plusieurs paquets,
  garde le transport reutilisable apres une commande `exec`, renvoie `127` sur
  une commande distante inconnue, et remet l'ecoute TCP/22 en etat apres une
  vraie deconnexion ou une session idle. Le `snprintf`
  kernel supporte maintenant l'alignement a gauche
  (`%-Ns`), ce qui evite les corruptions d'arguments dans les sorties comme
  `ps`.
- Securite: aucun mot de passe par defaut ni backdoor n'est cree; sans
  `ssh password`, l'auth reste desactivee. Les fichiers sensibles
  `/system/ssh.conf` et `/system/ssh_host_rsa.key`, ainsi que les chemins avec
  noms `private`, `secret`, `token` ou `password`, sont bloques par les
  commandes generiques `cat/head/tail/write/append/touch/mkdir/rm`. Utiliser
  `ssh auth`, `ssh hostkey`, `security`, `hostname set` ou `net config` pour les
  operations encadrees.

Depuis un autre PC du meme reseau:

```text
ssh orizon@<ip-orizon>
```

Pour relancer la regression SSH depuis ce poste via le serveur ZimaOS:

```powershell
$env:ORIZON_SSH_PASSWORD = "orizonpw"
.\scripts\orizon\test_orizon_ssh.ps1
```

Le script utilise `zimaos-orizon` et `192.168.122.138` par defaut. Ces valeurs
peuvent etre changees avec `-ZimaHost`, `-VmIp`, ou les variables
`ORIZON_ZIMA_HOST` et `ORIZON_VM_IP`.

Le client doit atteindre Orizon, voir le logiciel distant `OrizonSSH_0.1`,
recevoir `KEXINIT`, `ECDH_REPLY`, `NEWKEYS`, puis `SERVICE_ACCEPT`. Apres
authentification password, OpenSSH peut ouvrir un canal `session`; `exec` et le
mini-shell interactif fonctionnent deja pour les diagnostics de base, les
commandes admin listees plus haut, et les commandes `pkg status/list/info`,
`security`, `pkg search`, `pkg remote`, `pkg remote verify`,
`pkg upgrade plan`, `pkg sample`, `pkg hash`, `pkg verify`, `pkg update`,
`pkg upgrade`, `pkg install`, `pkg remove`, `pkg rollback` et `pkg history`.
`ssh status` et `ssh algorithms` affichent
la banniere client, la negociation
choisie, les empreintes X25519, le hash d'echange, la signature, les cles
derivees, l'etat auth et l'etat canal.

## Prochaine brique

Pour transformer ce listener en acces distant complet, il reste a ajouter:

- ajouter une vraie separation user/admin au-dela du shell admin `orizon`
- brancher le shell SSH sur une vraie pseudo-console Orizon partageant toute
  l'ergonomie locale; le sous-ensemble distant couvre deja les diagnostics,
  update/rollback et le flux paquet principal
- rotation/rechargement propre des cles hote dans `/system/ssh.conf`
