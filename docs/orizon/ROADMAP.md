# Orizon OS Roadmap

## Immediate Track

1. Finish the remaining branding cleanup in code comments, helper APIs, and VM tooling.
2. Decide whether Orizon should prioritize `x86_64` or `ARM64` first.
3. Produce one reproducible local build path on Linux and document it.
4. Build a minimal VM validation checklist before larger feature work starts.

## Near-Term Engineering

1. Audit the `orizon-os-x86_64/` path as the fastest route to a bootable Orizon VM.
2. Identify where boot labels, terminal banners, and interface text are hardcoded.
3. Add a small release or artifact layout for test ISOs and kernels.
4. Define the first Orizon OS versioning and release naming scheme.

## Lab Discipline

1. Keep secrets in ignored local env files only.
2. Save server facts locally after each important infrastructure change.
3. Watch the ZimaOS root filesystem carefully because it is currently full.
