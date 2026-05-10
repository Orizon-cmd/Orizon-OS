# Orizon SSH

Orizon contient maintenant une base serveur SSH integree au kernel. Elle valide
le reseau entrant, la negociation crypto et le debut de transport chiffre avant
d'activer un vrai shell distant.

## Commandes

```text
net dhcp
ssh start
ssh status
ssh algorithms
ssh poll
ssh stop
logs ssh
```

`ssh start` configure IPv4 si necessaire, ouvre TCP/22, ecrit
`/system/ssh.conf` et journalise dans `/logs/ssh.log`.

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
- Transport chiffre: Orizon derive IV, cles AES-128-CTR et cles HMAC-SHA256,
  echange `SSH_MSG_NEWKEYS`, lit le premier `SERVICE_REQUEST` chiffre et
  repond par `SERVICE_ACCEPT`.
- Securite: aucun mot de passe ni backdoor n'est cree tant que KEX/auth/shell
  ne sont pas termines.

Depuis un autre PC du meme reseau:

```text
ssh root@<ip-orizon>
```

Le client doit atteindre Orizon, voir le logiciel distant `OrizonSSH_0.1`,
recevoir `KEXINIT`, `ECDH_REPLY`, `NEWKEYS`, puis `SERVICE_ACCEPT`. Il envoie
ensuite `USERAUTH_REQUEST`; Orizon ferme volontairement ici, car
l'authentification et le canal shell ne sont pas encore actifs. `ssh status` et
`ssh algorithms` affichent la banniere client, la negociation choisie, les
empreintes X25519, le hash d'echange, la signature et les cles derivees.

## Prochaine brique

Pour transformer ce listener en acces distant complet, il reste a ajouter:

- remplacer la cle hote RSA de developpement par une cle persistante par
  installation
- authentification explicite configuree par l'utilisateur
- canal `session`, `pty-req`, `shell` et pseudo-console Orizon
- limites de tentatives, delais anti-bruteforce et journalisation des echecs
- rotation/rechargement propre des cles et options dans `/system/ssh.conf`
