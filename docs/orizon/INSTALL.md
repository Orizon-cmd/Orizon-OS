# Orizon OS Installer

The update system is paused while Orizon OS gets a real disk installation path.
The live ISO cannot safely rewrite itself, so the next durable model is:

1. boot the live ISO,
2. run `install`,
3. collect language, keyboard, hostname, and disk strategy,
4. write a staging plan under `/workspace/.orizon/`,
5. later hand that plan to a GPT/FAT32 boot writer with A/B boot slots.

## Current In-OS Command

Run from the Orizon console:

```text
install
```

The guided flow currently asks for:

- language: `fr_FR` or `en_US`
- keyboard: `fr-azerty` or `us-qwerty`
- disk mode: `guided-full-disk-a-b` or `manual-later`
- hostname, defaulting to `orizon-os`
- explicit confirmation with `INSTALL` or `install`

It writes:

```text
/workspace/.orizon/install-plan
/workspace/.orizon/install-state
/system/install-state
/system/locale
/system/keyboard
```

Only `/workspace` is persistent today. The `/system` files are runtime state
until the real installed root filesystem exists.

## Safety Boundary

The installer does not partition or overwrite the disk yet. That is deliberate:
Orizon OS still needs a proper GPT writer, FAT32/ESP writer, boot entry writer,
and rollback-safe A/B boot slots before destructive disk writes are acceptable.

## Next Kernel Layers

- Read disk identity and size with ATA IDENTIFY.
- Create a GPT layout from the installer plan.
- Format/write an ESP FAT32 boot partition.
- Copy kernel, Limine files, and system metadata into slot A.
- Keep slot B reserved for rollback updates.
- Mark an installation as bootable only after verification.
