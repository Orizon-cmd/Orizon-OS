# Orizon SSH

Orizon contient maintenant la premiere brique serveur SSH integree au kernel.
Elle sert a valider le reseau entrant avant d'activer un vrai shell distant.

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
  et choisit les premiers algorithmes communs.
- Diagnostic client: capture la banniere du client, le premier KEX, le premier
  hostkey propose, puis le paquet ECDH suivant.
- Securite: aucun mot de passe ni backdoor n'est cree tant que KEX/auth/shell
  ne sont pas termines.

Depuis un autre PC du meme reseau:

```text
ssh root@<ip-orizon>
```

Le client doit atteindre Orizon, voir le logiciel distant `OrizonSSH_0.1`,
recevoir un `KEXINIT`, puis afficher une fermeture diagnostique avant
l'authentification. `ssh status` et `ssh algorithms` doivent montrer la session,
la banniere client recue, le type du dernier paquet et la negotiation choisie.
C'est le comportement attendu pour cette etape.

## Prochaine brique

Pour transformer ce listener en acces distant complet, il reste a ajouter:

- reponse ECDH curve25519/sha256 avec cle serveur persistante
- derivation des cles de transport, chiffrement et MAC
- authentification explicite configuree par l'utilisateur
- canal `session` et pseudo-console Orizon
- limite de tentatives et journalisation des echecs
