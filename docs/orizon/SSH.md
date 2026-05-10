# Orizon SSH

Orizon contient maintenant la premiere brique serveur SSH integree au kernel.
Elle sert a valider le reseau entrant avant d'activer un vrai shell distant.

## Commandes

```text
net dhcp
ssh start
ssh status
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
- Diagnostic client: capture la banniere du client et detecte le debut KEX.
- Securite: aucun mot de passe ni backdoor n'est cree tant que KEX/auth/shell
  ne sont pas termines.

Depuis un autre PC du meme reseau:

```text
ssh root@<ip-orizon>
```

Le client doit atteindre Orizon, afficher une erreur d'echange de cles ou de
fermeture de session, et `ssh status` doit montrer une session et la banniere
client recue. C'est le comportement attendu pour cette etape.

## Prochaine brique

Pour transformer ce listener en acces distant complet, il reste a ajouter:

- echange de cles SSH moderne, par exemple curve25519/sha256
- chiffrement de transport et MAC
- authentification explicite configuree par l'utilisateur
- canal `session` et pseudo-console Orizon
- limite de tentatives et journalisation des echecs
