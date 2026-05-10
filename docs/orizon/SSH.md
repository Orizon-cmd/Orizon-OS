# Orizon SSH

Orizon contient maintenant une base serveur SSH integree au kernel. Elle valide
le reseau entrant, la negociation crypto et le debut de transport chiffre avant
d'activer un vrai shell distant.

## Commandes

```text
net dhcp
ssh password <mot-de-passe>
ssh start
ssh status
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
ssh password off
ssh poll
ssh stop
logs ssh
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
`ssh hostkey` affiche l'identite hote, `ssh hostkey reload` recharge
`/system/ssh_host_rsa.key`, et `ssh hostkey reset` recree le fichier depuis le
materiel RSA de bootstrap.
Apres connexion OpenSSH, les commandes admin utiles peuvent aussi etre lancees
directement avec `ssh orizon@<ip> "ssh auth max 4"`, `ssh orizon@<ip> "ssh
lockout clear"` ou `ssh orizon@<ip> "ssh hostkey reload"`.

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
- Cle hote: Orizon persiste maintenant le materiel RSA CRT dans
  `/system/ssh_host_rsa.key`, le recharge au demarrage du service SSH et expose
  le fingerprint via `ssh hostkey` / `ssh algorithms`.
- Transport chiffre: Orizon derive IV, cles AES-128-CTR et cles HMAC-SHA256,
  echange `SSH_MSG_NEWKEYS`, lit le premier `SERVICE_REQUEST` chiffre et
  repond par `SERVICE_ACCEPT`.
- Authentification: Orizon refuse `none`, annonce `password`, accepte seulement
  l'utilisateur `orizon` si un mot de passe a ete configure depuis la console.
- Durcissement auth: les echecs de mot de passe sont comptes, avec verrouillage
  temporaire configurable dans `/system/ssh.conf` (`max-attempts`,
  `lockout-seconds`).
- Canal session: Orizon accepte `session`, `pty-req`, `shell` et `exec`, expose
  un shell distant de diagnostic avec `help`, `ls`, `cd`, `cat`, `head`,
  `touch`, `mkdir`, `rm`, `write`, `append`, `logs`, `net`, `route`, `dns`,
  `ps`, `pkg`, `storage`, `free`, `timer`, `audit`, `sync`, `status`, `auth`,
  `hostkey`, `whoami`, `uname`, `pwd`, `uptime` et `exit`, puis ferme
  proprement avec `exit-status`.
- Audit: `audit` affiche le cumul des sessions, auth reussies/echouees,
  commandes `exec`, commandes shell, fermetures de canal, recoveries listener,
  temps idle et derniere commande; les evenements sont aussi journalises dans
  `/logs/ssh.log` avec le mot de passe masque.
- Commandes admin distantes: `exec` sait modifier la politique auth avec
  `ssh auth max`, `ssh auth lockout`, `ssh auth default`, changer ou couper le
  mot de passe avec `ssh password`, nettoyer le lockout avec `ssh lockout
  clear`, recharger/reinitialiser la cle hote, editer des fichiers avec
  `write`/`append`/`touch`/`mkdir`/`rm`, et sauvegarder avec `sync`.
- Robustesse: le chemin SSH utilise des buffers statiques pour les gros
  paquets et remet l'ecoute TCP/22 en etat apres une fermeture de canal ou une
  session idle. Le `snprintf` kernel supporte maintenant l'alignement a gauche
  (`%-Ns`), ce qui evite les corruptions d'arguments dans les sorties comme
  `ps`.
- Securite: aucun mot de passe par defaut ni backdoor n'est cree; sans
  `ssh password`, l'auth reste desactivee.

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
mini-shell interactif fonctionnent deja pour les diagnostics de base et les
commandes admin listees plus haut. `ssh status` et `ssh algorithms` affichent
la banniere client, la negociation
choisie, les empreintes X25519, le hash d'echange, la signature, les cles
derivees, l'etat auth et l'etat canal.

## Prochaine brique

Pour transformer ce listener en acces distant complet, il reste a ajouter:

- remplacer la cle hote RSA de developpement par une cle persistante par
  installation, generee par Orizon au lieu d'etre derivee du bootstrap compile
- durcir encore l'authentification: rotation du hash, permissions du fichier
  config et journalisation plus detaillee par IP
- brancher le shell SSH sur une vraie pseudo-console Orizon partageant toutes
  les commandes locales, au lieu du sous-ensemble distant actuel
- rotation/rechargement propre des cles hote dans `/system/ssh.conf`
