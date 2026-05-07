# Build On Linux

Le dépôt racine pointe maintenant vers le socle actif `x86_64`.

## Dépendances

Sur Debian ou Ubuntu :

```bash
sudo apt update
sudo apt install -y clang lld xorriso qemu-system-x86 make curl
```

## Build

Depuis la racine du dépôt :

```bash
make
```

Le noyau est généré dans `orizon-os-x86_64/build/kernel.elf` et l’image ISO
dans `orizon-os-x86_64/orizonos-x86_64.iso`.

## Exécution locale

```bash
make run-bios
```

Le mode `run` UEFI dépend d’un firmware OVMF disponible sur la machine.

## Boucle VM distante

Pour utiliser directement le serveur ZimaOS :

```powershell
python scripts/orizon/build_x86_64_on_zimaos.py --deploy-vm
powershell -File scripts/orizon/open_orizon_vnc.ps1
```
