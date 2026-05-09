# Orizon OS Hardware Boot Notes

## Splash Screen Freeze

If Orizon reaches the graphical splash screen on a real laptop but never opens
the console, the bootloader and kernel entry point already worked. The common
failure is later in early runtime: the current kernel still uses a legacy
PIT/PIC timer path, while many modern UEFI laptops expect APIC timer support.

The shell now avoids sleeping with `hlt` until at least one timer interrupt has
been observed. If no timer tick arrives, Orizon leaves the splash screen and
continues in polling fallback mode so the machine remains usable enough for
diagnostics.

After boot, run:

```text
sysinfo
report
```

Check the timer line:

```text
timer irq=active fallback=off
timer irq=not-seen fallback=polling
```

`fallback=polling` means the laptop booted without a working legacy timer IRQ.
This is useful for testing, but the long-term fix is APIC/LAPIC timer support.

## Current Real-Hardware Tips

- Disable Secure Boot if the firmware blocks the unsigned Limine/kernel image.
- Boot in UEFI mode, not legacy CSM.
- If the splash appears, Secure Boot is not the blocker; the kernel is already
  running.
- Prefer a simple USB-A keyboard for early tests if the built-in keyboard does
  not respond yet.
- Take a photo of the `sysinfo` and `report` output after boot; those commands
  show timer, storage, network, USB, PS/2, and boot payload state.
