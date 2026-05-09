# Orizon OS Hardware Boot Notes

## Splash Screen Freeze

If Orizon reaches the graphical splash screen on a real laptop but never opens
the console, the bootloader and kernel entry point already worked. The common
failure is later in early runtime: the current kernel still uses a legacy
PIT/PIC timer path, while many modern UEFI laptops expect APIC timer support.

Orizon now tries a Local APIC timer first. It parses ACPI MADT, maps the LAPIC,
calibrates the timer against the PIT counter, and uses a dedicated interrupt
vector for scheduler ticks. If LAPIC is unavailable, it keeps the previous PIT
path. If neither timer interrupt arrives after boot, the shell still avoids
sleeping with `hlt` and continues in polling fallback mode so the machine
remains usable enough for diagnostics.

After boot, run:

```text
sysinfo
report
hw
pci
input
```

Check the timer line:

```text
timer source=lapic status=LAPIC timer online ...
timer source=pit status=PIT timer online ...
timer irq=active fallback=off
timer irq=not-seen fallback=polling
```

`source=lapic` is the target path for modern laptops. `source=pit` means the
machine did not expose a usable LAPIC path and Orizon used the legacy fallback.
`fallback=polling` means no timer interrupt reached the shell, so Orizon stayed
awake manually instead of freezing at the splash.

## Current Real-Hardware Tips

- Disable Secure Boot if the firmware blocks the unsigned Limine/kernel image.
- Boot in UEFI mode, not legacy CSM.
- If the splash appears, Secure Boot is not the blocker; the kernel is already
  running.
- Prefer a simple USB-A keyboard for early tests if the built-in keyboard does
  not respond yet.
- Take a photo of the `sysinfo` and `report` output after boot; those commands
  show timer, storage, network, USB, PS/2, and boot payload state.
- Use `pci` and `input` for real laptops. They expose PCI driver hints and
  pointer-bus candidates such as USB, SMBus, or Intel LPSS I2C.
